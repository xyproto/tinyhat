#include <gtk/gtk.h>
#include <curl/curl.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UA "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0"
#define MAX_PAGE (4 * 1024 * 1024)
#define MAX_RESULTS 10
#define MB (1024 * 1024)

static GtkWidget *win, *entry, *button, *statuslbl;
static GtkWidget *radios[8];
static gboolean busy = FALSE;
static gboolean headless = FALSE;
static int play_ok = 0;
static gboolean ddg_blocked = FALSE;

static const char *archive_exts = " zip 7z rar gz tgz bz2 xz ";

typedef struct {
	const char *name;
	const char *dir;
	const char *romdir;
	const char *search;
	const char *exts;
	const char *keywords;
	const char *launcher;
	gsize max;
} Platform;

static const Platform platforms[] = {
	{ "SNES", "snes", "ROMs/snes", "snes rom", " sfc smc fig swc ", "\\b(snes|super nintendo|super famicom|sfc)\\b", "tinyhat-emulate", 64 * MB },
	{ "GBA", "gba", "ROMs/gba", "gba rom", " gba ", "\\b(gba|game ?boy advance)\\b", "tinyhat-emulate", 64 * MB },
	{ "Sega Genesis", "genesis", "ROMs/genesis", "sega genesis rom", " md gen smd bin ", "\\b(genesis|mega ?drive)\\b", "tinyhat-emulate", 32 * MB },
	{ "C64", "c64", "ROMs/c64", "c64 game", " d64 d71 d81 g64 prg t64 tap crt p00 ", "\\b(commodore ?64|c64)\\b", "tinyhat-emulate", 16 * MB },
	{ "DOS", "dos", "DOS", "dos game", " exe com bat iso img cue ", "\\b(dos|ms-?dos)\\b", "tinyhat-dosbox", 512 * MB },
	{ "TIC-80", "tic80", "ROMs/tic80", "tic-80 game", " tic tic80 ", "\\b(tic-?80)\\b", "tic80", 16 * MB },
	{ "NES", "nes", "ROMs/nes", "nes rom", " nes fds unf unif ", "\\b(nes|famicom|nintendo entertainment system)\\b", "tinyhat-emulate", 8 * MB },
	{ "Sega Master System", "sms", "ROMs/sms", "sega master system rom", " sms gg ", "\\b(master ?system|sms|game ?gear)\\b", "tinyhat-emulate", 8 * MB },
};

static const int radio_order[] = { 3, 4, 1, 6, 2, 7, 0, 5 };

static const Platform *plat = &platforms[0];

typedef struct {
	char *url;
	char *page;
	char *name;
	int score;
	int direct;
} Candidate;

static const char *home(void)
{
	const char *h = g_get_home_dir();
	return h && *h ? h : "/tmp";
}

static char *rom_dir_for(const Platform *p)
{
	return g_build_filename(home(), p->romdir, NULL);
}

static char *rom_dir(void)
{
	return rom_dir_for(plat);
}

static char *cache_dir(void)
{
	return g_build_filename(g_get_user_cache_dir(), "tinyhat-play", NULL);
}

static void norm(const char *in, char *out, size_t n)
{
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 1 < n; i++)
		if (isalnum((unsigned char)in[i]))
			out[j++] = (char)tolower((unsigned char)in[i]);
	out[j] = '\0';
}

static char *stem(const char *name)
{
	char *b = g_path_get_basename(name);
	char *dot = strrchr(b, '.');
	if (dot && dot != b)
		*dot = '\0';
	return b;
}

static int ext_in(const char *name, const char *bag)
{
	const char *dot = strrchr(name, '.');
	char e[16], hit[20];
	size_t j = 0;
	if (!dot || !dot[1])
		return 0;
	for (size_t i = 1; dot[i] && j + 1 < sizeof(e); i++)
		e[j++] = (char)tolower((unsigned char)dot[i]);
	e[j] = '\0';
	snprintf(hit, sizeof(hit), " %s ", e);
	return strstr(bag, hit) != NULL;
}

static int match_score(const char *query, const char *name)
{
	char nq[256], nn[512];
	char *s = stem(name);
	char **words = g_regex_split_simple("[^A-Za-z0-9]+", query, 0, 0);
	int total = 0, hits = 0;
	norm(query, nq, sizeof(nq));
	norm(s, nn, sizeof(nn));
	g_free(s);
	if (!nq[0] || !nn[0]) {
		g_strfreev(words);
		return 0;
	}
	if (!strcmp(nq, nn)) {
		g_strfreev(words);
		return 1000;
	}
	for (int i = 0; words[i]; i++) {
		char w[128];
		norm(words[i], w, sizeof(w));
		if (strlen(w) < 2)
			continue;
		total++;
		if (strstr(nn, w))
			hits++;
	}
	g_strfreev(words);
	if (strstr(nn, nq))
		return 500 - (int)(strlen(nn) - strlen(nq));
	if (total && hits == total)
		return 100 + hits;
	return 0;
}

static char *index_path(void)
{
	char *c = cache_dir();
	char *p = g_build_filename(c, "index", NULL);
	g_free(c);
	return p;
}

static char *index_key(const char *query, const Platform *p)
{
	char nq[256];
	norm(query, nq, sizeof(nq));
	return g_strdup_printf("%s/%s", p->dir, nq);
}

static char *index_lookup(const char *query, const Platform *p)
{
	char *key = index_key(query, p), *path = index_path(), *data = NULL, *found = NULL;
	if (g_file_get_contents(path, &data, NULL, NULL)) {
		char **lines = g_strsplit(data, "\n", -1);
		for (int i = 0; lines[i] && !found; i++) {
			char *tab = strchr(lines[i], '\t');
			if (!tab)
				continue;
			*tab = '\0';
			if (!strcmp(lines[i], key) && g_file_test(tab + 1, G_FILE_TEST_IS_REGULAR))
				found = g_strdup(tab + 1);
		}
		g_strfreev(lines);
		g_free(data);
	}
	g_free(path);
	g_free(key);
	return found;
}

static void index_add(const char *query, const char *rom, const Platform *p)
{
	char *key = index_key(query, p), *c = cache_dir(), *path = index_path();
	FILE *f;
	g_mkdir_with_parents(c, 0755);
	if (key[0] && (f = fopen(path, "a"))) {
		fprintf(f, "%s\t%s\n", key, rom);
		fclose(f);
	}
	g_free(c);
	g_free(path);
	g_free(key);
}

static char *find_cached(const char *query, const Platform **found_plat)
{
	char *best_path = NULL;
	int best_score = 0;
	const Platform *best_plat = NULL;
	for (size_t i = 0; i < G_N_ELEMENTS(platforms); i++) {
		const Platform *p = &platforms[i];
		char *hit, *dir;
		DIR *d;
		struct dirent *e;
		if (!headless && p != plat)
			continue;
		hit = index_lookup(query, p);
		if (hit) {
			if (found_plat)
				*found_plat = p;
			return hit;
		}
		dir = rom_dir_for(p);
		d = opendir(dir);
		if (d) {
			while ((e = readdir(d))) {
				int s;
				if (e->d_name[0] == '.' || !ext_in(e->d_name, p->exts))
					continue;
				s = match_score(query, e->d_name);
				if (s > best_score) {
					char *path = g_build_filename(dir, e->d_name, NULL);
					g_free(best_path);
					best_path = path;
					best_score = s;
					best_plat = p;
				}
			}
			closedir(d);
		}
		g_free(dir);
	}
	if (best_path && found_plat)
		*found_plat = best_plat;
	return best_path;
}

static gboolean status_idle(gpointer data)
{
	gtk_label_set_text(GTK_LABEL(statuslbl), data);
	g_free(data);
	return G_SOURCE_REMOVE;
}

static void status(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (headless) {
		char *m = g_strdup_vprintf(fmt, ap);
		puts(m);
		g_free(m);
	} else {
		g_idle_add(status_idle, g_strdup_vprintf(fmt, ap));
	}
	va_end(ap);
}

static void set_controls(gboolean active)
{
	busy = !active;
	if (headless)
		return;
	gtk_widget_set_sensitive(button, active);
	gtk_widget_set_sensitive(entry, active);
	for (size_t i = 0; i < G_N_ELEMENTS(radios); i++)
		if (radios[i])
			gtk_widget_set_sensitive(radios[i], active);
}

static gboolean spawn_game(const char *rom, GError **err)
{
	char *argv[4];
	if (!rom)
		return FALSE;
	if (!strcmp(plat->launcher, "tinyhat-emulate")) {
		argv[0] = "tinyhat-emulate";
		argv[1] = (char *)plat->dir;
		argv[2] = (char *)rom;
		argv[3] = NULL;
	} else if (!strcmp(plat->launcher, "tinyhat-dosbox")) {
		argv[0] = "tinyhat-dosbox";
		argv[1] = (char *)rom;
		argv[2] = NULL;
	} else {
		argv[0] = (char *)plat->launcher;
		argv[1] = (char *)rom;
		argv[2] = NULL;
	}
	return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, err);
}

static gboolean launch_idle(gpointer data)
{
	GError *err = NULL;
	if (!data) {
		set_controls(TRUE);
		return G_SOURCE_REMOVE;
	}
	if (spawn_game(data, &err)) {
		g_free(data);
		gtk_widget_destroy(win);
		return G_SOURCE_REMOVE;
	}
	{
		char *m = g_strdup_printf("Could not start the emulator: %s", err->message);
		gtk_label_set_text(GTK_LABEL(statuslbl), m);
		g_free(m);
		g_error_free(err);
	}
	g_free(data);
	set_controls(TRUE);
	return G_SOURCE_REMOVE;
}

static void finish(char *rom)
{
	if (headless) {
		GError *err = NULL;
		play_ok = rom && spawn_game(rom, &err);
		if (rom && !play_ok) {
			fprintf(stderr, "tinyhat-play: could not start the emulator: %s\n", err->message);
			g_error_free(err);
		}
		g_free(rom);
	} else {
		g_idle_add(launch_idle, rom);
	}
}

typedef struct {
	GString *buf;
	FILE *fp;
	size_t got, max;
} Sink;

static size_t sink_write(char *ptr, size_t size, size_t nmemb, void *ud)
{
	Sink *s = ud;
	size_t n = size * nmemb;
	if (s->got + n > s->max)
		return 0;
	s->got += n;
	if (s->buf)
		g_string_append_len(s->buf, ptr, (gssize)n);
	else if (fwrite(ptr, 1, n, s->fp) != n)
		return 0;
	return n;
}

static CURL *new_handle(int cookies)
{
	CURL *c = curl_easy_init();
	if (!c)
		return NULL;
	curl_easy_setopt(c, CURLOPT_USERAGENT, UA);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink_write);
	if (cookies)
		curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");
	return c;
}

static int fetch_with(CURL *c, const char *url, const char *post, struct curl_slist *hdr, Sink *s,
                      char **effective)
{
	long code = 0;
	CURLcode rc;
	char *eff = NULL;
	struct curl_slist *all = NULL, *it;
	for (it = hdr; it; it = it->next)
		all = curl_slist_append(all, it->data);
	all = curl_slist_append(all, "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
	all = curl_slist_append(all, "Accept-Language: en-US,en;q=0.5");
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, s);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, all);
	if (post)
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, post);
	else
		curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
	rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
	if (effective)
		*effective = g_strdup(eff ? eff : url);
	curl_slist_free_all(all);
	return rc == CURLE_OK && code >= 200 && code < 300;
}

static int fetch(const char *url, const char *post, Sink *s, char **effective)
{
	CURL *c = new_handle(0);
	int ok;
	if (!c)
		return 0;
	ok = fetch_with(c, url, post, NULL, s, effective);
	curl_easy_cleanup(c);
	return ok;
}

static curl_off_t remote_size(const char *url)
{
	CURL *c = new_handle(0);
	curl_off_t len = -1;
	CURLcode rc;
	if (!c)
		return -1;
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
	rc = curl_easy_perform(c);
	if (rc == CURLE_OK)
		curl_easy_getinfo(c, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
	curl_easy_cleanup(c);
	return len;
}

static int fetch_limited(const char *url, Sink *s, curl_off_t max)
{
	CURL *c = new_handle(0);
	int ok;
	if (!c)
		return 0;
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, max);
	ok = fetch_with(c, url, NULL, NULL, s, NULL);
	curl_easy_cleanup(c);
	return ok;
}

static GString *fetch_page(const char *url, const char *post, char **effective)
{
	Sink s = { g_string_new(NULL), NULL, 0, MAX_PAGE };
	if (fetch(url, post, &s, effective))
		return s.buf;
	g_string_free(s.buf, TRUE);
	if (effective) {
		g_free(*effective);
		*effective = NULL;
	}
	return NULL;
}

static char *html_unescape(const char *s)
{
	GString *o = g_string_new(NULL);
	while (*s) {
		if (!strncmp(s, "&amp;", 5)) {
			g_string_append_c(o, '&');
			s += 5;
		} else if (!strncmp(s, "&#x2F;", 6) || !strncmp(s, "&#47;", 5)) {
			g_string_append_c(o, '/');
			s += s[2] == 'x' ? 6 : 5;
		} else if (!strncmp(s, "&quot;", 6)) {
			g_string_append_c(o, '"');
			s += 6;
		} else {
			g_string_append_c(o, *s++);
		}
	}
	return g_string_free(o, FALSE);
}

static char *resolve(const char *base, const char *href)
{
	GUri *b, *r;
	char *out;
	if (!href[0] || href[0] == '#' || !g_ascii_strncasecmp(href, "javascript:", 11) ||
	    !g_ascii_strncasecmp(href, "mailto:", 7))
		return NULL;
	b = g_uri_parse(base, G_URI_FLAGS_ENCODED, NULL);
	if (!b)
		return NULL;
	r = g_uri_parse_relative(b, href, G_URI_FLAGS_ENCODED, NULL);
	g_uri_unref(b);
	if (!r)
		return NULL;
	if (g_strcmp0(g_uri_get_scheme(r), "http") && g_strcmp0(g_uri_get_scheme(r), "https")) {
		g_uri_unref(r);
		return NULL;
	}
	out = g_uri_to_string(r);
	g_uri_unref(r);
	return out;
}

static char *url_host(const char *url)
{
	GUri *u = g_uri_parse(url, G_URI_FLAGS_ENCODED, NULL);
	char *h = NULL;
	if (u) {
		h = g_ascii_strdown(g_uri_get_host(u) ? g_uri_get_host(u) : "", -1);
		g_uri_unref(u);
	}
	return h;
}

static int host_is(const char *host, const char *domain)
{
	size_t hl = strlen(host), dl = strlen(domain);
	return hl >= dl && !strcmp(host + hl - dl, domain) && (hl == dl || host[hl - dl - 1] == '.');
}

static int skip_host(const char *url)
{
	char *h = url_host(url);
	int skip = !h || !h[0] || host_is(h, "duckduckgo.com");
	g_free(h);
	return skip;
}

static char *url_filename(const char *url)
{
	GUri *u = g_uri_parse(url, G_URI_FLAGS_ENCODED, NULL);
	char *name = NULL;
	if (u) {
		char *path = g_uri_unescape_string(g_uri_get_path(u), NULL);
		if (path) {
			name = g_path_get_basename(path);
			g_free(path);
		}
		g_uri_unref(u);
	}
	return name;
}

static char *github_raw(const char *url)
{
	char *h = url_host(url), *out = NULL;
	const char *blob;
	if (h && !strcmp(h, "github.com") && (blob = strstr(url, "/blob/"))) {
		out = g_strdup_printf("%.*s/raw/%s", (int)(blob - url), url, blob + 6);
	}
	g_free(h);
	return out ? out : g_strdup(url);
}

static char *json_string(const char **pp)
{
	GString *o = g_string_new(NULL);
	const char *p = *pp + 1;
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 'n':
				g_string_append_c(o, '\n');
				break;
			case 't':
				g_string_append_c(o, '\t');
				break;
			case 'u':
				g_string_append_c(o, '?');
				for (int i = 0; i < 4 && isxdigit((unsigned char)p[1]); i++)
					p++;
				break;
			default:
				g_string_append_c(o, *p);
			}
			p++;
		} else {
			g_string_append_c(o, *p++);
		}
	}
	*pp = *p ? p + 1 : p;
	return g_string_free(o, FALSE);
}

static char *json_top_string(const char *js, const char *key)
{
	int depth = 0;
	const char *p = js;
	while (*p) {
		if (*p == '{' || *p == '[') {
			depth++;
			p++;
		} else if (*p == '}' || *p == ']') {
			depth--;
			p++;
		} else if (*p == '"') {
			char *str = json_string(&p);
			const char *q = p;
			while (isspace((unsigned char)*q))
				q++;
			if (*q == ':' && depth == 1 && !strcmp(str, key)) {
				q++;
				while (isspace((unsigned char)*q))
					q++;
				g_free(str);
				return *q == '"' ? json_string(&q) : NULL;
			}
			g_free(str);
		} else {
			p++;
		}
	}
	return NULL;
}

static char *itch_game_url(const char *url)
{
	GUri *u = g_uri_parse(url, G_URI_FLAGS_ENCODED, NULL);
	char *h = url_host(url), *out = NULL;
	if (u && h && host_is(h, "itch.io") && strcmp(h, "itch.io") && strcmp(h, "www.itch.io")) {
		const char *p = g_uri_get_path(u);
		size_t n = p && p[0] == '/' ? strcspn(p + 1, "/") : 0;
		if (n)
			out = g_strdup_printf("https://%s/%.*s", h, (int)n, p + 1);
	}
	if (u)
		g_uri_unref(u);
	g_free(h);
	return out;
}

static char *itch_csrf(const char *html)
{
	GMatchInfo *mi, *vi;
	char *tok = NULL;
	GRegex *re = g_regex_new("<(?:meta|input)\\s[^>]*\\bname=\"csrf_token\"[^>]*>", 0, 0, NULL);
	GRegex *vre = g_regex_new("\\b(?:value|content)=\"([^\"]+)\"", 0, 0, NULL);
	if (g_regex_match(re, html, 0, &mi)) {
		char *tag = g_match_info_fetch(mi, 0);
		if (g_regex_match(vre, tag, 0, &vi))
			tok = g_match_info_fetch(vi, 1);
		g_match_info_free(vi);
		g_free(tag);
	}
	g_match_info_free(mi);
	g_regex_unref(vre);
	g_regex_unref(re);
	return tok;
}

static char *itch_post(CURL *c, const char *url, const char *html, const char *referer)
{
	char *tok = itch_csrf(html), *esc, *post, *ref, *out = NULL;
	struct curl_slist *hdr;
	Sink s = { g_string_new(NULL), NULL, 0, MAX_PAGE };
	if (!tok) {
		g_string_free(s.buf, TRUE);
		return NULL;
	}
	esc = g_uri_escape_string(tok, NULL, FALSE);
	post = g_strdup_printf("csrf_token=%s", esc);
	ref = g_strdup_printf("Referer: %s", referer);
	hdr = curl_slist_append(NULL, "X-Requested-With: XMLHttpRequest");
	hdr = curl_slist_append(hdr, ref);
	if (fetch_with(c, url, post, hdr, &s, NULL))
		out = json_top_string(s.buf->str, "url");
	curl_slist_free_all(hdr);
	g_string_free(s.buf, TRUE);
	g_free(ref);
	g_free(post);
	g_free(esc);
	g_free(tok);
	return out;
}

static char *upload_ext(const char *title)
{
	char **parts = g_regex_split_simple("[\\s\\[\\](),]+", title, 0, 0);
	char *ext = NULL;
	for (int i = 0; parts[i] && !ext; i++)
		if (ext_in(parts[i], plat->exts) || ext_in(parts[i], archive_exts))
			ext = g_ascii_strdown(strrchr(parts[i], '.') + 1, -1);
	g_strfreev(parts);
	return ext;
}

static void itch_uploads(GPtrArray *cands, const char *html, const char *page, int base)
{
	const char *slug = strrchr(page, '/') + 1;
	GMatchInfo *mi;
	GRegex *re = g_regex_new("data-upload_id=\"(\\d+)\"(?:(?!data-upload_id=).)*?<strong[^>]*\\btitle=\"([^\"]+)\"",
	                         G_REGEX_DOTALL, 0, NULL);
	int i = 0;
	g_regex_match(re, html, 0, &mi);
	while (g_match_info_matches(mi)) {
		char *id = g_match_info_fetch(mi, 1), *raw = g_match_info_fetch(mi, 2);
		char *title = html_unescape(raw), *ext = upload_ext(title), *lower = g_ascii_strdown(title, -1);
		if (!ext && g_regex_match_simple(plat->keywords, lower, 0, 0))
			ext = g_strndup(plat->exts + 1, strcspn(plat->exts + 1, " "));
		if (ext) {
			Candidate *c = g_new0(Candidate, 1);
			c->url = g_strdup_printf("%s/file/%s?source=view_game&as_props=1", page, id);
			c->page = g_strdup(page);
			c->name = g_strdup_printf("%s.%s", slug, ext);
			c->direct = ext_in(c->name, plat->exts);
			c->score = base + (c->direct ? 50 : 0) - i;
			if (strstr(lower, "source"))
				c->score -= 300;
			if (g_regex_match_simple("\\b(old|beta|demo)\\b", lower, 0, 0))
				c->score -= 20;
			g_ptr_array_add(cands, c);
		}
		i++;
		g_free(lower);
		g_free(ext);
		g_free(title);
		g_free(raw);
		g_free(id);
		g_match_info_next(mi, NULL);
	}
	g_match_info_free(mi);
	g_regex_unref(re);
}

static void itch_discover(GPtrArray *cands, GHashTable *seen, const char *query, const char *url)
{
	char *page = itch_game_url(url), *dl;
	Sink s;
	CURL *c;
	int base;
	if (!page || g_hash_table_contains(seen, page)) {
		g_free(page);
		return;
	}
	g_hash_table_add(seen, g_strdup(page));
	base = match_score(query, strrchr(page, '/') + 1);
	if (base <= 0 || !(c = new_handle(1))) {
		g_free(page);
		return;
	}
	status("Looking at %s", page);
	s = (Sink){ g_string_new(NULL), NULL, 0, MAX_PAGE };
	if (fetch_with(c, page, NULL, NULL, &s, NULL)) {
		if (strstr(s.buf->str, "data-upload_id=")) {
			itch_uploads(cands, s.buf->str, page, base);
		} else {
			char *endpoint = g_strdup_printf("%s/download_url", page);
			dl = itch_post(c, endpoint, s.buf->str, page);
			g_free(endpoint);
			if (dl) {
				Sink d = { g_string_new(NULL), NULL, 0, MAX_PAGE };
				if (fetch_with(c, dl, NULL, NULL, &d, NULL))
					itch_uploads(cands, d.buf->str, page, base);
				g_string_free(d.buf, TRUE);
				g_free(dl);
			}
		}
	}
	g_string_free(s.buf, TRUE);
	curl_easy_cleanup(c);
	g_free(page);
}

static char *itch_resolve(Candidate *cand)
{
	CURL *c = new_handle(1);
	Sink s = { g_string_new(NULL), NULL, 0, MAX_PAGE };
	char *url = NULL;
	if (c && fetch_with(c, cand->page, NULL, NULL, &s, NULL))
		url = itch_post(c, cand->url, s.buf->str, cand->page);
	g_string_free(s.buf, TRUE);
	if (c)
		curl_easy_cleanup(c);
	return url;
}

static void consider(GPtrArray *cands, GHashTable *seen, const char *query, const char *url)
{
	char *name, *raw;
	int direct, score;
	Candidate *c;
	if (g_hash_table_contains(seen, url) || skip_host(url))
		return;
	name = url_filename(url);
	if (!name)
		return;
	direct = ext_in(name, plat->exts);
	if (!direct && !ext_in(name, archive_exts)) {
		g_free(name);
		return;
	}
	score = match_score(query, name);
	g_free(name);
	if (score <= 0)
		return;
	g_hash_table_add(seen, g_strdup(url));
	raw = github_raw(url);
	c = g_new0(Candidate, 1);
	c->url = raw;
	c->score = score + (direct ? 50 : 0);
	c->direct = direct;
	g_ptr_array_add(cands, c);
}

static GPtrArray *extract_links(const char *html, const char *base)
{
	GPtrArray *links = g_ptr_array_new_with_free_func(g_free);
	GRegex *re = g_regex_new("href\\s*=\\s*[\"']([^\"'<>]+)[\"']", G_REGEX_CASELESS, 0, NULL);
	GMatchInfo *mi;
	g_regex_match(re, html, 0, &mi);
	while (g_match_info_matches(mi)) {
		char *href = g_match_info_fetch(mi, 1);
		char *un = html_unescape(href);
		char *abs = resolve(base, un);
		if (abs)
			g_ptr_array_add(links, abs);
		g_free(un);
		g_free(href);
		g_match_info_next(mi, NULL);
	}
	g_match_info_free(mi);
	g_regex_unref(re);
	return links;
}

static char *ddg_target(const char *href)
{
	const char *u = strstr(href, "uddg=");
	if (u) {
		const char *end = strchr(u + 5, '&');
		char *enc = end ? g_strndup(u + 5, (gsize)(end - u - 5)) : g_strdup(u + 5);
		char *dec = g_uri_unescape_string(enc, NULL);
		g_free(enc);
		return dec;
	}
	if (!strncmp(href, "//", 2))
		return g_strconcat("https:", href, NULL);
	return g_strdup(href);
}

static void ddg_collect(GString *page, GPtrArray *results, const char *pattern)
{
	if (!page)
		return;
	if (strstr(page->str, "anomaly"))
		ddg_blocked = TRUE;
	GRegex *re = g_regex_new(pattern, 0, 0, NULL);
	GMatchInfo *mi;
	g_regex_match(re, page->str, 0, &mi);
	while (g_match_info_matches(mi) && results->len < MAX_RESULTS) {
		char *href = g_match_info_fetch(mi, 1);
		char *un = html_unescape(href);
		char *t = ddg_target(un);
		if (t && (!strncmp(t, "http://", 7) || !strncmp(t, "https://", 8)) && !skip_host(t))
			g_ptr_array_add(results, t);
		else
			g_free(t);
		g_free(un);
		g_free(href);
		g_match_info_next(mi, NULL);
	}
	g_match_info_free(mi);
	g_regex_unref(re);
}

static GPtrArray *ddg_search(const char *query)
{
	GPtrArray *results = g_ptr_array_new_with_free_func(g_free);
	char *q = g_strdup_printf("%s %s", query, plat->search);
	char *esc = g_uri_escape_string(q, NULL, FALSE);
	char *post = g_strdup_printf("q=%s&kl=wt-wt", esc);
	GString *page = fetch_page("https://html.duckduckgo.com/html/", post, NULL);
	ddg_collect(page, results, "class=\"result__a\"[^>]*href=\"([^\"]+)\"");
	if (page)
		g_string_free(page, TRUE);
	if (results->len == 0) {
		page = fetch_page("https://lite.duckduckgo.com/lite/", post, NULL);
		ddg_collect(page, results, "rel=\"nofollow\" href=\"([^\"]+)\"");
		if (page)
			g_string_free(page, TRUE);
	}
	g_free(q);
	g_free(esc);
	g_free(post);
	return results;
}

static gint cand_cmp(gconstpointer a, gconstpointer b)
{
	const Candidate *x = *(Candidate *const *)a, *y = *(Candidate *const *)b;
	return y->score - x->score;
}

static void cand_free(gpointer p)
{
	Candidate *c = p;
	g_free(c->url);
	g_free(c->page);
	g_free(c->name);
	g_free(c);
}

static int looks_like_html(const char *path)
{
	char buf[256];
	size_t n;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 1;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	for (size_t i = 0; i < n; i++)
		buf[i] = (char)tolower((unsigned char)buf[i]);
	return n == 0 || strstr(buf, "<!doctype html") || strstr(buf, "<html");
}

static int is_archive(const char *path)
{
	unsigned char b[8] = { 0 };
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fread(b, 1, sizeof(b), f) < 4) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return !memcmp(b, "PK\x03\x04", 4) || !memcmp(b, "7z\xbc\xaf", 4) || !memcmp(b, "Rar!", 4) ||
	       !memcmp(b, "\x1f\x8b", 2) || !memcmp(b, "BZh", 3) || !memcmp(b, "\xfd" "7zXZ", 5);
}

static int tic_script_ok(const unsigned char *code, size_t n)
{
	/* Tiny Hat's TIC-80 builds only the Lua and Fennel runtimes. Mirror TIC-80's
	   language autodetection (the "<comment> script:" metatag parsed by
	   tic_tool_metatag and used by tic_get_script) and reject carts written for
	   any other runtime, which TIC-80 would otherwise feed to Lua, failing with
	   a confusing "unexpected symbol" parse error. */
	static const char *comments[] = { "--", ";", "#", "//" };
	char buf[4096], needle[16];
	size_t len = MIN(n, sizeof buf - 1);
	memcpy(buf, code, len);
	buf[len] = '\0';
	for (size_t i = 0; i < len; i++)
		if (!code[i]) { /* the code chunk is zero padded */
			buf[i] = '\0';
			break;
		}
	for (size_t t = 0; t < G_N_ELEMENTS(comments); t++) {
		snprintf(needle, sizeof needle, "%.2s script:", comments[t]);
		const char *hit = strstr(buf, needle);
		if (!hit)
			continue;
		hit += strlen(needle);
		while (*hit == ' ' || *hit == '\t')
			hit++;
		int lua = !strncmp(hit, "lua", 3) && !isalnum((unsigned char)hit[3]);
		int fennel = !strncmp(hit, "fennel", 6) && !isalnum((unsigned char)hit[6]);
		return lua || fennel; /* janet, mruby, python (#) and js, wren (//) never run on Tiny Hat */
	}
	return 1;
}

static int tic_cart_ok(const unsigned char *d, size_t n)
{
	/* Mirror the chunk stream walk of TIC-80's tic_cart_load (src/cart.c): a
	   valid raw cartridge is a run of 4-byte chunk headers whose payloads cover
	   the file exactly and end with a code chunk, so plain text and other
	   non-cart files are rejected. Chunk types above CHUNK_LANG (20) are
	   rejected, as is a lang byte for an unsupported runtime. */
	size_t ptr = 0;
	long lang = -1;
	int has_code = 0;
	while (ptr + 4 <= n) {
		unsigned type = d[ptr] & 0x1f;
		unsigned size = d[ptr + 1] | (d[ptr + 2] << 8);
		size_t payload = !size && (type == 5 || type == 19) ? 0x10000 : size; /* TIC_BANK_SIZE */
		if (type > 20 || ptr + 4 + payload > n)
			return 0;
		if (type == 20) {
			if (size == 1)
				lang = d[ptr + 4];
			else
				lang = -1;
		} else if (type == 5)
			has_code = 1;
		ptr += 4 + payload;
	}
	if (ptr != n || !has_code)
		return 0;
	if (lang >= 0 && lang != 10 && lang != 14) /* 10 = lua, 14 = fennel */
		return 0;
	/* find the first code chunk again to scan its script metatag */
	ptr = 0;
	while (ptr + 4 <= n) {
		unsigned type = d[ptr] & 0x1f;
		unsigned size = d[ptr + 1] | (d[ptr + 2] << 8);
		size_t payload = !size && (type == 5 || type == 19) ? 0x10000 : size;
		if (type == 5) {
			size_t left = n - (ptr + 4);
			return tic_script_ok(d + ptr + 4, MIN(payload, left));
		}
		ptr += 4 + payload;
	}
	return 0;
}

static int rom_ok(const char *path)
{
	unsigned char b[0x110];
	size_t n;
	FILE *f;
	if (looks_like_html(path) || !(f = fopen(path, "rb")))
		return 0;
	n = fread(b, 1, sizeof(b), f);
	fclose(f);
	if (n < sizeof(b) || !memcmp(b, "\xff\xd8\xff", 3) || !memcmp(b, "\x89PNG", 4) || !memcmp(b, "GIF8", 4) ||
	    !memcmp(b, "%PDF", 4))
		return 0;
	if (!strcmp(plat->dir, "tic80")) {
		/* zip carts are extracted by install(), and the inner cart is
		   validated here, so a zip file is accepted as a cartridge */
		if (!memcmp(b, "PK\x03\x04", 4))
			return 1;
		size_t full;
		if (!(f = fopen(path, "rb")))
			return 0;
		fseek(f, 0, SEEK_END);
		full = ftell(f);
		rewind(f);
		unsigned char *all = full && full <= (gsize)plat->max ? g_malloc(full) : NULL;
		size_t got = all ? fread(all, 1, full, f) : 0;
		fclose(f);
		int ok = all && got == full && tic_cart_ok(all, got);
		g_free(all);
		return ok;
	}
	if (!strcmp(plat->dir, "gba"))
		return b[0xb2] == 0x96;
	if (!strcmp(plat->dir, "nes") && ext_in(path, " nes "))
		return !memcmp(b, "NES\x1a", 4);
	if (!strcmp(plat->dir, "genesis") && !ext_in(path, " smd ")) {
		for (size_t i = 0x100; i + 4 <= sizeof(b); i++)
			if (!memcmp(b + i, "SEGA", 4))
				return 1;
		return 0;
	}
	return 1;
}

static char *find_rom_in(const char *dir, const char *query)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	const char *n;
	char *best = NULL;
	int best_score = -1;
	if (!d)
		return NULL;
	while ((n = g_dir_read_name(d))) {
		char *p = g_build_filename(dir, n, NULL);
		char *hit = NULL;
		int s;
		if (g_file_test(p, G_FILE_TEST_IS_SYMLINK)) {
			g_free(p);
			continue;
		}
		if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
			hit = find_rom_in(p, query);
			g_free(p);
			p = hit;
		} else if (!ext_in(n, plat->exts) || !rom_ok(p)) {
			g_free(p);
			continue;
		}
		if (!p)
			continue;
		s = match_score(query, p);
		if (s > best_score) {
			g_free(best);
			best = p;
			best_score = s;
		} else {
			g_free(p);
		}
	}
	g_dir_close(d);
	return best;
}

static int remove_tree(const char *path)
{
	GDir *d = g_dir_open(path, 0, NULL);
	const char *n;
	if (d) {
		while ((n = g_dir_read_name(d))) {
			char *p = g_build_filename(path, n, NULL);
			if (g_file_test(p, G_FILE_TEST_IS_DIR) && !g_file_test(p, G_FILE_TEST_IS_SYMLINK))
				remove_tree(p);
			else
				unlink(p);
			g_free(p);
		}
		g_dir_close(d);
	}
	return rmdir(path);
}

static int move_file(const char *src, const char *dst)
{
	char *data = NULL;
	gsize len = 0;
	int ok;
	if (!rename(src, dst))
		return 1;
	if (!g_file_get_contents(src, &data, &len, NULL))
		return 0;
	ok = g_file_set_contents(dst, data, (gssize)len, NULL);
	g_free(data);
	if (ok)
		unlink(src);
	return ok;
}

static int same_name(const char *ns, const char *name)
{
	char nw[256];
	if (!name)
		return 0;
	norm(name, nw, sizeof(nw));
	return nw[0] && (!strcmp(ns, nw) || (strlen(ns) == 8 && !strncmp(ns, nw, 8)));
}

static void dos_scan(const char *dir, int depth, const char *game, const char *query, char **best, int *best_rank,
                     int *best_depth)
{
	GDir *d = g_dir_open(dir, 0, NULL);
	char *folder = g_path_get_basename(dir);
	const char *n;
	if (!d) {
		g_free(folder);
		return;
	}
	while ((n = g_dir_read_name(d))) {
		char *p = g_build_filename(dir, n, NULL), *st, ns[64];
		int rank;
		if (g_file_test(p, G_FILE_TEST_IS_SYMLINK)) {
			g_free(p);
			continue;
		}
		if (g_file_test(p, G_FILE_TEST_IS_DIR)) {
			dos_scan(p, depth + 1, game, query, best, best_rank, best_depth);
			g_free(p);
			continue;
		}
		rank = ext_in(n, " bat ") ? 1 : ext_in(n, " exe ") ? 2 : ext_in(n, " com ") ? 3 : 0;
		st = stem(n);
		norm(st, ns, sizeof(ns));
		g_free(st);
		if (!rank || !strncmp(ns, "setup", 5) || !strncmp(ns, "install", 7)) {
			g_free(p);
			continue;
		}
		if (same_name(ns, game) || same_name(ns, query) || (depth && same_name(ns, folder)))
			rank = 0;
		if (!*best || rank < *best_rank ||
		    (rank == *best_rank && (depth < *best_depth || (depth == *best_depth && strcmp(p, *best) < 0)))) {
			g_free(*best);
			*best = p;
			*best_rank = rank;
			*best_depth = depth;
		} else {
			g_free(p);
		}
	}
	g_dir_close(d);
	g_free(folder);
}

static char *install_dos(const char *extract, const char *game, const char *query)
{
	GDir *d = g_dir_open(extract, 0, NULL);
	char *root = g_strdup(extract), *best = NULL, *dir, *target, *dest = NULL, nm[256], base[9];
	const char *n;
	int count = 0, rank = 0, depth = 0;
	size_t len = 0;
	if (d) {
		char *only = NULL;
		while ((n = g_dir_read_name(d))) {
			count++;
			g_free(only);
			only = g_build_filename(extract, n, NULL);
		}
		g_dir_close(d);
		if (count == 1 && g_file_test(only, G_FILE_TEST_IS_DIR) && !g_file_test(only, G_FILE_TEST_IS_SYMLINK)) {
			g_free(root);
			root = only;
		} else {
			g_free(only);
		}
	}
	dos_scan(root, 0, game, query, &best, &rank, &depth);
	if (!best) {
		g_free(root);
		return NULL;
	}
	norm(game && game[0] ? game : query, nm, sizeof(nm));
	for (; nm[len] && len < 8; len++)
		base[len] = (char)toupper((unsigned char)nm[len]);
	base[len] = '\0';
	if (!len)
		g_strlcpy(base, "GAME", sizeof(base));
	dir = rom_dir();
	g_mkdir_with_parents(dir, 0755);
	target = g_build_filename(dir, base, NULL);
	for (char i = '2'; g_file_test(target, G_FILE_TEST_EXISTS) && i <= '9'; i++) {
		size_t k = strlen(base) < 8 ? strlen(base) : 7;
		base[k] = i;
		base[k + 1] = '\0';
		g_free(target);
		target = g_build_filename(dir, base, NULL);
	}
	if (!g_file_test(target, G_FILE_TEST_EXISTS)) {
		int ok = rename(root, target) == 0;
		if (!ok) {
			char *argv[] = { "cp", "-a", root, target, NULL };
			int st = 1;
			ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &st, NULL) &&
			     g_spawn_check_wait_status(st, NULL);
		}
		if (ok)
			dest = g_strconcat(target, best + strlen(root), NULL);
	}
	g_free(target);
	g_free(dir);
	g_free(best);
	g_free(root);
	return dest;
}

static char *install(const char *query, Candidate *c, const char *work)
{
	char *name = c->name ? g_strdup(c->name) : url_filename(c->url), *file, *extract, *url;
	char *rom = NULL, *dest = NULL;
	Sink s = { NULL, NULL, 0, 0 };
	int ok;
	if (!name || !name[0] || !strcmp(name, "/")) {
		g_free(name);
		name = c->direct ? g_strdup_printf("game.%.*s", (int)strcspn(plat->exts + 1, " "), plat->exts + 1)
		                 : g_strdup("game.zip");
	}
	file = g_build_filename(work, name, NULL);
	url = c->page ? itch_resolve(c) : g_strdup(c->url);
	if (!url) {
		g_free(name);
		g_free(file);
		return NULL;
	}
	{
		curl_off_t size = remote_size(url);
		if (size > (curl_off_t)plat->max) {
			status("Skipping %s: %lld MB is too large for %s", name, (long long)(size / MB), plat->name);
			g_free(url);
			g_free(name);
			g_free(file);
			return NULL;
		}
	}
	s.max = plat->max;
	s.fp = fopen(file, "wb");
	if (!s.fp) {
		g_free(url);
		g_free(name);
		g_free(file);
		return NULL;
	}
	status("Downloading %s", name);
	ok = fetch_limited(url, &s, (curl_off_t)plat->max);
	g_free(url);
	fclose(s.fp);
	if (!ok || looks_like_html(file)) {
		unlink(file);
		g_free(name);
		g_free(file);
		return NULL;
	}
	if (c->direct && !is_archive(file)) {
		rom = rom_ok(file) ? g_strdup(file) : NULL;
	} else {
		char *argv[] = { "bsdtar", "-xf", file, "-C", NULL, NULL };
		int st = 1;
		extract = g_build_filename(work, "x", NULL);
		g_mkdir_with_parents(extract, 0700);
		argv[4] = extract;
		status("Extracting %s", name);
		if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
		                 G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &st, NULL) &&
		    g_spawn_check_wait_status(st, NULL)) {
			if (!strcmp(plat->dir, "dos")) {
				char *game = stem(c->name ? c->name : name);
				dest = install_dos(extract, game, query);
				g_free(game);
			}
			if (!dest)
				rom = find_rom_in(extract, query);
		}
		g_free(extract);
	}
	if (rom) {
		char *dir = rom_dir(), *base;
		if (c->name) {
			char *st = stem(c->name);
			const char *dot = strrchr(rom, '.');
			base = g_strconcat(st, dot ? dot : "", NULL);
			g_free(st);
		} else {
			base = g_path_get_basename(rom);
		}
		g_mkdir_with_parents(dir, 0755);
		dest = g_build_filename(dir, base, NULL);
		if (!move_file(rom, dest)) {
			g_free(dest);
			dest = NULL;
		}
		g_free(dir);
		g_free(base);
		g_free(rom);
	}
	g_free(name);
	g_free(file);
	return dest;
}

static GPtrArray *itch_search(const char *query)
{
	GPtrArray *results = g_ptr_array_new_with_free_func(g_free);
	char *esc = g_uri_escape_string(query, NULL, FALSE);
	char *url = g_strdup_printf("https://itch.io/search?q=%s", esc);
	GString *page;
	status("Searching itch.io for \"%s\"", query);
	page = fetch_page(url, NULL, NULL);
	if (page) {
		GRegex *re = g_regex_new("<a\\s[^>]*\\bclass=\"title game_link\"[^>]*>", 0, 0, NULL);
		GRegex *hre = g_regex_new("\\bhref=\"([^\"]+)\"", 0, 0, NULL);
		GMatchInfo *mi, *hi;
		g_regex_match(re, page->str, 0, &mi);
		while (g_match_info_matches(mi) && results->len < MAX_RESULTS) {
			char *tag = g_match_info_fetch(mi, 0);
			if (g_regex_match(hre, tag, 0, &hi)) {
				char *href = g_match_info_fetch(hi, 1);
				g_ptr_array_add(results, html_unescape(href));
				g_free(href);
			}
			g_match_info_free(hi);
			g_free(tag);
			g_match_info_next(mi, NULL);
		}
		g_match_info_free(mi);
		g_regex_unref(hre);
		g_regex_unref(re);
		g_string_free(page, TRUE);
	}
	g_free(url);
	g_free(esc);
	return results;
}

static char *try_candidates(GPtrArray *cands, const char *query)
{
	char *rom = NULL, *cdir = cache_dir(), *work;
	g_ptr_array_sort(cands, cand_cmp);
	g_mkdir_with_parents(cdir, 0755);
	for (guint i = 0; i < cands->len && !rom; i++) {
		work = g_build_filename(cdir, "work-XXXXXX", NULL);
		if (g_mkdtemp(work)) {
			rom = install(query, cands->pdata[i], work);
			remove_tree(work);
		}
		g_free(work);
	}
	g_free(cdir);
	return rom;
}

static char *search_platform(const char *query, GPtrArray **itch)
{
	char *rom = NULL;
	GPtrArray *cands = g_ptr_array_new_with_free_func(cand_free);
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	if (!ddg_blocked) {
		GPtrArray *results;
		status("Searching DuckDuckGo for a %s ROM of \"%s\"", plat->name, query);
		results = ddg_search(query);
		for (guint i = 0; i < results->len; i++) {
			const char *url = results->pdata[i];
			char *eff = NULL;
			GString *page;
			consider(cands, seen, query, url);
			itch_discover(cands, seen, query, url);
			status("Looking at %s (%u/%u)", url, i + 1, results->len);
			page = fetch_page(url, NULL, &eff);
			if (page) {
				GPtrArray *links = extract_links(page->str, eff);
				for (guint j = 0; j < links->len; j++) {
					consider(cands, seen, query, links->pdata[j]);
					itch_discover(cands, seen, query, links->pdata[j]);
				}
				g_ptr_array_free(links, TRUE);
				g_string_free(page, TRUE);
			}
			g_free(eff);
		}
		g_ptr_array_free(results, TRUE);
		rom = try_candidates(cands, query);
	}
	if (!rom) {
		if (!*itch)
			*itch = itch_search(query);
		g_ptr_array_set_size(cands, 0);
		for (guint i = 0; i < (*itch)->len; i++)
			itch_discover(cands, seen, query, (*itch)->pdata[i]);
		rom = try_candidates(cands, query);
	}
	g_hash_table_destroy(seen);
	g_ptr_array_free(cands, TRUE);
	return rom;
}

static gpointer worker(gpointer data)
{
	char *query = data, *rom;
	const Platform *found = NULL;
	GPtrArray *itch = NULL;
	rom = find_cached(query, &found);
	if (rom) {
		char *b = g_path_get_basename(rom);
		plat = found;
		status("Playing %s (cached)", b);
		g_free(b);
		finish(rom);
		g_free(query);
		return NULL;
	}
	ddg_blocked = FALSE;
	if (headless) {
		for (size_t i = 0; i < G_N_ELEMENTS(platforms) && !rom; i++) {
			plat = &platforms[i];
			rom = search_platform(query, &itch);
		}
	} else {
		rom = search_platform(query, &itch);
	}
	if (itch)
		g_ptr_array_free(itch, TRUE);
	if (rom) {
		char *b = g_path_get_basename(rom);
		index_add(query, rom, plat);
		status("Playing %s (%s)", b, plat->name);
		g_free(b);
	} else if (ddg_blocked) {
		status("No ROM found on itch.io for \"%s\", and DuckDuckGo is limiting searches right now", query);
	} else if (headless) {
		status("No ROM found for \"%s\"", query);
	} else {
		status("No %s ROM found for \"%s\"", plat->name, query);
	}
	finish(rom);
	g_free(query);
	return NULL;
}

static void on_play(GtkWidget *w, gpointer data)
{
	const char *q = gtk_entry_get_text(GTK_ENTRY(entry));
	char *trimmed;
	(void)w;
	(void)data;
	if (busy)
		return;
	trimmed = g_strstrip(g_strdup(q ? q : ""));
	if (!*trimmed) {
		gtk_label_set_text(GTK_LABEL(statuslbl), "Type the name of a game first");
		g_free(trimmed);
		return;
	}
	busy = TRUE;
	set_controls(FALSE);
	g_thread_unref(g_thread_new("tinyhat-play", worker, trimmed));
}

static void on_platform_toggled(GtkToggleButton *b, gpointer data)
{
	if (gtk_toggle_button_get_active(b))
		plat = data;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	(void)data;
	if (ev->keyval != GDK_KEY_Escape)
		return FALSE;
	gtk_widget_destroy(w);
	return TRUE;
}

int main(int argc, char **argv)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	if (argc > 1) {
		GString *q = g_string_new(NULL);
		char *query;
		headless = TRUE;
		for (int i = 1; i < argc; i++) {
			if (i > 1)
				g_string_append_c(q, ' ');
			g_string_append(q, argv[i]);
		}
		query = g_strstrip(g_string_free(q, FALSE));
		if (!*query) {
			fputs("usage: tinyhat-play [game name]\n", stderr);
			curl_global_cleanup();
			return 1;
		}
		worker(query);
		curl_global_cleanup();
		return play_ok ? 0 : 1;
	}
	gtk_init(&argc, &argv);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Tiny Hat Play");
	gtk_window_set_default_size(GTK_WINDOW(win), 540, 230);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(win), box);
	GtkWidget *label = gtk_label_new("Choose a platform, then search for a homebrew game to download and run");
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	for (size_t i = 0; i < G_N_ELEMENTS(radio_order); i++) {
		int idx = radio_order[i];
		GtkWidget *radio = gtk_radio_button_new_with_label_from_widget(
		    i ? GTK_RADIO_BUTTON(radios[radio_order[0]]) : NULL, platforms[idx].name);
		radios[idx] = radio;
		g_signal_connect(radio, "toggled", G_CALLBACK(on_platform_toggled), (gpointer)&platforms[idx]);
		gtk_grid_attach(GTK_GRID(grid), radio, (int)(i % 3), (int)(i / 3), 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radios[0]), TRUE);
	entry = gtk_entry_new();
	g_signal_connect(entry, "activate", G_CALLBACK(on_play), NULL);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	button = gtk_button_new_with_label("Play");
	g_signal_connect(button, "clicked", G_CALLBACK(on_play), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	statuslbl = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(statuslbl), PANGO_ELLIPSIZE_MIDDLE);
	gtk_box_pack_start(GTK_BOX(box), statuslbl, TRUE, TRUE, 0);
	gtk_widget_show_all(win);
	gtk_main();
	curl_global_cleanup();
	return 0;
}
