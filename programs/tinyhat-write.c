#include "tinyhat-write-core.h"

#include <ctype.h>
#include <dirent.h>
#include <gtk/gtk.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#endif

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
static GtkWidget *gview;
static GtkListStore *gstore;
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
	if (IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching("IOMedia"), &it) != KERN_SUCCESS)
		return 0;
	while ((obj = IOIteratorNext(it)) && n < max) {
		CFMutableDictionaryRef props = NULL;
		CFBooleanRef whole, internal, wr;
		CFNumberRef size;
		CFStringRef bsd;
		char name[64];
		long long sz = 0;
		IORegistryEntryCreateCFProperties(obj, &props, kCFAllocatorDefault, 0);
		if (!props)
			continue;
		whole = CFDictionaryGetValue(props, CFSTR("Whole"));
		internal = CFDictionaryGetValue(props, CFSTR("Internal"));
		wr = CFDictionaryGetValue(props, CFSTR("Writable"));
		size = CFDictionaryGetValue(props, CFSTR("Size"));
		bsd = CFDictionaryGetValue(props, CFSTR("BSD Name"));
		if (whole && CFBooleanGetValue(whole) && wr && CFBooleanGetValue(wr) &&
		    !(internal && CFBooleanGetValue(internal)) && bsd) {
			CFStringGetCString(bsd, name, sizeof(name), kCFStringEncodingASCII);
			if (size)
				CFNumberGetValue(size, kCFNumberLongLongType, &sz);
			snprintf(out[n].dev, sizeof(out[n].dev), "/dev/rdisk%s", name + 4);
			snprintf(out[n].name, sizeof(out[n].name), "Disk %s", name + 4);
			out[n].size = (uint64_t)sz;
			n++;
		}
		CFRelease(props);
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
	while (gtk_events_pending())
		gtk_main_iteration();
}

static void tw_error_dialog(const char *text)
{
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(gwin),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", text);
	gtk_window_set_title(GTK_WINDOW(d), "Tiny Hat USB writer");
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

static void tw_done_state(const char *msg)
{
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
	char sz[32];
	GtkTreeIter iter;
	gtk_list_store_clear(gstore);
	gndrives = tw_scan(gdrives, TW_MAX_DRIVES);
	for (i = 0; i < gndrives; i++) {
		gtk_list_store_append(gstore, &iter);
		gtk_list_store_set(gstore, &iter, 0, gdrives[i].dev, 1, gdrives[i].name,
				   2, tw_fmt_size(gdrives[i].size, sz, sizeof(sz)), -1);
	}
	gtk_widget_set_sensitive(gwrite, gndrives > 0);
	tw_set_status(gndrives ? "Select a USB drive" : "No USB drives found");
}

static int tw_selected(void)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gview));
	GtkTreeIter iter;
	GtkTreePath *path;
	int idx = -1;
	if (!gtk_tree_selection_get_selected(sel, NULL, &iter))
		return -1;
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(gstore), &iter);
	if (path) {
		idx = gtk_tree_path_get_indices(path)[0];
		gtk_tree_path_free(path);
	}
	return idx;
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
		GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(gwin),
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", line + 8);
		gint r;
		gtk_window_set_title(GTK_WINDOW(d), "Overwrite?");
		r = gtk_dialog_run(GTK_DIALOG(d));
		gtk_widget_destroy(d);
		tw_answer(r == GTK_RESPONSE_YES ? "YES\n" : "NO\n");
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
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(gwin),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", text);
	gint r;
	gtk_window_set_title(GTK_WINDOW(d), "Overwrite?");
	r = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	return r == GTK_RESPONSE_YES;
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
	GtkWidget *box, *scroll, *hbox;
	GtkCellRenderer *rend;
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
	gtk_init(&argc, &argv);
	gwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(gwin), "Tiny Hat USB writer");
	gtk_window_set_default_size(GTK_WINDOW(gwin), 540, 430);
	gtk_container_set_border_width(GTK_CONTAINER(gwin), 10);
	g_signal_connect(gwin, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(gwin), box);
	gtk_box_pack_start(GTK_BOX(box),
		gtk_label_new("Select the USB drive to write Tiny Hat " TW_VERSION " to. Everything on that drive gets erased."),
		FALSE, FALSE, 0);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	gstore = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	gview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gstore));
	rend = gtk_cell_renderer_text_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(gview),
		gtk_tree_view_column_new_with_attributes("Device", rend, "text", 0, NULL));
	gtk_tree_view_append_column(GTK_TREE_VIEW(gview),
		gtk_tree_view_column_new_with_attributes("Drive", rend, "text", 1, NULL));
	gtk_tree_view_append_column(GTK_TREE_VIEW(gview),
		gtk_tree_view_column_new_with_attributes("Size", rend, "text", 2, NULL));
	gtk_container_add(GTK_CONTAINER(scroll), gview);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
	gwrite = gtk_button_new_with_label("Write Tiny Hat to this drive");
	grefresh = gtk_button_new_with_label("Refresh");
	gtk_box_pack_start(GTK_BOX(hbox), gwrite, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), grefresh, FALSE, FALSE, 0);
	g_signal_connect(gwrite, "clicked", G_CALLBACK(tw_on_write), NULL);
	g_signal_connect(grefresh, "clicked", G_CALLBACK(tw_on_refresh), NULL);
	gprog = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(box), gprog, FALSE, FALSE, 0);
	gstatus = gtk_label_new("");
	gtk_widget_set_halign(gstatus, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), gstatus, FALSE, FALSE, 0);
	tw_refresh();
	gtk_widget_show_all(gwin);
	gtk_main();
	return 0;
}
