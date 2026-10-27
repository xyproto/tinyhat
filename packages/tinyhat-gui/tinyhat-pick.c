#include <gtk/gtk.h>
#include <stdio.h>

static int pick(const char *title, const char *folder)
{
	GtkWidget *chooser = gtk_file_chooser_dialog_new(title,
		NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
	if (folder != NULL && folder[0] != '\0')
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), folder);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(chooser);
		return 1;
	}
	char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
	gtk_widget_destroy(chooser);
	if (filename == NULL)
		return 1;
	puts(filename);
	g_free(filename);
	return 0;
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	const char *title = argc > 1 ? argv[1] : "Open...";
	const char *folder = argc > 2 ? argv[2] : NULL;
	return pick(title, folder);
}
