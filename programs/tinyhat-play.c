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
#define MAX_FILE (32 * 1024 * 1024)
#define MAX_RESULTS 10

static GtkWidget *win, *entry, *button, *statuslbl;
static gboolean busy = FALSE;
static gboolean headless = FALSE;
static int play_ok = 0;

static const char *rom_exts = " sfc smc fig swc ";
static const char *archive_exts = " zip 7z rar gz tgz bz2 xz ";

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

static char *rom_dir(void)
{
	return g_build_filename(home(), "ROMs", "snes", NULL);
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

static char *index_lookup(const char *query)
{
	char nq[256], *p = index_path(), *data = NULL, *found = NULL;
	norm(query, nq, sizeof(nq));
	if (nq[0] && g_file_get_contents(p, &data, NULL, NULL)) {
		char **lines = g_strsplit(data, "\n", -1);
		for (int i = 0; lines[i] && !found; i++) {
			char *tab = strchr(lines[i], '\t');
			if (!tab)
				continue;
			*tab = '\0';
			if (!strcmp(lines[i], nq) && g_file_test(tab + 1, G_FILE_TEST_IS_REGULAR))
				found = g_strdup(tab + 1);
		}
		g_strfreev(lines);
		g_free(data);
	}
	g_free(p);
	return found;
}

static void index_add(const char *query, const char *rom)
{
	char nq[256], *c = cache_dir(), *p = index_path();
	FILE *f;
	norm(query, nq, sizeof(nq));
	g_mkdir_with_parents(c, 0755);
	if (nq[0] && (f = fopen(p, "a"))) {
		fprintf(f, "%s\t%s\n", nq, rom);
		fclose(f);
	}
	g_free(c);
	g_free(p);
}

static char *find_cached(const char *query)
{
	char *hit = index_lookup(query), *dir;
	DIR *d;
	struct dirent *e;
	int best = 0;
	if (hit)
		return hit;
	dir = rom_dir();
	d = opendir(dir);
	if (d) {
		while ((e = readdir(d))) {
			int s;
			if (e->d_name[0] == '.' || !ext_in(e->d_name, rom_exts))
				continue;
			s = match_score(query, e->d_name);
			if (s > best) {
				best = s;
				g_free(hit);
				hit = g_build_filename(dir, e->d_name, NULL);
			}
		}
		closedir(d);
	}
	g_free(dir);
	return hit;
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

static gboolean launch_idle(gpointer data)
{
	char *argv[] = { "zsnes", data, NULL };
	GError *err = NULL;
	if (data && !g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
		char *m = g_strdup_printf("Could not start zsnes: %s", err->message);
		gtk_label_set_text(GTK_LABEL(statuslbl), m);
		g_free(m);
		g_error_free(err);
	}
	g_free(data);
	busy = FALSE;
	gtk_widget_set_sensitive(button, TRUE);
	gtk_widget_set_sensitive(entry, TRUE);
	return G_SOURCE_REMOVE;
}

static void finish(char *rom)
{
	if (headless) {
		char *argv[] = { "zsnes", rom, NULL };
		GError *err = NULL;
		play_ok = rom != NULL;
		if (rom && !g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
			fprintf(stderr, "tinyhat-play: could not start zsnes: %s\n", err->message);
			g_error_free(err);
			play_ok = 0;
		}
		g_free(rom);
	} else {
		finish(rom);
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
	GMatchInfo *mi;
	char *tok = NULL;
	GRegex *re = g_regex_new("name=\"csrf_token\"\\s+(?:value|content)=\"([^\"]+)\"", 0, 0, NULL);
	if (g_regex_match(re, html, 0, &mi))
		tok = g_match_info_fetch(mi, 1);
	g_match_info_free(mi);
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
		if (ext_in(parts[i], rom_exts) || ext_in(parts[i], archive_exts))
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
		if (ext) {
			Candidate *c = g_new0(Candidate, 1);
			c->url = g_strdup_printf("%s/file/%s?source=view_game&as_props=1", page, id);
			c->page = g_strdup(page);
			c->name = g_strdup_printf("%s.%s", slug, ext);
			c->direct = ext_in(c->name, rom_exts);
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
	direct = ext_in(name, rom_exts);
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
	char *q = g_strdup_printf("%s snes rom", query);
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
		} else if (!ext_in(n, rom_exts)) {
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

static char *install(const char *query, Candidate *c, const char *work)
{
	char *name = c->name ? g_strdup(c->name) : url_filename(c->url), *file, *extract, *url;
	char *rom = NULL, *dest = NULL;
	Sink s = { NULL, NULL, 0, MAX_FILE };
	int ok;
	if (!name || !name[0] || !strcmp(name, "/")) {
		g_free(name);
		name = g_strdup(c->direct ? "game.sfc" : "game.zip");
	}
	file = g_build_filename(work, name, NULL);
	s.fp = fopen(file, "wb");
	if (!s.fp) {
		g_free(name);
		g_free(file);
		return NULL;
	}
	status("Downloading %s", name);
	url = c->page ? itch_resolve(c) : g_strdup(c->url);
	ok = url && fetch(url, NULL, &s, NULL);
	g_free(url);
	fclose(s.fp);
	if (!ok || looks_like_html(file)) {
		unlink(file);
		g_free(name);
		g_free(file);
		return NULL;
	}
	if (c->direct) {
		rom = g_strdup(file);
	} else {
		char *argv[] = { "bsdtar", "-xf", file, "-C", NULL, NULL };
		int st = 1;
		extract = g_build_filename(work, "x", NULL);
		g_mkdir_with_parents(extract, 0700);
		argv[4] = extract;
		status("Extracting %s", name);
		if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
		                 G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &st, NULL) &&
		    g_spawn_check_wait_status(st, NULL))
			rom = find_rom_in(extract, query);
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

static gpointer worker(gpointer data)
{
	char *query = data, *rom = find_cached(query), *cdir, *work;
	GPtrArray *results, *cands;
	GHashTable *seen;
	if (rom) {
		char *b = g_path_get_basename(rom);
		status("Playing %s (cached)", b);
		g_free(b);
		finish(rom);
		g_free(query);
		return NULL;
	}
	status("Searching DuckDuckGo for \"%s\"", query);
	results = ddg_search(query);
	if (!results->len) {
		status("No search results for \"%s\"", query);
		g_ptr_array_free(results, TRUE);
		finish(NULL);
		g_free(query);
		return NULL;
	}
	cands = g_ptr_array_new_with_free_func(cand_free);
	seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
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
	g_ptr_array_sort(cands, cand_cmp);
	cdir = cache_dir();
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
	if (rom) {
		char *b = g_path_get_basename(rom);
		index_add(query, rom);
		status("Playing %s", b);
		g_free(b);
	} else {
		status(cands->len ? "Could not download a ROM for \"%s\"" : "No SNES ROM found for \"%s\"",
		       query);
	}
	finish(rom);
	g_hash_table_destroy(seen);
	g_ptr_array_free(cands, TRUE);
	g_ptr_array_free(results, TRUE);
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
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_set_sensitive(entry, FALSE);
	g_thread_unref(g_thread_new("tinyhat-play", worker, trimmed));
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
	gtk_window_set_default_size(GTK_WINDOW(win), 480, 150);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(win), box);
	GtkWidget *label = gtk_label_new("Type the name of a free SNES homebrew game");
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
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
