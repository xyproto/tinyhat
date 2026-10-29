#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static int run_tar(const char *dest)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("tar", "tar", "-czf", dest, "-C", "/home", "tinyhat", (char *)NULL);
		_exit(127);
	}
	int status;
	if (pid < 0 || waitpid(pid, &status, 0) < 0)
		return 1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void on_answer(GObject *source, GAsyncResult *result, gpointer data)
{
	char *dest = data;
	if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, NULL) == 1) {
		if (run_tar(dest) == 0)
			dialog("Back up", "The back up is complete.");
		else
			dialog("Back up", "The back up failed.");
	}
	g_free(dest);
}

static void on_saved(GObject *source, GAsyncResult *result, gpointer data)
{
	(void)data;
	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, NULL);
	if (!file)
		return;
	char *dest = g_file_get_path(file);
	g_object_unref(file);
	if (!dest)
		return;
	char *home = getenv("HOME");
	char question[512];
	snprintf(question, sizeof(question), "Back up %s to\n%s?",
		home ? home : "/home/tinyhat", dest);
	GtkAlertDialog *c = gtk_alert_dialog_new("%s", question);
	const char *buttons[] = { "_No", "_Yes", NULL };
	gtk_alert_dialog_set_buttons(c, buttons);
	gtk_alert_dialog_set_cancel_button(c, 0);
	gtk_alert_dialog_set_default_button(c, 1);
	gtk_alert_dialog_choose(c, GTK_WINDOW(win), NULL, on_answer, dest);
	g_object_unref(c);
}

static void on_backup(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	GtkFileDialog *chooser = gtk_file_dialog_new();
	gtk_file_dialog_set_title(chooser, "Back up to...");
	gtk_file_dialog_set_accept_label(chooser, "_Back up");
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char name[64];
	strftime(name, sizeof(name), "tinyhat-backup-%Y-%m-%d.tar.gz", &tm);
	gtk_file_dialog_set_initial_name(chooser, name);
	char *home = getenv("HOME");
	if (home) {
		GFile *dir = g_file_new_for_path(home);
		gtk_file_dialog_set_initial_folder(chooser, dir);
		g_object_unref(dir);
	}
	gtk_file_dialog_save(chooser, GTK_WINDOW(win), NULL, on_saved, NULL);
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
	gtk_window_set_title(GTK_WINDOW(win), "Back up home folder to another drive");
	gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);
	g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), loop);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);
	GtkWidget *label = gtk_label_new("Save a copy of your home folder\nto another drive or folder.");
	gtk_widget_set_vexpand(label, TRUE);
	gtk_box_append(GTK_BOX(box), label);
	GtkWidget *button = gtk_button_new_with_label("Back up...");
	g_signal_connect(button, "clicked", G_CALLBACK(on_backup), NULL);
	gtk_box_append(GTK_BOX(box), button);
	gtk_window_present(GTK_WINDOW(win));
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	return 0;
}
