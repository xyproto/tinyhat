#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static GtkWidget *win;

static void dialog(const char *title, const char *text)
{
	GtkAlertDialog *d = gtk_alert_dialog_new("%s", title);
	gtk_alert_dialog_set_detail(d, text);
	gtk_alert_dialog_show(d, GTK_WINDOW(win));
	g_object_unref(d);
}

static int run_tar(const char *archive)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("tar", "tar", "-xzf", archive, "-C", "/home", (char *)NULL);
		_exit(127);
	}
	int status;
	if (pid < 0 || waitpid(pid, &status, 0) < 0)
		return 1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void on_answer(GObject *source, GAsyncResult *result, gpointer data)
{
	char *archive = data;
	if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL) == 1) {
		if (run_tar(archive) == 0)
			dialog("Restore", "The restore is complete.");
		else
			dialog("Restore", "The restore failed.");
	}
	g_free(archive);
}

static void on_opened(GObject *source, GAsyncResult *result, gpointer data)
{
	(void)data;
	GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
	if (!file)
		return;
	char *archive = g_file_get_path(file);
	g_object_unref(file);
	if (!archive)
		return;
	char question[512];
	snprintf(question, sizeof(question),
		"This will replace files in /home/tinyhat with the contents of\n%s. Continue?", archive);
	GtkAlertDialog *c = gtk_alert_dialog_new("%s", question);
	const char *buttons[] = { "_No", "_Yes", NULL };
	gtk_alert_dialog_set_buttons(c, buttons);
	gtk_alert_dialog_set_cancel_button(c, 0);
	gtk_alert_dialog_set_default_button(c, 0);
	gtk_alert_dialog_choose(c, GTK_WINDOW(win), NULL, on_answer, archive);
	g_object_unref(c);
}

static void on_restore(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	GtkFileDialog *chooser = gtk_file_dialog_new();
	gtk_file_dialog_set_title(chooser, "Restore from...");
	gtk_file_dialog_set_accept_label(chooser, "_Restore");
	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Back ups (*.tar.gz)");
	gtk_file_filter_add_pattern(filter, "*.tar.gz");
	gtk_file_filter_add_pattern(filter, "*.tgz");
	GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
	g_list_store_append(filters, filter);
	gtk_file_dialog_set_filters(chooser, G_LIST_MODEL(filters));
	gtk_file_dialog_set_default_filter(chooser, filter);
	g_object_unref(filters);
	g_object_unref(filter);
	gtk_file_dialog_open(chooser, GTK_WINDOW(win), NULL, on_opened, NULL);
	g_object_unref(chooser);
}

static void on_destroy(GtkWidget *w, gpointer loop)
{
	(void)w;
	g_main_loop_quit(loop);
}

int main(void)
{
	gtk_init();
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	win = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(win), "Restore my files");
	gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), loop);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);
	GtkWidget *label = gtk_label_new("Bring back your files from\na back up archive.");
	gtk_widget_set_vexpand(label, TRUE);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget *button = gtk_button_new_with_label("Restore...");
	g_signal_connect(button, "clicked", G_CALLBACK(on_restore), NULL);
	gtk_box_append(GTK_BOX(box), button);
	gtk_window_present(GTK_WINDOW(win));
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	return 0;
}
