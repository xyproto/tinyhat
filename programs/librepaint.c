#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#define MAXUNDO 12

static cairo_surface_t *canvas;
static GtkWidget *win;
static GtkWidget *area;
static GtkWidget *scroller;
static GtkWidget *colorbtn;
static GtkWidget *eraserbtn;
static GtkWidget *sizeadj_widget;
static GtkAdjustment *sizeadj;
static cairo_surface_t *undo[MAXUNDO];
static int undocount;
static int cw = 800, ch = 600;
static double lastx, lasty;
static int drawing, strokeeraser;
static int dirty;
static char *filepath;

static void set_title(void)
{
	char *name = filepath ? g_path_get_basename(filepath) : g_strdup("untitled");
	char *title = g_strdup_printf("%s%s - librepaint", dirty ? "*" : "", name);
	gtk_window_set_title(GTK_WINDOW(win), title);
	g_free(title);
	g_free(name);
}

static void dialog(const char *title, const char *text, GtkMessageType type)
{
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		type, GTK_BUTTONS_OK, "%s", text);
	gtk_window_set_title(GTK_WINDOW(d), title);
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

static void push_undo(void)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cw, ch);
	cairo_t *cr = cairo_create(s);
	cairo_set_source_surface(cr, canvas, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	if (undocount == MAXUNDO) {
		cairo_surface_destroy(undo[0]);
		memmove(undo, undo + 1, (MAXUNDO - 1) * sizeof(undo[0]));
		undocount--;
	}
	undo[undocount++] = s;
}

static void pop_undo(void)
{
	if (undocount == 0)
		return;
	cairo_surface_destroy(canvas);
	canvas = undo[--undocount];
	gtk_widget_set_size_request(area, cw, ch);
	gtk_widget_queue_draw(area);
	dirty = 1;
	set_title();
}

static void clear_canvas(void)
{
	cairo_t *cr = cairo_create(canvas);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_destroy(cr);
	gtk_widget_queue_draw(area);
}

static void reset_canvas(int w, int h)
{
	int i;
	for (i = 0; i < undocount; i++)
		cairo_surface_destroy(undo[i]);
	undocount = 0;
	if (canvas)
		cairo_surface_destroy(canvas);
	cw = w;
	ch = h;
	canvas = cairo_image_surface_create(CAIRO_FORMAT_RGB24, cw, ch);
	clear_canvas();
	gtk_widget_set_size_request(area, cw, ch);
}

static void use_color(cairo_t *cr, int erase)
{
	if (erase) {
		cairo_set_source_rgb(cr, 1, 1, 1);
	} else {
		GdkRGBA c;
		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(colorbtn), &c);
		cairo_set_source_rgb(cr, c.red, c.green, c.blue);
	}
}

static void draw_dot(double x, double y, int erase)
{
	cairo_t *cr = cairo_create(canvas);
	use_color(cr, erase);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_width(cr, gtk_adjustment_get_value(sizeadj));
	cairo_move_to(cr, x, y);
	cairo_line_to(cr, x + 0.01, y);
	cairo_stroke(cr);
	cairo_destroy(cr);
	gtk_widget_queue_draw(area);
}

static void draw_seg(double x, double y, int erase)
{
	cairo_t *cr = cairo_create(canvas);
	use_color(cr, erase);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	cairo_set_line_width(cr, gtk_adjustment_get_value(sizeadj));
	cairo_move_to(cr, lastx, lasty);
	cairo_line_to(cr, x, y);
	cairo_stroke(cr);
	cairo_destroy(cr);
	gtk_widget_queue_draw(area);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
	(void)w;
	(void)data;
	cairo_set_source_rgb(cr, 0.35, 0.35, 0.38);
	cairo_paint(cr);
	cairo_set_source_surface(cr, canvas, 0, 0);
	cairo_paint(cr);
	cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
	cairo_set_line_width(cr, 1);
	cairo_rectangle(cr, 0.5, 0.5, cw - 1, ch - 1);
	cairo_stroke(cr);
	return FALSE;
}

static gboolean on_press(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	(void)w;
	(void)data;
	if (ev->button != 1 && ev->button != 3)
		return FALSE;
	push_undo();
	drawing = 1;
	strokeeraser = ev->button == 3 || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(eraserbtn));
	lastx = ev->x;
	lasty = ev->y;
	draw_dot(ev->x, ev->y, strokeeraser);
	dirty = 1;
	set_title();
	return TRUE;
}

static gboolean on_release(GtkWidget *w, GdkEventButton *ev, gpointer data)
{
	(void)w;
	(void)ev;
	(void)data;
	drawing = 0;
	return TRUE;
}

static gboolean on_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data)
{
	(void)w;
	(void)data;
	if (!drawing)
		return FALSE;
	draw_seg(ev->x, ev->y, strokeeraser);
	lastx = ev->x;
	lasty = ev->y;
	return TRUE;
}

static int confirm_discard(void)
{
	if (!dirty)
		return 1;
	GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(win),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
		"Discard unsaved changes?");
	int r = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	return r == GTK_RESPONSE_YES;
}

static void set_filepath(const char *path)
{
	g_free(filepath);
	filepath = g_strdup(path);
}

static int load_image(const char *path)
{
	GError *err = NULL;
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(path, &err);
	if (!pb) {
		dialog("librepaint", err ? err->message : "Could not open image", GTK_MESSAGE_ERROR);
		g_clear_error(&err);
		return 0;
	}
	reset_canvas(gdk_pixbuf_get_width(pb), gdk_pixbuf_get_height(pb));
	cairo_t *cr = cairo_create(canvas);
	gdk_cairo_set_source_pixbuf(cr, pb, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	g_object_unref(pb);
	gtk_widget_queue_draw(area);
	set_filepath(path);
	dirty = 0;
	set_title();
	return 1;
}

static int save_image(const char *path)
{
	const char *fmt = g_str_has_suffix(path, ".jpg") || g_str_has_suffix(path, ".jpeg") ? "jpeg" : "png";
	GError *err = NULL;
	GdkPixbuf *pb = gdk_pixbuf_get_from_surface(canvas, 0, 0, cw, ch);
	if (!pb) {
		dialog("librepaint", "Could not capture the canvas", GTK_MESSAGE_ERROR);
		return 0;
	}
	gboolean ok = gdk_pixbuf_save(pb, path, fmt, &err, NULL);
	g_object_unref(pb);
	if (!ok) {
		dialog("librepaint", err ? err->message : "Could not save image", GTK_MESSAGE_ERROR);
		g_clear_error(&err);
		return 0;
	}
	set_filepath(path);
	dirty = 0;
	set_title();
	return 1;
}

static char *ask_path(int save)
{
	GtkWidget *chooser = gtk_file_chooser_dialog_new(
		save ? "Save as..." : "Open image...", GTK_WINDOW(win),
		save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel", GTK_RESPONSE_CANCEL, save ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, NULL);
	if (save)
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
	char *home = getenv("HOME");
	if (home)
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), home);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(chooser);
		return NULL;
	}
	char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
	gtk_widget_destroy(chooser);
	return path;
}

static void on_new(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	if (!confirm_discard())
		return;
	reset_canvas(800, 600);
	set_filepath(NULL);
	dirty = 0;
	set_title();
}

static void on_open(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	if (!confirm_discard())
		return;
	char *path = ask_path(0);
	if (!path)
		return;
	load_image(path);
	g_free(path);
}

static int do_save_as(void)
{
	char *path = ask_path(1);
	if (!path)
		return 0;
	int ok = save_image(path);
	g_free(path);
	return ok;
}

static void on_save(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	if (filepath)
		save_image(filepath);
	else
		do_save_as();
}

static void on_save_as(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	do_save_as();
}

static void on_undo(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	pop_undo();
}

static void on_clear(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	push_undo();
	clear_canvas();
	dirty = 1;
	set_title();
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	(void)w;
	(void)data;
	if (!(ev->state & GDK_CONTROL_MASK))
		return FALSE;
	switch (gdk_keyval_to_lower(ev->keyval)) {
	case GDK_KEY_z:
		pop_undo();
		return TRUE;
	case GDK_KEY_n:
		on_new(NULL, NULL);
		return TRUE;
	case GDK_KEY_o:
		on_open(NULL, NULL);
		return TRUE;
	case GDK_KEY_s:
		if (ev->state & GDK_SHIFT_MASK)
			do_save_as();
		else if (filepath)
			save_image(filepath);
		else
			do_save_as();
		return TRUE;
	default:
		return FALSE;
	}
}

static GtkWidget *button(const char *label, GCallback cb)
{
	GtkWidget *b = gtk_button_new_with_label(label);
	g_signal_connect(b, "clicked", cb, NULL);
	gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
	return b;
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(win), 920, 720);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win), vbox);

	GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(tools), 6);
	gtk_box_pack_start(GTK_BOX(vbox), tools, FALSE, FALSE, 0);

	colorbtn = gtk_color_button_new();
	GdkRGBA black = { 0, 0, 0, 1 };
	gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(colorbtn), &black);
	gtk_widget_set_tooltip_text(colorbtn, "Drawing color");
	gtk_box_pack_start(GTK_BOX(tools), colorbtn, FALSE, FALSE, 0);

	eraserbtn = gtk_toggle_button_new_with_label("Eraser");
	gtk_widget_set_tooltip_text(eraserbtn, "Erase with the left mouse button");
	gtk_box_pack_start(GTK_BOX(tools), eraserbtn, FALSE, FALSE, 0);

	sizeadj = gtk_adjustment_new(4, 1, 64, 1, 4, 0);
	sizeadj_widget = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, sizeadj);
	gtk_scale_set_draw_value(GTK_SCALE(sizeadj_widget), TRUE);
	gtk_widget_set_size_request(sizeadj_widget, 160, -1);
	gtk_widget_set_tooltip_text(sizeadj_widget, "Brush size");
	gtk_box_pack_start(GTK_BOX(tools), sizeadj_widget, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(tools), button("Undo", G_CALLBACK(on_undo)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tools), button("Clear", G_CALLBACK(on_clear)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tools), button("New", G_CALLBACK(on_new)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tools), button("Open...", G_CALLBACK(on_open)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tools), button("Save...", G_CALLBACK(on_save)), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(tools), button("Save as...", G_CALLBACK(on_save_as)), FALSE, FALSE, 0);

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), scroller, TRUE, TRUE, 0);

	area = gtk_drawing_area_new();
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(area, "draw", G_CALLBACK(on_draw), NULL);
	g_signal_connect(area, "button-press-event", G_CALLBACK(on_press), NULL);
	g_signal_connect(area, "button-release-event", G_CALLBACK(on_release), NULL);
	g_signal_connect(area, "motion-notify-event", G_CALLBACK(on_motion), NULL);
	gtk_container_add(GTK_CONTAINER(scroller), area);

	reset_canvas(800, 600);
	if (argc > 1)
		load_image(argv[1]);
	else
		set_title();
	gtk_widget_show_all(win);
	gtk_main();
	g_free(filepath);
	return 0;
}
