#include <errno.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <tomlc17.h>
#include <unistd.h>

#ifndef HELP_FILE
#define HELP_FILE "/etc/tinyhat/help.toml"
#endif

static toml_result_t doc;
static toml_datum_t topic;

static const char *str(toml_datum_t t, const char *key)
{
	toml_datum_t d = toml_get(t, key);
	return d.type == TOML_STRING ? d.u.s : NULL;
}

static gboolean load(const char *name)
{
	const char *path = g_getenv("TINYHAT_HELP");
	FILE *f = fopen(path && *path ? path : HELP_FILE, "r");
	if (!f) {
		fprintf(stderr, "tinyhat-help: %s: %s\n", path && *path ? path : HELP_FILE, strerror(errno));
		return FALSE;
	}
	doc = toml_parse_file_named(f, path && *path ? path : HELP_FILE);
	fclose(f);
	if (!doc.ok) {
		fprintf(stderr, "tinyhat-help: %s\n", doc.errmsg);
		return FALSE;
	}
	topic = toml_get(doc.toptab, name);
	if (topic.type != TOML_TABLE) {
		fprintf(stderr, "tinyhat-help: no [%s] in the help file\n", name);
		return FALSE;
	}
	return TRUE;
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint keycode, GdkModifierType state, gpointer win)
{
	(void)c;
	(void)keycode;
	(void)state;
	if (keyval != GDK_KEY_Escape && keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter)
		return FALSE;
	gtk_window_destroy(GTK_WINDOW(win));
	return TRUE;
}

static GtkWidget *label(const char *markup, gboolean wrap)
{
	GtkWidget *l = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(l), markup);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_wrap(GTK_LABEL(l), wrap);
	if (wrap)
		gtk_label_set_width_chars(GTK_LABEL(l), 40);
	return l;
}

static void add_steps(GtkWidget *box)
{
	toml_datum_t steps = toml_get(topic, "steps");
	if (steps.type != TOML_ARRAY)
		return;
	for (int i = 0; i < steps.u.arr.size; i++) {
		if (steps.u.arr.elem[i].type != TOML_STRING)
			continue;
		char *s = g_strdup_printf("%d.  %s", i + 1, steps.u.arr.elem[i].u.s);
		gtk_box_append(GTK_BOX(box), label(s, TRUE));
		g_free(s);
	}
}

static void add_keys(GtkWidget *box)
{
	toml_datum_t keys = toml_get(topic, "keys");
	const char *heading = str(topic, "keys_heading");
	if (keys.type != TOML_ARRAY)
		return;
	if (heading) {
		char *h = g_markup_printf_escaped("<span weight='bold'>%s</span>", heading);
		GtkWidget *l = label(h, FALSE);
		gtk_widget_set_margin_top(l, 8);
		gtk_box_append(GTK_BOX(box), l);
		g_free(h);
	}
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	for (int i = 0; i < keys.u.arr.size; i++) {
		toml_datum_t row = keys.u.arr.elem[i];
		if (row.type != TOML_ARRAY || row.u.arr.size != 2 || row.u.arr.elem[0].type != TOML_STRING ||
		    row.u.arr.elem[1].type != TOML_STRING)
			continue;
		char *k = g_markup_printf_escaped("<tt><b>%s</b></tt>", row.u.arr.elem[0].u.s);
		char *d = g_markup_escape_text(row.u.arr.elem[1].u.s, -1);
		gtk_grid_attach(GTK_GRID(grid), label(k, FALSE), 0, i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), label(d, TRUE), 1, i, 1, 1);
		g_free(k);
		g_free(d);
	}
	gtk_box_append(GTK_BOX(box), grid);
}

static void activate(GtkApplication *app, gpointer data)
{
	(void)data;
	const char *title = str(topic, "title"), *heading = str(topic, "heading"), *footer = str(topic, "footer");
	GtkWidget *win = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(win), title ? title : "Tiny Hat help");
	gtk_window_set_default_size(GTK_WINDOW(win), 560, -1);
	GtkEventController *k = gtk_event_controller_key_new();
	g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), win);
	gtk_widget_add_controller(win, k);

	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_bottom(outer, 18);
	gtk_widget_set_margin_end(outer, 20);
	gtk_window_set_child(GTK_WINDOW(win), outer);
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(outer), scroll);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_start(box, 20);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);

	if (heading) {
		char *h = g_markup_printf_escaped("<span size='x-large' weight='bold'>%s</span>", heading);
		gtk_box_append(GTK_BOX(box), label(h, FALSE));
		g_free(h);
	}
	add_steps(box);
	add_keys(box);
	if (footer) {
		char *f = g_markup_printf_escaped("<small>%s</small>", footer);
		gtk_box_append(GTK_BOX(box), label(f, TRUE));
		g_free(f);
	}
	GtkWidget *ok = gtk_button_new_with_label("Got it");
	gtk_widget_set_halign(ok, GTK_ALIGN_END);
	g_signal_connect_swapped(ok, "clicked", G_CALLBACK(gtk_window_destroy), win);
	gtk_box_append(GTK_BOX(outer), ok);
	gtk_window_present(GTK_WINDOW(win));
	gtk_widget_grab_focus(ok);
}

static gboolean first_time(const char *name)
{
	const char *rt = g_getenv("XDG_RUNTIME_DIR");
	char *dir = g_build_filename(rt && *rt ? rt : g_get_tmp_dir(), "tinyhat-help", NULL);
	char *marker = g_build_filename(dir, name, NULL);
	gboolean first = !g_file_test(marker, G_FILE_TEST_EXISTS);
	if (first && g_mkdir_with_parents(dir, 0700) == 0)
		g_file_set_contents(marker, "", 0, NULL);
	g_free(dir);
	g_free(marker);
	return first;
}

static int show(const char *name)
{
	if (!load(name))
		return 1;
	GtkApplication *app = gtk_application_new("org.tinyhat.Help", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	char *argv[] = { "tinyhat-help", NULL };
	int status = g_application_run(G_APPLICATION(app), 1, argv);
	g_object_unref(app);
	toml_free(doc);
	return status;
}

int main(int argc, char **argv)
{
	int i = 1;
	gboolean once = FALSE;
	if (i < argc && !strcmp(argv[i], "--once")) {
		once = TRUE;
		i++;
	}
	if (i >= argc || argv[i][0] == '-') {
		fprintf(stderr, "usage: tinyhat-help [--once] TOPIC [-- COMMAND [ARG...]]\n");
		return 2;
	}
	const char *name = argv[i++];
	char **cmd = NULL;
	if (i < argc) {
		if (strcmp(argv[i], "--") || i + 1 >= argc) {
			fprintf(stderr, "usage: tinyhat-help [--once] TOPIC [-- COMMAND [ARG...]]\n");
			return 2;
		}
		cmd = argv + i + 1;
	}
	gboolean wanted = !once || first_time(name);
	if (!cmd)
		return wanted ? show(name) : 0;
	if (wanted) {
		pid_t pid = fork();
		if (pid == 0) {
			setsid();
			if (fork() == 0)
				_exit(show(name));
			_exit(0);
		}
		if (pid > 0)
			waitpid(pid, NULL, 0);
	}
	execvp(cmd[0], cmd);
	fprintf(stderr, "tinyhat-help: %s: %s\n", cmd[0], strerror(errno));
	return 127;
}
