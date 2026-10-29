#include "tinyhat-write-core.h"

#include <ctype.h>
#include <dirent.h>
#include <gtk/gtk.h>
#include <signal.h>


#ifndef TW_VERSION
#define TW_VERSION "0.0.0"
#endif

#define TW_MAX_DRIVES 32

typedef struct {
	char dev[128];
	char name[128];
	uint64_t size;
} tw_drive;

static GtkWidget *gwin;
static GtkStringList *gstore;
static GtkSingleSelection *gsel;
static GMainLoop *gloop;
static GtkWidget *gwrite;
static GtkWidget *grefresh;
static GtkWidget *gprog;
static GtkWidget *gstatus;
static tw_drive gdrives[TW_MAX_DRIVES];
static int gndrives;
static GPid gpid;
static int gin = -1, gout = -1, gerrfd = -1;
static GIOChannel *gchan;
static GIOChannel *gerrchan;
static char gline[1024];
static size_t glinelen;
static uint64_t gtotal;
static int grunning;
static int gfinished;
static char gtmdir[256];
static char gstderr[2048];
static size_t gerrlen;
static char gspawnerr[512];
static guint gwatch;
static int gsawline;

static char *tw_fmt_size(uint64_t n, char *buf, size_t cap)
{
	if (n >= 1000000000ull)
		snprintf(buf, cap, "%.1f GB", (double)n / 1000000000.0);
	else if (n >= 1000000ull)
		snprintf(buf, cap, "%.1f MB", (double)n / 1000000.0);
	else
		snprintf(buf, cap, "%.1f kB", (double)n / 1000.0);
	return buf;
}

static int tw_read_sys(const char *path, char *buf, size_t cap)
{
	FILE *f = fopen(path, "r");
	size_t n;
	if (!f)
		return -1;
	n = fread(buf, 1, cap - 1, f);
	fclose(f);
	buf[n] = 0;
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = 0;
	return 0;
}

static int tw_scan(tw_drive *out, int max)
{
	int n = 0;
#ifdef __APPLE__
	io_iterator_t it;
	io_object_t obj;
	if (IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching("IOMedia"), &it) != KERN_SUCCESS)
		return 0;
	while ((obj = IOIteratorNext(it))) {
		CFTypeRef bsd = IORegistryEntryCreateCFProperty(obj, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
		char name[64];
		if (bsd && n < max && CFGetTypeID(bsd) == CFStringGetTypeID() &&
		    CFStringGetCString((CFStringRef)bsd, name, sizeof(name), kCFStringEncodingASCII) &&
		    tw_mac_external(name, out[n].name, sizeof(out[n].name), &out[n].size)) {
			snprintf(out[n].dev, sizeof(out[n].dev), "/dev/r%s", name);
			n++;
		}
		if (bsd)
			CFRelease(bsd);
		IOObjectRelease(obj);
	}
	IOObjectRelease(it);
#else
	DIR *d = opendir("/sys/block");
	struct dirent *e;
	if (!d)
		return 0;
	while ((e = readdir(d)) && n < max) {
		char p[512], buf[4096];
		int usb = 0, removable = 0;
		ssize_t ln;
		unsigned long long sectors = 0;
		if (strncmp(e->d_name, "sd", 2) && strncmp(e->d_name, "mmcblk", 6))
			continue;
		snprintf(p, sizeof(p), "/sys/block/%s/removable", e->d_name);
		if (tw_read_sys(p, buf, sizeof(buf)) == 0 && buf[0] == '1')
			removable = 1;
		snprintf(p, sizeof(p), "/sys/block/%s/device", e->d_name);
		ln = readlink(p, buf, sizeof(buf) - 1);
		if (ln > 0) {
			buf[ln] = 0;
			if (strstr(buf, "/usb"))
				usb = 1;
		}
		if (!usb && !removable)
			continue;
		snprintf(p, sizeof(p), "/sys/block/%s/size", e->d_name);
		if (tw_read_sys(p, buf, sizeof(buf)) == 0)
			sectors = strtoull(buf, NULL, 10);
		snprintf(p, sizeof(p), "/sys/block/%s/device/model", e->d_name);
		if (tw_read_sys(p, buf, sizeof(buf)) < 0)
			snprintf(buf, sizeof(buf), "drive");
		snprintf(out[n].dev, sizeof(out[n].dev), "/dev/%s", e->d_name);
		snprintf(out[n].name, sizeof(out[n].name), "%s", buf);
		out[n].size = sectors * 512;
		n++;
	}
	closedir(d);
#endif
	{
		const char *tt = getenv("TW_TEST_TARGET");
		if (tt && n < max) {
			snprintf(out[n].dev, sizeof(out[n].dev), "%s", tt);
			snprintf(out[n].name, sizeof(out[n].name), "test target");
			out[n].size = 0;
			n++;
		}
	}
	return n;
}

static void tw_set_status(const char *s)
{
	gtk_label_set_text(GTK_LABEL(gstatus), s);
}

static void tw_pump(void)
{
	while (g_main_context_pending(NULL))
		g_main_context_iteration(NULL, FALSE);
}

static void tw_error_dialog(const char *text)
{
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", text);
	gtk_alert_dialog_show(d, GTK_WINDOW(gwin));
	g_object_unref(d);
}

typedef struct {
	GMainLoop *loop;
	int answer;
} tw_wait;

static void tw_on_choice(GObject *src, GAsyncResult *res, gpointer data)
{
	tw_wait *w = data;
	w->answer = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL);
	g_main_loop_quit(w->loop);
}

static int tw_ask(const char *text)
{
	static const char *buttons[] = { "_No", "_Yes", NULL };
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", text);
	tw_wait w;
	gtk_alert_dialog_set_buttons(d, buttons);
	gtk_alert_dialog_set_cancel_button(d, 0);
	gtk_alert_dialog_set_default_button(d, 0);
	w.loop = g_main_loop_new(NULL, FALSE);
	w.answer = -1;
	gtk_alert_dialog_choose(d, GTK_WINDOW(gwin), NULL, tw_on_choice, &w);
	g_main_loop_run(w.loop);
	g_main_loop_unref(w.loop);
	g_object_unref(d);
	return w.answer == 1;
}

static void tw_done_state(const char *msg);

static gboolean tw_on_watchdog(gpointer data)
{
	(void)data;
	gwatch = 0;
	if (grunning && !gsawline && !gfinished) {
		if (!tw_ask("No authentication prompt has appeared. Keep waiting?")) {
			if (gpid)
				kill(gpid, SIGKILL);
			tw_error_dialog("Canceled. To write without a password prompt, start this program with sudo.");
			tw_done_state("Canceled");
		}
	}
	return FALSE;
}

static void tw_done_state(const char *msg)
{
	if (gwatch) {
		g_source_remove(gwatch);
		gwatch = 0;
	}
	tw_set_status(msg);
	gtk_widget_set_sensitive(gwrite, TRUE);
	gtk_widget_set_sensitive(grefresh, TRUE);
	grunning = 0;
	if (gin >= 0) {
		close(gin);
		gin = -1;
	}
	if (gtmdir[0]) {
		char p[300];
		snprintf(p, sizeof(p), "%s/in", gtmdir);
		unlink(p);
		snprintf(p, sizeof(p), "%s/out", gtmdir);
		unlink(p);
		rmdir(gtmdir);
		gtmdir[0] = 0;
	}
}

static void tw_refresh(void)
{
	int i;
	char num[16];
	gtk_string_list_splice(gstore, 0, g_list_model_get_n_items(G_LIST_MODEL(gstore)), NULL);
	gndrives = tw_scan(gdrives, TW_MAX_DRIVES);
	for (i = 0; i < gndrives; i++) {
		snprintf(num, sizeof(num), "%d", i);
		gtk_string_list_append(gstore, num);
	}
	gtk_widget_set_sensitive(gwrite, gndrives > 0);
	tw_set_status(gndrives ? "Select a USB drive" : "No USB drives found");
}

static int tw_selected(void)
{
	guint pos = gtk_single_selection_get_selected(gsel);
	return pos == GTK_INVALID_LIST_POSITION ? -1 : (int)pos;
}

static void tw_on_setup(GtkSignalListItemFactory *f, GtkListItem *item, gpointer col)
{
	GtkWidget *label = gtk_label_new(NULL);
	(void)f;
	(void)col;
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_list_item_set_child(item, label);
}

static void tw_on_bind(GtkSignalListItemFactory *f, GtkListItem *item, gpointer col)
{
	int i = atoi(gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_list_item_get_item(item))));
	char sz[32];
	const char *text = "";
	(void)f;
	if (i >= 0 && i < gndrives) {
		switch (GPOINTER_TO_INT(col)) {
		case 0: text = gdrives[i].dev; break;
		case 1: text = gdrives[i].name; break;
		default: text = tw_fmt_size(gdrives[i].size, sz, sizeof(sz)); break;
		}
	}
	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), text);
}

static void tw_add_column(GtkWidget *view, const char *title, int col)
{
	GtkListItemFactory *f = gtk_signal_list_item_factory_new();
	GtkColumnViewColumn *c;
	g_signal_connect(f, "setup", G_CALLBACK(tw_on_setup), GINT_TO_POINTER(col));
	g_signal_connect(f, "bind", G_CALLBACK(tw_on_bind), GINT_TO_POINTER(col));
	c = gtk_column_view_column_new(title, f);
	gtk_column_view_column_set_expand(c, TRUE);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(view), c);
	g_object_unref(c);
}

static void tw_shell_quote(char *out, size_t cap, const char *s)
{
	size_t o = 0;
	out[o++] = '\'';
	while (*s && o + 4 < cap) {
		if (*s == '\'') {
			out[o++] = '\'';
			out[o++] = '\\';
			out[o++] = '\'';
			out[o++] = '\'';
			s++;
			continue;
		}
		out[o++] = *s++;
	}
	out[o++] = '\'';
	out[o] = 0;
}

static void tw_answer(const char *a)
{
	if (gin >= 0)
		write(gin, a, strlen(a));
}

static gboolean tw_on_line(const char *line);

static void tw_failed_state(void)
{
	char msg[2400];
	if (gerrlen) {
		char *p = gstderr;
		while (*p) {
			if (*p == '\n' || *p == '\r')
				*p = ' ';
			p++;
		}
		snprintf(msg, sizeof(msg), "Writing failed: %s", gstderr);
	} else {
		snprintf(msg, sizeof(msg),
			 "Writing failed. Root access is required: try running this program with sudo.");
	}
	tw_error_dialog(msg);
	tw_done_state(msg);
}

static gboolean tw_on_err_io(GIOChannel *src, GIOCondition cond, gpointer data)
{
	char buf[512];
	gsize got = 0;
	(void)cond;
	(void)data;
	g_io_channel_read_chars(src, buf, sizeof(buf), &got, NULL);
	if (got && gerrlen < sizeof(gstderr) - 1) {
		size_t room = sizeof(gstderr) - 1 - gerrlen;
		if (got > room)
			got = room;
		memcpy(gstderr + gerrlen, buf, got);
		gerrlen += got;
		gstderr[gerrlen] = 0;
	}
	return TRUE;
}

static gboolean tw_on_io(GIOChannel *src, GIOCondition cond, gpointer data)
{
	char buf[512];
	gsize got = 0;
	GIOStatus st;
	(void)data;
	(void)cond;
	st = g_io_channel_read_chars(src, buf, sizeof(buf), &got, NULL);
	if (got) {
		gsize i;
		for (i = 0; i < got; i++) {
			if (buf[i] == '\n' || glinelen == sizeof(gline) - 1) {
				gline[glinelen] = 0;
				if (glinelen)
					tw_on_line(gline);
				glinelen = 0;
			} else {
				gline[glinelen++] = buf[i];
			}
		}
	}
	if (st == G_IO_STATUS_EOF || st == G_IO_STATUS_ERROR) {
		if (grunning && !gfinished)
			tw_failed_state();
		gchan = NULL;
		return FALSE;
	}
	return TRUE;
}

static gboolean tw_on_line(const char *line)
{
	gsawline = 1;
	if (!strncmp(line, "SIZE ", 5)) {
		gtotal = strtoull(line + 5, NULL, 10);
	} else if (!strncmp(line, "PROGRESS ", 9)) {
		uint64_t done = strtoull(line + 9, NULL, 10);
		if (gtotal) {
			char pct[16];
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gprog),
						      (double)done / (double)gtotal);
			snprintf(pct, sizeof(pct), "%d%%", (int)(done * 100 / gtotal));
			gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(gprog), TRUE);
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(gprog), pct);
		} else {
			gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gprog));
		}
	} else if (!strncmp(line, "CONFIRM ", 8)) {
		tw_answer(tw_ask(line + 8) ? "YES\n" : "NO\n");
	} else if (!strcmp(line, "DONE")) {
		gfinished = 1;
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gprog), 1.0);
		tw_done_state("Finished. The drive is ready.");
	} else if (!strncmp(line, "ERROR ", 6)) {
		gfinished = 1;
		tw_error_dialog(line + 6);
		tw_done_state(line + 6);
	}
	return TRUE;
}

static void tw_on_child(GPid pid, gint status, gpointer data)
{
	(void)status;
	(void)data;
	g_spawn_close_pid(pid);
	if (grunning && !gfinished)
		tw_failed_state();
}

static int tw_gui_ask(void *ud, const char *text)
{
	(void)ud;
	return tw_ask(text);
}

static void tw_gui_note(void *ud, const char *line)
{
	(void)ud;
	tw_pump();
	tw_on_line(line);
	tw_pump();
}

static int tw_start_flow(const char *self, const char *target)
{
#ifdef __APPLE__
	static const char *script =
		"on run argv\ndo shell script (item 1 of argv) with administrator privileges\nend run";
	char quoted[4200], qtarget[300], cmd[5200], infifo[300], outfifo[300];
	char *osargv[6];
	snprintf(gtmdir, sizeof(gtmdir), "/tmp/tinyhat-write.XXXXXXX");
	if (!mkdtemp(gtmdir))
		return -1;
	snprintf(infifo, sizeof(infifo), "%s/in", gtmdir);
	snprintf(outfifo, sizeof(outfifo), "%s/out", gtmdir);
	if (mkfifo(infifo, 0600) < 0 || mkfifo(outfifo, 0600) < 0)
		return -1;
	tw_shell_quote(quoted, sizeof(quoted), self);
	tw_shell_quote(qtarget, sizeof(qtarget), target);
	snprintf(cmd, sizeof(cmd), "%s --flow %s <%s >%s 2>&1", quoted, qtarget, infifo, outfifo);
	osargv[0] = "osascript";
	osargv[1] = "-e";
	osargv[2] = (char *)script;
	osargv[3] = "--";
	osargv[4] = cmd;
	osargv[5] = NULL;
	if (!g_spawn_async(NULL, osargv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
			   NULL, NULL, &gpid, NULL))
		return -1;
	gout = open(outfifo, O_RDONLY | O_NONBLOCK);
	gin = open(infifo, O_RDWR | O_NONBLOCK);
	if (gout < 0 || gin < 0)
		return -1;
	return 0;
#else
	char *prog = g_find_program_in_path("pkexec");
	char **pargv;
	GError *ge = NULL;
	int ok;
	if (!prog)
		prog = g_find_program_in_path("doas");
	if (!prog) {
		snprintf(gspawnerr, sizeof(gspawnerr),
			 "Writing to a drive needs root access. Install pkexec or doas, or start this program with sudo.");
		return -1;
	}
	pargv = g_new0(char *, 5);
	pargv[0] = prog;
	pargv[1] = (char *)self;
	pargv[2] = "--flow";
	pargv[3] = (char *)target;
	ok = g_spawn_async_with_pipes(NULL, pargv, NULL,
				      G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
				      NULL, NULL, &gpid, &gin, &gout, &gerrfd, &ge);
	if (!ok && ge) {
		snprintf(gspawnerr, sizeof(gspawnerr), "%s", ge->message);
		g_error_free(ge);
	}
	g_free(pargv);
	g_free(prog);
	return ok ? 0 : -1;
#endif
}

static void tw_start_write(void)
{
	int idx = tw_selected();
	char self[4096];
	if (idx < 0 || idx >= gndrives || grunning)
		return;
	gtotal = 0;
	glinelen = 0;
	gerrlen = 0;
	gfinished = 0;
	gsawline = 0;
	gspawnerr[0] = 0;
	gstderr[0] = 0;
	gerrfd = -1;
	if (geteuid() == 0) {
		tw_image img;
		tw_hooks hk;
		if (tw_load_image(NULL, &img) < 0) {
			tw_error_dialog("No tinyhat.img.gz found");
			return;
		}
		grunning = 1;
		gtk_widget_set_sensitive(gwrite, FALSE);
		gtk_widget_set_sensitive(grefresh, FALSE);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gprog), 0.0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(gprog), NULL);
		tw_set_status("Writing, this takes a few minutes");
		hk.ask = tw_gui_ask;
		hk.note = tw_gui_note;
		hk.ud = NULL;
		tw_do_write(&img, gdrives[idx].dev, &hk);
		if (grunning)
			tw_done_state("Finished. The drive is ready.");
		return;
	}
	if (tw_exe_path(self, sizeof(self)) < 0) {
		tw_error_dialog("Cannot locate the program");
		return;
	}
	if (tw_start_flow(self, gdrives[idx].dev) < 0) {
		tw_error_dialog(gspawnerr[0] ? gspawnerr : "Could not start the writer");
		return;
	}
	grunning = 1;
	gtk_widget_set_sensitive(gwrite, FALSE);
	gtk_widget_set_sensitive(grefresh, FALSE);
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gprog), 0.0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(gprog), NULL);
	tw_set_status("Writing, this takes a few minutes");
	gchan = g_io_channel_unix_new(gout);
	g_io_channel_set_encoding(gchan, NULL, NULL);
	g_io_channel_set_close_on_unref(gchan, TRUE);
	g_io_add_watch(gchan, G_IO_IN | G_IO_HUP | G_IO_ERR, tw_on_io, NULL);
	gwatch = g_timeout_add_seconds(10, tw_on_watchdog, NULL);
	if (gerrfd >= 0) {
		gerrchan = g_io_channel_unix_new(gerrfd);
		g_io_channel_set_encoding(gerrchan, NULL, NULL);
		g_io_channel_set_close_on_unref(gerrchan, TRUE);
		g_io_add_watch(gerrchan, G_IO_IN | G_IO_HUP | G_IO_ERR, tw_on_err_io, NULL);
	}
	g_child_watch_add(gpid, tw_on_child, NULL);
}

static void tw_on_write(GtkWidget *b, gpointer d)
{
	(void)b;
	(void)d;
	tw_start_write();
}

static void tw_on_refresh(GtkWidget *b, gpointer d)
{
	(void)b;
	(void)d;
	tw_refresh();
}

static int tw_flow_ask(void *ud, const char *text)
{
	char buf[64];
	(void)ud;
	if (isatty(0)) {
		printf("%s [y/N] ", text);
	} else {
		printf("CONFIRM %s\n", text);
	}
	fflush(stdout);
	if (!fgets(buf, sizeof(buf), stdin))
		return 0;
	return buf[0] == 'y' || buf[0] == 'Y';
}

static void tw_flow_note(void *ud, const char *line)
{
	(void)ud;
	fputs(line, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	tw_image img;
	tw_hooks hk;
	GtkWidget *box, *scroll, *hbox, *view, *label;
	if (argc >= 2 && !strcmp(argv[1], "--version")) {
		printf("tinyhat-write %s\n", TW_VERSION);
		return 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "--flow")) {
		hk.ask = tw_flow_ask;
		hk.note = tw_flow_note;
		hk.ud = NULL;
		if (tw_load_image(argc >= 4 ? argv[3] : NULL, &img) < 0) {
			printf("ERROR no tinyhat.img.gz found\n");
			return 1;
		}
		return tw_do_write(&img, argv[2], &hk);
	}
	gtk_init();
	gloop = g_main_loop_new(NULL, FALSE);
	gwin = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(gwin), "Tiny Hat USB writer");
	gtk_window_set_default_size(GTK_WINDOW(gwin), 540, 430);
	g_signal_connect_swapped(gwin, "destroy", G_CALLBACK(g_main_loop_quit), gloop);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 10);
	gtk_widget_set_margin_bottom(box, 10);
	gtk_widget_set_margin_start(box, 10);
	gtk_widget_set_margin_end(box, 10);
	gtk_window_set_child(GTK_WINDOW(gwin), box);
	label = gtk_label_new("Select the USB drive to write Tiny Hat " TW_VERSION " to. Everything on that drive gets erased.");
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_box_append(GTK_BOX(box), label);
	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(box), scroll);
	gstore = gtk_string_list_new(NULL);
	gsel = gtk_single_selection_new(G_LIST_MODEL(gstore));
	gtk_single_selection_set_autoselect(gsel, FALSE);
	gtk_single_selection_set_can_unselect(gsel, TRUE);
	view = gtk_column_view_new(GTK_SELECTION_MODEL(gsel));
	tw_add_column(view, "Device", 0);
	tw_add_column(view, "Drive", 1);
	tw_add_column(view, "Size", 2);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(box), hbox);
	gwrite = gtk_button_new_with_label("Write Tiny Hat to this drive");
	grefresh = gtk_button_new_with_label("Refresh");
	gtk_widget_set_hexpand(gwrite, TRUE);
	gtk_box_append(GTK_BOX(hbox), gwrite);
	gtk_box_append(GTK_BOX(hbox), grefresh);
	g_signal_connect(gwrite, "clicked", G_CALLBACK(tw_on_write), NULL);
	g_signal_connect(grefresh, "clicked", G_CALLBACK(tw_on_refresh), NULL);
	gprog = gtk_progress_bar_new();
	gtk_box_append(GTK_BOX(box), gprog);
	gstatus = gtk_label_new("");
	gtk_widget_set_halign(gstatus, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(box), gstatus);
	tw_refresh();
	gtk_window_present(GTK_WINDOW(gwin));
	g_main_loop_run(gloop);
	return 0;
}
