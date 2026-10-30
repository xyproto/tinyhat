#include <gtk/gtk.h>

static const struct {
	const char *key, *label;
} options[] = {
	{ "startup", "Startup sound" },
	{ "typewriter", "Typewriter sounds while typing" },
	{ "fart", "Fart sounds while typing" },
	{ "shutdown", "Shutdown sound" },
};

static GtkWidget *checks[G_N_ELEMENTS(options)];

static char *config_file(void)
{
	return g_build_filename(g_get_user_config_dir(), "tinyhat", "sounds", NULL);
}

static void run(const char *arg, const char *what)
{
	const char *argv[] = { "tinyhat-soundd", arg, what, NULL };
	g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

static void load(void)
{
	auto path = config_file();
	char *text = NULL;
	if (g_file_get_contents(path, &text, NULL, NULL)) {
		auto lines = g_strsplit(text, "\n", -1);
		for (auto l = lines; *l; l++)
			for (gsize i = 0; i < G_N_ELEMENTS(options); i++) {
				auto prefix = g_strconcat(options[i].key, "=", NULL);
				if (g_str_has_prefix(*l, prefix))
					gtk_check_button_set_active(GTK_CHECK_BUTTON(checks[i]), atoi(*l + strlen(prefix)) != 0);
				g_free(prefix);
			}
		g_strfreev(lines);
		g_free(text);
	}
	g_free(path);
}

static void toggled(GtkCheckButton *b, gpointer key)
{
	if (gtk_check_button_get_active(b))
		run("--preview", key);
}

static void save(GtkButton *b, gpointer win)
{
	(void)b;
	auto text = g_string_new(NULL);
	for (gsize i = 0; i < G_N_ELEMENTS(options); i++)
		g_string_append_printf(text, "%s=%d\n", options[i].key, gtk_check_button_get_active(GTK_CHECK_BUTTON(checks[i])));
	auto path = config_file();
	auto dir = g_path_get_dirname(path);
	GError *err = NULL;
	g_mkdir_with_parents(dir, 0755);
	if (!g_file_set_contents(path, text->str, -1, &err)) {
		auto alert = gtk_alert_dialog_new("Could not save the settings");
		gtk_alert_dialog_set_detail(alert, err->message);
		gtk_alert_dialog_show(alert, GTK_WINDOW(win));
		g_object_unref(alert);
		g_error_free(err);
	} else {
		run("--reload", NULL);
		gtk_window_destroy(GTK_WINDOW(win));
	}
	g_free(dir);
	g_free(path);
	g_string_free(text, TRUE);
}

static void activate(GtkApplication *app, gpointer data)
{
	(void)data;
	auto win = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(win), "System sounds");
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
	auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_window_set_child(GTK_WINDOW(win), box);
	for (gsize i = 0; i < G_N_ELEMENTS(options); i++) {
		checks[i] = gtk_check_button_new_with_label(options[i].label);
		gtk_box_append(GTK_BOX(box), checks[i]);
	}
	load();
	for (gsize i = 0; i < G_N_ELEMENTS(options); i++)
		g_signal_connect(checks[i], "toggled", G_CALLBACK(toggled), (gpointer)options[i].key);
	auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(row, GTK_ALIGN_END);
	gtk_widget_set_margin_top(row, 8);
	auto exit = gtk_button_new_with_mnemonic("E_xit");
	auto ok = gtk_button_new_with_mnemonic("_Save");
	gtk_widget_add_css_class(ok, "suggested-action");
	g_signal_connect_swapped(exit, "clicked", G_CALLBACK(gtk_window_destroy), win);
	g_signal_connect(ok, "clicked", G_CALLBACK(save), win);
	gtk_box_append(GTK_BOX(row), exit);
	gtk_box_append(GTK_BOX(row), ok);
	gtk_box_append(GTK_BOX(box), row);
	gtk_window_set_default_widget(GTK_WINDOW(win), ok);
	gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char *argv[])
{
	auto app = gtk_application_new("org.tinyhat.Sounds", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
