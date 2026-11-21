#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>

#define MAXITEMS 16
#define SNI "org.kde.StatusNotifierItem"
#define DBUSMENU "com.canonical.dbusmenu"

typedef struct {
	char name[128], owner[128], path[192], key[330];
	sd_bus_slot *signals;
	int shown;
} Item;

static sd_bus *bus;
static sd_event *event;
static Item items[MAXITEMS];
static int nitems;
static uint32_t lastid, shownid;
static sd_event_source *expiry;
static char rundir[512];

static void bar(const char *line)
{
	char path[600];
	int fd;
	snprintf(path, sizeof(path), "%s/tinyhat-bar", rundir);
	if ((fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
		return;
	(void)!write(fd, line, strlen(line));
	close(fd);
}

static void flatten(char *dst, size_t n, const char *src)
{
	size_t j = 0;
	int tag = 0, space = 0;
	for (; *src && j + 1 < n; src++) {
		if (*src == '<') {
			tag = 1;
			continue;
		}
		if (tag) {
			tag = *src != '>';
			continue;
		}
		if (isspace((unsigned char)*src)) {
			space = j > 0;
			continue;
		}
		if (space && j + 2 < n)
			dst[j++] = ' ';
		space = 0;
		dst[j++] = *src;
	}
	dst[j] = '\0';
}

static int expired(sd_event_source *s, uint64_t usec, void *userdata)
{
	uint32_t id = (uint32_t)(uintptr_t)userdata;
	(void)s;
	(void)usec;
	if (id == shownid) {
		bar("notice\n");
		shownid = 0;
		sd_bus_emit_signal(bus, "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
			"NotificationClosed", "uu", id, 1);
	}
	return 0;
}

static int m_notify(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	const char *app, *icon, *summary, *body;
	uint32_t replaces, id;
	int32_t timeout;
	char s[100], b[200], line[300];
	int r;
	(void)userdata;
	(void)err;
	if ((r = sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body)) < 0)
		return r;
	if ((r = sd_bus_message_skip(m, "asa{sv}")) < 0 || (r = sd_bus_message_read(m, "i", &timeout)) < 0)
		return r;
	id = replaces ? replaces : ++lastid;
	flatten(s, sizeof(s), summary);
	flatten(b, sizeof(b), body);
	if (*s && *b)
		snprintf(line, sizeof(line), "notice %.60s: %s", s, b);
	else
		snprintf(line, sizeof(line), "notice %s", *s ? s : b);
	{
		size_t cut = strlen(line);
		if (cut > 117) {
			cut = 117;
			while (cut > 7 && ((unsigned char)line[cut] & 0xc0) == 0x80)
				cut--;
			line[cut] = '\0';
		}
		strcat(line, "\n");
	}
	bar(line);
	shownid = id;
	if (expiry)
		sd_event_source_unref(expiry);
	expiry = NULL;
	if (timeout < 0)
		timeout = 6000;
	if (timeout > 0)
		sd_event_add_time_relative(event, &expiry, CLOCK_MONOTONIC, (uint64_t)(timeout < 60000 ? timeout : 60000) * 1000,
			0, expired, (void *)(uintptr_t)id);
	return sd_bus_reply_method_return(m, "u", id);
}

static int m_close(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	uint32_t id;
	int r;
	(void)userdata;
	(void)err;
	if ((r = sd_bus_message_read(m, "u", &id)) < 0)
		return r;
	if (id && id == shownid) {
		bar("notice\n");
		shownid = 0;
		sd_bus_emit_signal(bus, "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
			"NotificationClosed", "uu", id, 3);
	}
	return sd_bus_reply_method_return(m, "");
}

static int m_caps(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	(void)userdata;
	(void)err;
	return sd_bus_reply_method_return(m, "as", 1, "body");
}

static int m_info(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	(void)userdata;
	(void)err;
	return sd_bus_reply_method_return(m, "ssss", "tinyhat-notify", "Tiny Hat", "1.0", "1.2");
}

static const sd_bus_vtable notify_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("GetCapabilities", "", "as", m_caps, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", m_notify, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CloseNotification", "u", "", m_close, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetServerInformation", "", "ssss", m_info, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("NotificationClosed", "uu", 0),
	SD_BUS_SIGNAL("ActionInvoked", "us", 0),
	SD_BUS_VTABLE_END
};

static int getstr(Item *it, const char *prop, char *out, size_t n)
{
	char *v = NULL;
	out[0] = '\0';
	if (sd_bus_get_property_string(bus, it->name, it->path, SNI, prop, NULL, &v) < 0)
		return -1;
	snprintf(out, n, "%s", v);
	free(v);
	return 0;
}

static int sizescore(const char *dir)
{
	int n = atoi(dir);
	if (n <= 0)
		return -1;
	return n >= 22 ? 1000 - (n - 22) : 500 + n;
}

static void candidate(const char *file, int score, char *out, size_t n, int *best)
{
	struct stat st;
	if (score > *best && stat(file, &st) == 0 && S_ISREG(st.st_mode)) {
		snprintf(out, n, "%s", file);
		*best = score;
	}
}

static int lookin(const char *dir, const char *name, char *out, size_t n, int *best)
{
	char file[1200];
	DIR *d1;
	struct dirent *e1;
	snprintf(file, sizeof(file), "%s/%s.png", dir, name);
	candidate(file, 0, out, n, best);
	if (!(d1 = opendir(dir)))
		return *best >= 0;
	while ((e1 = readdir(d1))) {
		char sub[700];
		DIR *d2;
		struct dirent *e2;
		int s1 = sizescore(e1->d_name);
		if (e1->d_name[0] == '.')
			continue;
		snprintf(sub, sizeof(sub), "%s/%s", dir, e1->d_name);
		snprintf(file, sizeof(file), "%s/%s.png", sub, name);
		candidate(file, s1 >= 0 ? s1 : 0, out, n, best);
		if (!(d2 = opendir(sub)))
			continue;
		while ((e2 = readdir(d2))) {
			int s2 = s1 >= 0 ? s1 : sizescore(e2->d_name);
			if (e2->d_name[0] == '.')
				continue;
			snprintf(file, sizeof(file), "%s/%s/%s.png", sub, e2->d_name, name);
			candidate(file, s2 >= 0 ? s2 : 0, out, n, best);
		}
		closedir(d2);
	}
	closedir(d1);
	return *best >= 0;
}

static int findicon(const char *name, const char *themepath, char *out, size_t n)
{
	static const char *themes[] = { "tinyhat", "Pixelitos", "pixelitos", "hicolor", "Adwaita", NULL };
	char dir[600];
	struct stat st;
	int best = -1;
	if (!*name)
		return 0;
	if (name[0] == '/') {
		if (stat(name, &st) == 0 && strstr(name, ".png")) {
			snprintf(out, n, "%s", name);
			return 1;
		}
		return 0;
	}
	if (*themepath && lookin(themepath, name, out, n, &best))
		return 1;
	for (int i = 0; themes[i]; i++) {
		snprintf(dir, sizeof(dir), "/usr/share/icons/%s", themes[i]);
		if (lookin(dir, name, out, n, &best))
			return 1;
	}
	snprintf(dir, sizeof(dir), "/usr/share/pixmaps/%s.png", name);
	if (stat(dir, &st) == 0) {
		snprintf(out, n, "%s", dir);
		return 1;
	}
	return 0;
}

static int pixmapicon(Item *it, const char *prop, char *out, size_t n)
{
	sd_bus_message *reply = NULL;
	const uint8_t *best = NULL;
	int bw = 0, bh = 0, r, ok = 0;
	if (sd_bus_get_property(bus, it->name, it->path, SNI, prop, NULL, &reply, "a(iiay)") < 0)
		return 0;
	if (sd_bus_message_enter_container(reply, 'a', "(iiay)") < 0)
		goto done;
	while ((r = sd_bus_message_enter_container(reply, 'r', "iiay")) > 0) {
		int32_t w, h;
		const void *data;
		size_t size;
		if (sd_bus_message_read(reply, "ii", &w, &h) < 0 || sd_bus_message_read_array(reply, 'y', &data, &size) < 0)
			break;
		if (w > 0 && h > 0 && size >= (size_t)w * h * 4) {
			int better = !best || (bw < 22 ? w > bw : (w >= 22 && w < bw));
			if (better) {
				best = data;
				bw = w;
				bh = h;
			}
		}
		sd_bus_message_exit_container(reply);
	}
	if (best) {
		png_image png = { .version = PNG_IMAGE_VERSION, .width = (png_uint_32)bw, .height = (png_uint_32)bh,
			.format = PNG_FORMAT_RGBA };
		uint8_t *rgba = malloc((size_t)bw * bh * 4);
		char tmp[700];
		if (rgba) {
			for (size_t i = 0; i < (size_t)bw * bh; i++) {
				rgba[i * 4] = best[i * 4 + 1];
				rgba[i * 4 + 1] = best[i * 4 + 2];
				rgba[i * 4 + 2] = best[i * 4 + 3];
				rgba[i * 4 + 3] = best[i * 4];
			}
			snprintf(out, n, "%s/tinyhat-tray/%s%s.png", rundir, it->name, it->path);
			for (char *c = out + strlen(rundir) + 14; *c; c++)
				if (*c == '/' || *c == ':')
					*c = '_';
			snprintf(tmp, sizeof(tmp), "%s.tmp", out);
			if (png_image_write_to_file(&png, tmp, 0, rgba, 0, NULL) && rename(tmp, out) == 0)
				ok = 1;
			free(rgba);
		}
	}
done:
	sd_bus_message_unref(reply);
	return ok;
}

static void show(Item *it)
{
	char status[32], iconname[256], themepath[512], title[128], icon[700], line[1400];
	int attention;
	getstr(it, "Status", status, sizeof(status));
	if (!strcmp(status, "Passive")) {
		if (it->shown) {
			snprintf(line, sizeof(line), "tray %s\n", it->key);
			bar(line);
		}
		it->shown = 0;
		return;
	}
	attention = !strcmp(status, "NeedsAttention");
	getstr(it, "IconThemePath", themepath, sizeof(themepath));
	getstr(it, "Title", title, sizeof(title));
	icon[0] = '\0';
	if (attention) {
		getstr(it, "AttentionIconName", iconname, sizeof(iconname));
		if (!findicon(iconname, themepath, icon, sizeof(icon)))
			pixmapicon(it, "AttentionIconPixmap", icon, sizeof(icon));
	}
	if (!*icon) {
		getstr(it, "IconName", iconname, sizeof(iconname));
		if (!findicon(iconname, themepath, icon, sizeof(icon)) && !pixmapicon(it, "IconPixmap", icon, sizeof(icon)))
			snprintf(icon, sizeof(icon), "-");
	}
	for (char *c = title; *c; c++)
		if (*c == '\n' || *c == '\t')
			*c = ' ';
	snprintf(line, sizeof(line), "tray %s %s %s\n", it->key, icon, title);
	bar(line);
	it->shown = 1;
}

static Item *finditem(const char *name, const char *path)
{
	for (int i = 0; i < nitems; i++)
		if ((!strcmp(items[i].name, name) || !strcmp(items[i].owner, name)) && !strcmp(items[i].path, path))
			return &items[i];
	return NULL;
}

static void removeitem(Item *it)
{
	char line[400];
	snprintf(line, sizeof(line), "tray %s\n", it->key);
	bar(line);
	sd_bus_emit_signal(bus, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
		"StatusNotifierItemUnregistered", "s", it->key);
	sd_bus_slot_unref(it->signals);
	*it = items[--nitems];
	memset(&items[nitems], 0, sizeof(Item));
}

static int changed(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	Item *it = finditem(sd_bus_message_get_sender(m), sd_bus_message_get_path(m));
	(void)userdata;
	(void)err;
	if (it)
		show(it);
	return 0;
}

static int deferred(sd_event_source *s, void *userdata)
{
	char *key = userdata;
	for (int i = 0; i < nitems; i++)
		if (!strcmp(items[i].key, key))
			show(&items[i]);
	free(key);
	sd_event_source_unref(s);
	return 0;
}

static int m_register(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	const char *service, *sender = sd_bus_message_get_sender(m);
	char name[128], path[192], match[600];
	Item *it;
	int r;
	(void)userdata;
	(void)err;
	if ((r = sd_bus_message_read(m, "s", &service)) < 0)
		return r;
	if (service[0] == '/') {
		snprintf(name, sizeof(name), "%s", sender);
		snprintf(path, sizeof(path), "%s", service);
	} else {
		snprintf(name, sizeof(name), "%s", service);
		snprintf(path, sizeof(path), "/StatusNotifierItem");
	}
	if (!(it = finditem(name, path))) {
		if (nitems == MAXITEMS)
			return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_LIMITS_EXCEEDED, "too many tray icons");
		it = &items[nitems++];
		snprintf(it->name, sizeof(it->name), "%s", name);
		snprintf(it->owner, sizeof(it->owner), "%s", sender);
		snprintf(it->path, sizeof(it->path), "%s", path);
		snprintf(it->key, sizeof(it->key), "%s %s", name, path);
		snprintf(match, sizeof(match), "type='signal',sender='%s',path='%s',interface='" SNI "'", name, path);
		sd_bus_add_match(bus, &it->signals, match, changed, NULL);
		sd_bus_emit_signal(bus, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
			"StatusNotifierItemRegistered", "s", it->key);
	}
	r = sd_bus_reply_method_return(m, "");
	sd_event_add_defer(event, NULL, deferred, strdup(it->key));
	return r;
}

static int m_host(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	(void)userdata;
	(void)err;
	return sd_bus_reply_method_return(m, "");
}

static int p_items(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
	void *userdata, sd_bus_error *err)
{
	int r;
	(void)b;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	if ((r = sd_bus_message_open_container(reply, 'a', "s")) < 0)
		return r;
	for (int i = 0; i < nitems; i++) {
		char s[330];
		snprintf(s, sizeof(s), "%s%s", items[i].name, items[i].path);
		if ((r = sd_bus_message_append(reply, "s", s)) < 0)
			return r;
	}
	return sd_bus_message_close_container(reply);
}

static int p_true(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
	void *userdata, sd_bus_error *err)
{
	(void)b;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	return sd_bus_message_append(reply, "b", 1);
}

static int p_zero(sd_bus *b, const char *path, const char *iface, const char *prop, sd_bus_message *reply,
	void *userdata, sd_bus_error *err)
{
	(void)b;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	return sd_bus_message_append(reply, "i", 0);
}

static const sd_bus_vtable watcher_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "", m_register, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "", m_host, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", p_items, 0, 0),
	SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ProtocolVersion", "i", p_zero, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
	SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
	SD_BUS_VTABLE_END
};

static int ownerchanged(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	const char *name, *old, *new;
	(void)userdata;
	(void)err;
	if (sd_bus_message_read(m, "sss", &name, &old, &new) < 0 || *new)
		return 0;
	for (int i = nitems - 1; i >= 0; i--)
		if (!strcmp(items[i].name, name) || !strcmp(items[i].owner, name))
			removeitem(&items[i]);
	return 0;
}

typedef struct {
	int id;
	char label[200];
} Entry;

static void menulabel(char *dst, size_t n, const char *src)
{
	size_t j = 0;
	for (; *src && j + 1 < n; src++) {
		if (*src == '_') {
			if (src[1] != '_')
				continue;
			src++;
		}
		dst[j++] = *src;
	}
	dst[j] = '\0';
}

static int readmenu(sd_bus_message *m, const char *prefix, Entry *out, int *n, int max)
{
	int32_t id;
	char label[160] = "", type[32] = "", toggle[32] = "";
	int visible = 1, enabled = 1, state = -1, r;
	if ((r = sd_bus_message_enter_container(m, 'r', "ia{sv}av")) <= 0)
		return r;
	sd_bus_message_read(m, "i", &id);
	sd_bus_message_enter_container(m, 'a', "{sv}");
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *key, *contents;
		char t;
		sd_bus_message_read(m, "s", &key);
		sd_bus_message_peek_type(m, &t, &contents);
		sd_bus_message_enter_container(m, 'v', contents);
		if (!strcmp(contents, "s")) {
			const char *v;
			sd_bus_message_read(m, "s", &v);
			if (!strcmp(key, "label"))
				menulabel(label, sizeof(label), v);
			else if (!strcmp(key, "type"))
				snprintf(type, sizeof(type), "%s", v);
			else if (!strcmp(key, "toggle-type"))
				snprintf(toggle, sizeof(toggle), "%s", v);
		} else if (!strcmp(contents, "b")) {
			int v;
			sd_bus_message_read(m, "b", &v);
			if (!strcmp(key, "visible"))
				visible = v;
			else if (!strcmp(key, "enabled"))
				enabled = v;
		} else if (!strcmp(contents, "i")) {
			int32_t v;
			sd_bus_message_read(m, "i", &v);
			if (!strcmp(key, "toggle-state"))
				state = v;
		} else {
			sd_bus_message_skip(m, contents);
		}
		sd_bus_message_exit_container(m);
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);
	{
		char sub[400];
		int children = 0;
		snprintf(sub, sizeof(sub), "%s%s%s", prefix, label, *label ? " › " : "");
		sd_bus_message_enter_container(m, 'a', "v");
		while (sd_bus_message_enter_container(m, 'v', "(ia{sv}av)") > 0) {
			if (visible)
				readmenu(m, id == 0 ? prefix : sub, out, n, max);
			else
				sd_bus_message_skip(m, "(ia{sv}av)");
			sd_bus_message_exit_container(m);
			children = 1;
		}
		sd_bus_message_exit_container(m);
		if (id != 0 && !children && visible && enabled && strcmp(type, "separator") && *label && *n < max) {
			const char *mark = "";
			if (*toggle)
				mark = state == 1 ? "[x] " : "[ ] ";
			out[*n].id = id;
			snprintf(out[*n].label, sizeof(out[*n].label), "%s%s%s", mark, prefix, label);
			(*n)++;
		}
	}
	return sd_bus_message_exit_container(m);
}

static int showmenu(const char *name, const char *path)
{
	sd_bus_message *reply = NULL;
	char *menu = NULL, tmp[] = "/tmp/tinyhat-menu-XXXXXX", cmd[700], out[32];
	Entry entries[200];
	int n = 0, fd, choice = -1;
	uint32_t rev;
	FILE *f;
	{
		sd_bus_message *v = NULL;
		const char *o;
		if (sd_bus_get_property(bus, name, path, SNI, "Menu", NULL, &v, "o") < 0 || sd_bus_message_read(v, "o", &o) < 0) {
			sd_bus_message_unref(v);
			return -1;
		}
		menu = strdup(o);
		sd_bus_message_unref(v);
	}
	if (!menu || !strcmp(menu, "/")) {
		free(menu);
		return -1;
	}
	sd_bus_call_method(bus, name, menu, DBUSMENU, "AboutToShow", NULL, NULL, "i", 0);
	if (sd_bus_call_method(bus, name, menu, DBUSMENU, "GetLayout", NULL, &reply, "iias", 0, -1, 0) < 0 ||
			sd_bus_message_read(reply, "u", &rev) < 0) {
		free(menu);
		sd_bus_message_unref(reply);
		return -1;
	}
	readmenu(reply, "", entries, &n, 200);
	sd_bus_message_unref(reply);
	if (n == 0 || (fd = mkstemp(tmp)) < 0) {
		free(menu);
		return n == 0 ? 0 : -1;
	}
	if ((f = fdopen(fd, "w"))) {
		for (int i = 0; i < n; i++)
			fprintf(f, "%s\n", entries[i].label);
		fclose(f);
	}
	snprintf(cmd, sizeof(cmd), "fuzzel --dmenu --index --prompt 'Menu ❯ ' <%s", tmp);
	if ((f = popen(cmd, "r"))) {
		if (fgets(out, sizeof(out), f))
			choice = atoi(out);
		pclose(f);
	}
	unlink(tmp);
	if (choice >= 0 && choice < n)
		sd_bus_call_method(bus, name, menu, DBUSMENU, "Event", NULL, NULL, "isvu", entries[choice].id, "clicked",
			"i", 0, (uint32_t)time(NULL));
	free(menu);
	return 0;
}

static int click(int argc, char **argv)
{
	const char *button, *name, *path;
	int x, y, ismenu = 0;
	if (argc < 7) {
		fputs("usage: tinyhat-notify click left|middle|right SERVICE PATH X Y\n", stderr);
		return 2;
	}
	button = argv[2];
	name = argv[3];
	path = argv[4];
	x = atoi(argv[5]);
	y = atoi(argv[6]);
	if (sd_bus_open_user(&bus) < 0)
		return 1;
	sd_bus_set_method_call_timeout(bus, 5000000);
	if (!strcmp(button, "middle"))
		return sd_bus_call_method(bus, name, path, SNI, "SecondaryActivate", NULL, NULL, "ii", x, y) < 0;
	if (!strcmp(button, "left")) {
		sd_bus_get_property_trivial(bus, name, path, SNI, "ItemIsMenu", NULL, 'b', &ismenu);
		if (!ismenu && sd_bus_call_method(bus, name, path, SNI, "Activate", NULL, NULL, "ii", x, y) >= 0)
			return 0;
	}
	if (showmenu(name, path) >= 0)
		return 0;
	return sd_bus_call_method(bus, name, path, SNI, "ContextMenu", NULL, NULL, "ii", x, y) < 0;
}

static int sendnotice(int argc, char **argv)
{
	sd_bus_message *reply = NULL;
	uint32_t id = 0;
	int32_t timeout = 0;
	if (argc < 3 || argc > 4) {
		fputs("usage: tinyhat-notify send TEXT [SECONDS]\n", stderr);
		return 2;
	}
	if (argc == 4)
		timeout = (int32_t)(atof(argv[3]) * 1000);
	if (sd_bus_open_user(&bus) < 0)
		return 1;
	sd_bus_set_method_call_timeout(bus, 2000000);
	if (sd_bus_call_method(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
			"org.freedesktop.Notifications", "Notify", NULL, &reply, "susssasa{sv}i", "tinyhat-notice", 0, "",
			argv[2], "", 0, 0, timeout) < 0 || sd_bus_message_read(reply, "u", &id) < 0) {
		sd_bus_message_unref(reply);
		return 1;
	}
	sd_bus_message_unref(reply);
	printf("%u\n", id);
	return 0;
}

static int closeid(int argc, char **argv)
{
	if (argc != 3) {
		fputs("usage: tinyhat-notify close ID\n", stderr);
		return 2;
	}
	if (sd_bus_open_user(&bus) < 0)
		return 1;
	return sd_bus_call_method(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
		"org.freedesktop.Notifications", "CloseNotification", NULL, NULL, "u", (uint32_t)strtoul(argv[2], NULL, 10)) < 0;
}

int main(int argc, char **argv)
{
	const char *rd = getenv("XDG_RUNTIME_DIR");
	int notifications, watcher;
	snprintf(rundir, sizeof(rundir), "%s", rd && *rd ? rd : "/tmp");
	if (argc > 1 && !strcmp(argv[1], "click"))
		return click(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "send"))
		return sendnotice(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "close"))
		return closeid(argc, argv);
	if (argc > 1) {
		fputs("usage: tinyhat-notify [send TEXT [SECONDS] | close ID | click left|middle|right SERVICE PATH X Y]\n", stderr);
		return 2;
	}
	if (sd_bus_open_user(&bus) < 0 || sd_event_default(&event) < 0) {
		fputs("tinyhat-notify: no D-Bus session bus\n", stderr);
		return 1;
	}
	sd_bus_set_method_call_timeout(bus, 2000000);
	{
		char dir[600];
		snprintf(dir, sizeof(dir), "%s/tinyhat-tray", rundir);
		mkdir(dir, 0700);
	}
	sd_bus_add_object_vtable(bus, NULL, "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
		notify_vtable, NULL);
	sd_bus_add_object_vtable(bus, NULL, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", watcher_vtable,
		NULL);
	sd_bus_add_match(bus, NULL,
		"type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged'",
		ownerchanged, NULL);
	notifications = sd_bus_request_name(bus, "org.freedesktop.Notifications", 0) >= 0;
	watcher = sd_bus_request_name(bus, "org.kde.StatusNotifierWatcher", 0) >= 0;
	if (!notifications && !watcher) {
		fputs("tinyhat-notify: another notification service and tray are running\n", stderr);
		return 1;
	}
	if (watcher) {
		char host[64];
		snprintf(host, sizeof(host), "org.kde.StatusNotifierHost-%d", (int)getpid());
		sd_bus_request_name(bus, host, 0);
		bar("tray\n");
		sd_bus_emit_signal(bus, "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
			"StatusNotifierHostRegistered", "");
	}
	sd_bus_attach_event(bus, event, SD_EVENT_PRIORITY_NORMAL);
	return sd_event_loop(event) < 0;
}
