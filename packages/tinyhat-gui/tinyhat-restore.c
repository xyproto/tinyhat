#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int run_tar(const char *archive)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("tar", "tar", "-xPzf", archive, "-C", "/home", (char *)NULL);
		_exit(127);
	}
	int status = 1;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void on_restore(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	GtkWidget *chooser = gtk_file_chooser_dialog_new("Restore from...",
		GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Restore", GTK_RESPONSE_ACCEPT, NULL);
	GtkFileFilter *filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Back ups (*.tar.gz)");
	gtk_file_filter_add_pattern(filter, "*.tar.gz");
	gtk_file_filter_add_pattern(filter, "*.tgz");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(chooser);
		return;
	}
	char *archive = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
	gtk_widget_destroy(chooser);
	char question[512];
	snprintf(question, sizeof(question),
		"This will replace files in /home/tinyhat with the contents of\n%s. Continue?", archive);
	GtkWidget *c = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_MODAL,
		GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", question);
	int answer = gtk_dialog_run(GTK_DIALOG(c));
	gtk_widget_destroy(c);
	if (answer != GTK_RESPONSE_YES) {
		g_free(archive);
		return;
	}
	if (run_tar(archive) == 0)
		dialog("Restore", "The restore is complete.", GTK_MESSAGE_INFO);
	else
		dialog("Restore", "The restore failed.", GTK_MESSAGE_ERROR);
	g_free(archive);
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Restore my files");
	gtk_window_set_default_size(GTK_WINDOW(win), 420, 150);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	gtk_container_add(GTK_CONTAINER(win), box);
	GtkWidget *label = gtk_label_new("Bring back your files from\na back up archive.");
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
	GtkWidget *button = gtk_button_new_with_label("Restore...");
	g_signal_connect(button, "clicked", G_CALLBACK(on_restore), NULL);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}
