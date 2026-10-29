#include <gtk/gtk.h>
#include <stdio.h>

static int status = 1;

static void opened(GObject *source, GAsyncResult *result, gpointer data)
{
	GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, NULL);
	if (file != NULL) {
		char *filename = g_file_get_path(file);
		if (filename != NULL) {
			puts(filename);
			g_free(filename);
			status = 0;
		}
		g_object_unref(file);
	}
	g_main_loop_quit(data);
}

static int pick(const char *title, const char *folder)
{
	GtkFileDialog *dialog = gtk_file_dialog_new();
	gtk_file_dialog_set_title(dialog, title);
	if (folder != NULL && folder[0] != '\0') {
		GFile *dir = g_file_new_for_path(folder);
		gtk_file_dialog_set_initial_folder(dialog, dir);
		g_object_unref(dir);
	}
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	gtk_file_dialog_open(dialog, NULL, NULL, opened, loop);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	g_object_unref(dialog);
	return status;
}

int main(int argc, char **argv)
{
	gtk_init();
	const char *title = argc > 1 ? argv[1] : "Open...";
	const char *folder = argc > 2 ? argv[2] : NULL;
	return pick(title, folder);
}
