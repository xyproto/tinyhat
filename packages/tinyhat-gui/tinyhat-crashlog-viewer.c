#include <gtk/gtk.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Shows the newest kernel crash log that /usr/lib/tinyhat/crashlogs collected at boot. "OK" removes it, so the next
// boot shows the next one (if any), and "Never show again" also turns the viewer off for this user
#define LOGDIR "/var/lib/tinyhat/crashlogs"

static GtkWidget *win;
static char path[512];

static int newest(void)
{
	DIR *d = opendir(LOGDIR);
	if (!d)
		return 0;
	time_t best = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (e->d_name[0] == '.' || n < 5 || strcmp(e->d_name + n - 4, ".txt"))
			continue;
		char p[512];
		struct stat st;
		snprintf(p, sizeof(p), LOGDIR "/%s", e->d_name);
		if (stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && (!*path || st.st_mtime >= best)) {
			best = st.st_mtime;
			snprintf(path, sizeof(path), "%s", p);
		}
	}
	closedir(d);
	return *path != 0;
}

static char *never_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "tinyhat-crashlog-viewer", "never", NULL);
}

static void dismiss(void)
{
	if (unlink(path) != 0)
		perror(path);
	gtk_window_destroy(GTK_WINDOW(win));
}

static void on_copy(GtkWidget *b, gpointer text)
{
	gdk_clipboard_set_text(gtk_widget_get_clipboard(b), text);
	gtk_button_set_label(GTK_BUTTON(b), "Copied");
}

static void on_never(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	char *never = never_path();
	char *dir = g_path_get_dirname(never);
	g_mkdir_with_parents(dir, 0755);
	if (!g_file_set_contents(never, "Remove this file to see kernel crash logs after booting again.\n", -1, NULL))
		perror(never);
	g_free(dir);
	g_free(never);
	dismiss();
}

static void on_ok(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	dismiss();
}

static void on_destroy(GtkWidget *w, gpointer loop)
{
	(void)w;
	g_main_loop_quit(loop);
}

static GtkWidget *button(GtkWidget *row, const char *label, GCallback cb, gpointer data)
{
	GtkWidget *b = gtk_button_new_with_mnemonic(label);
	g_signal_connect(b, "clicked", cb, data);
	gtk_box_append(GTK_BOX(row), b);
	return b;
}

int main(void)
{
	char *never = never_path();
	int off = g_file_test(never, G_FILE_TEST_EXISTS);
	g_free(never);
	char *text = NULL;
	gsize len;
	if (off || !newest() || !g_file_get_contents(path, &text, &len, NULL))
		return 0;
	if (!g_utf8_validate(text, len, NULL)) {
		char *valid = g_utf8_make_valid(text, len);
		g_free(text);
		text = valid;
	}

	gtk_init();
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	win = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(win), "Kernel crash log");
	gtk_window_set_default_size(GTK_WINDOW(win), 760, 480);
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), loop);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);

	GtkWidget *label = gtk_label_new("The Linux kernel crashed before an earlier reboot. These are its last messages.");
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_box_append(GTK_BOX(box), label);

	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	gtk_text_buffer_set_text(buf, text, -1);
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(box), scroll);

	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(row, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(box), row);
	button(row, "_Copy text", G_CALLBACK(on_copy), text);
	button(row, "_Never show again", G_CALLBACK(on_never), NULL);
	GtkWidget *ok = button(row, "_OK", G_CALLBACK(on_ok), NULL);
	gtk_widget_add_css_class(ok, "suggested-action");
	gtk_window_set_default_widget(GTK_WINDOW(win), ok);

	gtk_window_present(GTK_WINDOW(win));
	// The end of the log has the crash itself
	GtkTextIter end;
	gtk_text_buffer_get_end_iter(buf, &end);
	gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(view), gtk_text_buffer_create_mark(buf, NULL, &end, FALSE), 0, FALSE, 0, 1);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	g_free(text);
	return 0;
}
