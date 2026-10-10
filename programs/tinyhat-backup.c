#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

static GtkWidget *win;

static void dialog(const char *title, const char *text, GtkMessageType type)
{
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		type, GTK_BUTTONS_OK, "%s", text);
	gtk_window_set_title(GTK_WINDOW(d), title);
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

static int run_tar(const char *dest)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("tar", "tar", "-cPzf", dest, "-C", "/home", "tinyhat", (char *)NULL);
		_exit(127);
	}
	int status = 1;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void on_backup(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	GtkWidget *chooser = gtk_file_chooser_dialog_new("Back up to...",
		GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_SAVE,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Back up", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char name[64];
	strftime(name, sizeof(name), "tinyhat-backup-%Y-%m-%d.tar.gz", &tm);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), name);
	char *home = getenv("HOME");
	if (home)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), home);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(chooser);
		return;
	}
	char *dest = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
	gtk_widget_destroy(chooser);
	char question[512];
	snprintf(question, sizeof(question), "Back up %s to\n%s?",
		home ? home : "/home/tinyhat", dest);
	GtkWidget *c = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", question);
	int answer = gtk_dialog_run(GTK_DIALOG(c));
	gtk_widget_destroy(c);
	if (answer != GTK_RESPONSE_YES) {
		g_free(dest);
		return;
	}
	if (run_tar(dest) == 0)
		dialog("Back up", "The back up is complete.", GTK_MESSAGE_INFO);
	else
		dialog("Back up", "The back up failed.", GTK_MESSAGE_ERROR);
	g_free(dest);
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Back up home folder to another drive");
	gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(win), box);
	GtkWidget *label = gtk_label_new("Save a copy of your home folder\nto another drive or folder.");
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
	GtkWidget *button = gtk_button_new_with_label("Back up...");
	g_signal_connect(button, "clicked", G_CALLBACK(on_backup), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}
