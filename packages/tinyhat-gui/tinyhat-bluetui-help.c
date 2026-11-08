#include <gtk/gtk.h>

static const char *steps[] = {
	"Put the device (gamepad, keyboard, mouse or headphones) in pairing mode.",
	"Click on the bluetui terminal, then press <b>s</b> to start scanning.",
	"Press <b>Tab</b> until <i>New devices</i> is selected, choose the device with <b>↑</b> and <b>↓</b>, then press <b>Enter</b> to pair.",
	"In <i>Paired devices</i>, press <b>t</b> to trust the device, so it connects by itself next time.",
};

static const char *keys[][2] = {
	{ "Tab, Shift+Tab", "Next or previous panel" },
	{ "↑ ↓  or  j k", "Move up and down in a list" },
	{ "s", "Start or stop scanning for new devices" },
	{ "Enter or Space", "Pair a new device, or connect or disconnect a paired one" },
	{ "t", "Trust or distrust a paired device" },
	{ "u", "Unpair (forget) a paired device" },
	{ "e", "Rename a paired device" },
	{ "f", "Mark a paired device as a favorite" },
	{ "o", "Turn the Bluetooth adapter on or off" },
	{ "p, d", "Make the adapter pairable or discoverable" },
	{ "q", "Quit bluetui" },
};

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
	return l;
}

static void activate(GtkApplication *app, gpointer data)
{
	(void)data;
	GtkWidget *win = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(win), "Tiny Hat: Bluetooth keys");
	gtk_window_set_default_size(GTK_WINDOW(win), 560, -1);
	GtkEventController *k = gtk_event_controller_key_new();
	g_signal_connect(k, "key-pressed", G_CALLBACK(on_key), win);
	gtk_widget_add_controller(win, k);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 18);
	gtk_widget_set_margin_start(box, 20);
	gtk_widget_set_margin_end(box, 20);
	gtk_window_set_child(GTK_WINDOW(win), box);

	gtk_box_append(GTK_BOX(box), label("<span size='x-large' weight='bold'>Connecting a Bluetooth device</span>", FALSE));
	for (guint i = 0; i < G_N_ELEMENTS(steps); i++) {
		char *s = g_strdup_printf("%u.  %s", i + 1, steps[i]);
		gtk_box_append(GTK_BOX(box), label(s, TRUE));
		g_free(s);
	}

	GtkWidget *heading = label("<span weight='bold'>Keys in bluetui</span>", FALSE);
	gtk_widget_set_margin_top(heading, 8);
	gtk_box_append(GTK_BOX(box), heading);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
	for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
		char *k = g_markup_printf_escaped("<tt><b>%s</b></tt>", keys[i][0]);
		char *d = g_markup_escape_text(keys[i][1], -1);
		gtk_grid_attach(GTK_GRID(grid), label(k, FALSE), 0, (int)i, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), label(d, TRUE), 1, (int)i, 1, 1);
		g_free(k);
		g_free(d);
	}
	gtk_box_append(GTK_BOX(box), grid);

	gtk_box_append(GTK_BOX(box), label("<small>This window is shown the first time bluetui is started after booting.</small>", TRUE));
	GtkWidget *ok = gtk_button_new_with_label("Got it");
	gtk_widget_set_halign(ok, GTK_ALIGN_END);
	g_signal_connect_swapped(ok, "clicked", G_CALLBACK(gtk_window_destroy), win);
	gtk_box_append(GTK_BOX(box), ok);
	gtk_window_present(GTK_WINDOW(win));
	gtk_widget_grab_focus(ok);
}

int main(int argc, char **argv)
{
	GtkApplication *app = gtk_application_new("org.tinyhat.BluetuiHelp", G_APPLICATION_NON_UNIQUE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
