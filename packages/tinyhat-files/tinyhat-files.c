#include <gtk/gtk.h>
#include <string.h>

#define ATTRS "standard::name,standard::display-name,standard::type,standard::size,standard::icon,standard::is-hidden,standard::is-backup,standard::is-symlink,standard::fast-content-type,time::modified"

enum { JOB_COPY, JOB_MOVE, JOB_DELETE, JOB_RESTORE, JOB_EMPTY, JOB_EXTRACT, JOB_COMPRESS };
enum { COL_NAME, COL_SIZE, COL_TYPE, COL_TIME };

typedef struct {
	GtkWidget *box, *pathentry, *stack, *view, *grid;
	GtkDirectoryList *dirlist;
	GListStore *results;
	GtkFilter *filter;
	GtkFilterListModel *filtered;
	GtkMultiSelection *selection;
	GFile *cwd;
	GList *back, *forward;
	GCancellable *search, *fsinfo;
	char *free;
	gboolean grid_mode, searching, search_running;
} pane;

typedef struct {
	GList *files;
	GFile *dest;
	int kind;
} job;

static GtkWidget *win, *sidebar, *searchentry, *statuslbl, *cancelbtn;
static pane panes[2];
static pane *act;
static gboolean show_hidden, dual, busy;
static GList *cut_files;
static GCancellable *job_cancel;
static GVolumeMonitor *volumes;
static guint sidebar_idle;

static void update_status(void);
static void update_actions(void);
static void go(pane *p, GFile *dir);
static const char *archive_suffix(const char *name);

static GSimpleAction *action(const char *name)
{
	return G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(win), name));
}

static void enable(const char *name, gboolean on)
{
	g_simple_action_set_enabled(action(name), on);
}

static pane *other(void)
{
	return act == &panes[0] ? &panes[1] : &panes[0];
}

static GtkWidget *current_view(pane *p)
{
	return p->grid_mode ? p->grid : p->view;
}

static void set_status(const char *s)
{
	gtk_label_set_text(GTK_LABEL(statuslbl), s);
}

static void free_files(gpointer files)
{
	g_list_free_full(files, g_object_unref);
}

static GFile *info_file(GFileInfo *info)
{
	return G_FILE(g_file_info_get_attribute_object(info, "standard::file"));
}

static gboolean is_dir(GFileInfo *info)
{
	return g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
}

static GFile *trash_dir(void)
{
	auto path = g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
	auto f = g_file_new_for_path(path);
	g_free(path);
	return f;
}

static gboolean in_trash(pane *p)
{
	auto t = trash_dir();
	gboolean r = p->cwd && !p->searching && g_file_equal(p->cwd, t);
	g_object_unref(t);
	return r;
}

static guint selected_count(pane *p)
{
	auto set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(p->selection));
	guint n = (guint)gtk_bitset_get_size(set);
	gtk_bitset_unref(set);
	return n;
}

static GList *selected_infos(pane *p)
{
	GList *out = NULL;
	auto set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(p->selection));
	GtkBitsetIter it;
	guint pos;
	for (gboolean ok = gtk_bitset_iter_init_first(&it, set, &pos); ok; ok = gtk_bitset_iter_next(&it, &pos))
		out = g_list_prepend(out, g_list_model_get_item(G_LIST_MODEL(p->selection), pos));
	gtk_bitset_unref(set);
	return g_list_reverse(out);
}

static GList *selected_files(pane *p)
{
	auto infos = selected_infos(p);
	for (auto l = infos; l; l = l->next) {
		auto f = g_object_ref(info_file(l->data));
		g_object_unref(l->data);
		l->data = f;
	}
	return infos;
}

static GSList *to_slist(GList *files)
{
	GSList *sl = NULL;
	for (auto l = files; l; l = l->next)
		sl = g_slist_prepend(sl, l->data);
	return g_slist_reverse(sl);
}

static GList *from_file_list(const GValue *v)
{
	auto sl = gdk_file_list_get_files(g_value_get_boxed(v));
	GList *files = NULL;
	for (auto l = sl; l; l = l->next)
		files = g_list_prepend(files, g_object_ref(l->data));
	g_slist_free(sl);
	return g_list_reverse(files);
}

static char *format_time(GFileInfo *info, const char *attr)
{
	if (!g_file_info_has_attribute(info, attr))
		return g_strdup("");
	auto t = g_date_time_new_from_unix_local((gint64)g_file_info_get_attribute_uint64(info, attr));
	auto s = g_date_time_format(t, "%Y-%m-%d %H:%M:%S");
	g_date_time_unref(t);
	return s;
}

static void update_status(void)
{
	auto p = act;
	if (busy || !p->cwd)
		return;
	if (!p->searching && gtk_directory_list_get_error(p->dirlist)) {
		set_status(gtk_directory_list_get_error(p->dirlist)->message);
		return;
	}
	guint n = g_list_model_get_n_items(G_LIST_MODEL(p->filtered));
	auto s = g_string_new(NULL);
	if (p->searching)
		g_string_append_printf(s, "%s%u found", p->search_running ? "Searching... " : "", n);
	else
		g_string_append_printf(s, n == 1 ? "%u item" : "%u items", n);
	auto sel = selected_infos(p);
	if (sel) {
		guint64 size = 0;
		for (auto l = sel; l; l = l->next)
			if (!is_dir(l->data))
				size += g_file_info_get_size(l->data);
		auto fs = g_format_size(size);
		g_string_append_printf(s, ", %u selected (%s)", g_list_length(sel), fs);
		g_free(fs);
		g_list_free_full(sel, g_object_unref);
	}
	if (p->free)
		g_string_append_printf(s, ", %s free", p->free);
	set_status(s->str);
	g_string_free(s, TRUE);
}

static void update_title(void)
{
	auto base = act->cwd ? g_file_get_basename(act->cwd) : NULL;
	auto title = g_strdup_printf("%s - Files", base ? base : "/");
	gtk_window_set_title(GTK_WINDOW(win), title);
	g_free(title);
	g_free(base);
}

static void update_actions(void)
{
	if (!act->selection)
		return;
	guint n = selected_count(act);
	gboolean trash = in_trash(act);
	enable("open", n > 0);
	enable("openwith", n > 0);
	enable("view", n > 0);
	enable("edit", n > 0);
	enable("cut", n > 0);
	enable("copy", n > 0);
	enable("paste", !act->searching);
	enable("copyto", dual && n > 0);
	enable("moveto", dual && n > 0);
	enable("link", n > 0);
	gboolean archives = n > 0 && !trash;
	auto infos = archives ? selected_infos(act) : NULL;
	for (auto l = infos; l && archives; l = l->next)
		archives = !is_dir(l->data) && archive_suffix(g_file_info_get_name(l->data));
	g_list_free_full(infos, g_object_unref);
	enable("extract", archives);
	enable("compress", n > 0 && !trash);
	enable("rename", n == 1);
	enable("trash", n > 0 && !trash);
	enable("delete", n > 0);
	enable("restore", trash && n > 0);
	enable("emptytrash", trash);
	enable("mkdir", !act->searching);
	enable("newfile", !act->searching);
	enable("back", act->back != NULL);
	enable("forward", act->forward != NULL);
	enable("swap", dual);
}

static void sync_sidebar(void)
{
	GtkListBoxRow *row;
	gtk_list_box_unselect_all(GTK_LIST_BOX(sidebar));
	for (int i = 0; (row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar), i)); i++) {
		GFile *f = g_object_get_data(G_OBJECT(row), "file");
		if (f && act->cwd && !act->searching && g_file_equal(f, act->cwd)) {
			gtk_list_box_select_row(GTK_LIST_BOX(sidebar), row);
			break;
		}
	}
}

static void set_active(pane *p)
{
	act = p;
	for (int i = 0; i < 2; i++) {
		if (&panes[i] == p)
			gtk_widget_remove_css_class(panes[i].pathentry, "dim-label");
		else
			gtk_widget_add_css_class(panes[i].pathentry, "dim-label");
	}
	g_simple_action_set_state(action("grid"), g_variant_new_boolean(p->grid_mode));
	update_title();
	sync_sidebar();
	update_actions();
	update_status();
}

static void got_fsinfo(GObject *src, GAsyncResult *res, gpointer data)
{
	pane *p = data;
	auto info = g_file_query_filesystem_info_finish(G_FILE(src), res, NULL);
	if (!info)
		return;
	g_clear_pointer(&p->free, g_free);
	if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE))
		p->free = g_format_size(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE));
	g_object_unref(info);
	if (p == act)
		update_status();
}

static void query_free(pane *p)
{
	if (p->fsinfo) {
		g_cancellable_cancel(p->fsinfo);
		g_object_unref(p->fsinfo);
	}
	p->fsinfo = g_cancellable_new();
	g_file_query_filesystem_info_async(p->cwd, G_FILE_ATTRIBUTE_FILESYSTEM_FREE, G_PRIORITY_DEFAULT, p->fsinfo, got_fsinfo, p);
}

static void stop_search(pane *p)
{
	if (p->search) {
		g_cancellable_cancel(p->search);
		g_clear_object(&p->search);
	}
	if (p->searching) {
		p->searching = FALSE;
		p->search_running = FALSE;
		gtk_filter_list_model_set_model(p->filtered, G_LIST_MODEL(p->dirlist));
		g_list_store_remove_all(p->results);
	}
}

static void load(pane *p, GFile *dir)
{
	stop_search(p);
	g_set_object(&p->cwd, dir);
	gtk_directory_list_set_file(p->dirlist, dir);
	auto name = g_file_get_parse_name(dir);
	gtk_editable_set_text(GTK_EDITABLE(p->pathentry), name);
	g_free(name);
	gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(p->selection));
	g_clear_pointer(&p->free, g_free);
	query_free(p);
	if (p == act) {
		gtk_editable_set_text(GTK_EDITABLE(searchentry), "");
		update_title();
		sync_sidebar();
		update_actions();
		update_status();
	}
}

static void go(pane *p, GFile *dir)
{
	if (p->cwd && !p->searching && g_file_equal(p->cwd, dir))
		return;
	if (p->cwd)
		p->back = g_list_prepend(p->back, g_object_ref(p->cwd));
	g_list_free_full(p->forward, g_object_unref);
	p->forward = NULL;
	load(p, dir);
}

static GFile *unique_child(GFile *dir, const char *name)
{
	auto f = g_file_get_child(dir, name);
	if (!g_file_query_exists(f, NULL))
		return f;
	auto dot = strrchr(name, '.');
	if (dot == name || g_file_query_file_type(f, 0, NULL) == G_FILE_TYPE_DIRECTORY)
		dot = NULL;
	auto stem = dot ? g_strndup(name, dot - name) : g_strdup(name);
	for (int i = 1; g_file_query_exists(f, NULL); i++) {
		auto n = i == 1 ? g_strdup_printf("%s (copy)%s", stem, dot ? dot : "")
				: g_strdup_printf("%s (copy %d)%s", stem, i, dot ? dot : "");
		g_object_unref(f);
		f = g_file_get_child(dir, n);
		g_free(n);
	}
	g_free(stem);
	return f;
}

static gboolean copy_tree(GFile *src, GFile *dst, GCancellable *c, GError **err)
{
	if (g_file_query_file_type(src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c) != G_FILE_TYPE_DIRECTORY)
		return g_file_copy(src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA, c, NULL, NULL, err);
	if (!g_file_make_directory(dst, c, err))
		return FALSE;
	auto e = g_file_enumerate_children(src, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, err);
	if (!e)
		return FALSE;
	gboolean ok = TRUE;
	GFileInfo *info;
	while (ok && (info = g_file_enumerator_next_file(e, c, err))) {
		auto s = g_file_get_child(src, g_file_info_get_name(info));
		auto d = g_file_get_child(dst, g_file_info_get_name(info));
		ok = copy_tree(s, d, c, err);
		g_object_unref(s);
		g_object_unref(d);
		g_object_unref(info);
	}
	if (ok && err && *err)
		ok = FALSE;
	g_object_unref(e);
	return ok;
}

static gboolean delete_tree(GFile *f, GCancellable *c, GError **err)
{
	if (g_file_query_file_type(f, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c) == G_FILE_TYPE_DIRECTORY) {
		auto e = g_file_enumerate_children(f, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, err);
		if (!e)
			return FALSE;
		gboolean ok = TRUE;
		GFileInfo *info;
		while (ok && (info = g_file_enumerator_next_file(e, c, err))) {
			auto child = g_file_get_child(f, g_file_info_get_name(info));
			ok = delete_tree(child, c, err);
			g_object_unref(child);
			g_object_unref(info);
		}
		g_object_unref(e);
		if (!ok || (err && *err))
			return FALSE;
	}
	return g_file_delete(f, c, err);
}

static gboolean move_file(GFile *f, GFile *d, GCancellable *c, GError **err)
{
	GError *e = NULL;
	if (g_file_move(f, d, G_FILE_COPY_NOFOLLOW_SYMLINKS, c, NULL, NULL, &e))
		return TRUE;
	if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE)) {
		g_propagate_error(err, e);
		return FALSE;
	}
	g_error_free(e);
	return copy_tree(f, d, c, err) && delete_tree(f, c, err);
}

static gboolean progress_idle(gpointer data)
{
	if (busy)
		set_status(data);
	g_free(data);
	return G_SOURCE_REMOVE;
}

static void progress(const char *verb, guint i, guint n, GFile *f)
{
	auto base = g_file_get_basename(f);
	g_idle_add(progress_idle, g_strdup_printf("%s %u of %u: %s", verb, i, n, base));
	g_free(base);
}

static void restore_one(GFile *f, GCancellable *c, GError **err)
{
	auto files = g_file_get_parent(f);
	auto top = g_file_get_parent(files);
	auto base = g_file_get_basename(f);
	auto infoname = g_strconcat(base, ".trashinfo", NULL);
	auto info = g_file_get_child(top, "info");
	auto infofile = g_file_get_child(info, infoname);
	auto kf = g_key_file_new();
	char *path = NULL;
	if (g_key_file_load_from_file(kf, g_file_peek_path(infofile), G_KEY_FILE_NONE, NULL))
		path = g_key_file_get_string(kf, "Trash Info", "Path", NULL);
	if (!path) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s has no record of where it came from", base);
	} else {
		auto orig = g_uri_unescape_string(path, NULL);
		auto dest = g_file_new_for_path(orig);
		auto parent = g_file_get_parent(dest);
		auto destname = g_file_get_basename(dest);
		g_file_make_directory_with_parents(parent, c, NULL);
		auto target = unique_child(parent, destname);
		if (move_file(f, target, c, err))
			g_file_delete(infofile, c, NULL);
		g_object_unref(target);
		g_free(destname);
		g_object_unref(parent);
		g_object_unref(dest);
		g_free(orig);
	}
	g_free(path);
	g_key_file_unref(kf);
	g_object_unref(infofile);
	g_object_unref(info);
	g_free(infoname);
	g_free(base);
	g_object_unref(top);
	g_object_unref(files);
}

static void empty_dir(GFile *dir, GCancellable *c, GError **err)
{
	auto e = g_file_enumerate_children(dir, "standard::name", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, NULL);
	if (!e)
		return;
	GFileInfo *info;
	while (!*err && (info = g_file_enumerator_next_file(e, c, err))) {
		auto child = g_file_get_child(dir, g_file_info_get_name(info));
		delete_tree(child, c, err);
		g_object_unref(child);
		g_object_unref(info);
	}
	g_object_unref(e);
}

static const char *const archive_suffixes[] = {
	".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz", ".tar.lzma", ".tgz", ".tbz2", ".txz", ".tzst",
	".tar", ".zip", ".7z", ".rar", ".jar", ".iso", ".cpio", ".lha", ".lzh", ".cab", ".xar", NULL,
};

static const char *archive_suffix(const char *name)
{
	size_t len = strlen(name);
	for (int i = 0; archive_suffixes[i]; i++) {
		size_t n = strlen(archive_suffixes[i]);
		if (len > n && !g_ascii_strcasecmp(name + len - n, archive_suffixes[i]))
			return name + len - n;
	}
	return NULL;
}

static gboolean run_tool(const char *const *argv, GCancellable *c, GError **err)
{
	auto proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE, err);
	char *out = NULL;
	if (!proc)
		return FALSE;
	gboolean ok = g_subprocess_communicate_utf8(proc, NULL, c, NULL, &out, err);
	if (!ok) {
		g_subprocess_force_exit(proc);
	} else if (!g_subprocess_get_successful(proc)) {
		ok = FALSE;
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s", argv[0], out && *g_strstrip(out) ? out : "failed");
	}
	g_free(out);
	g_object_unref(proc);
	return ok;
}

static void extract_one(GFile *f, GCancellable *c, GError **err)
{
	auto name = g_file_get_basename(f);
	auto stem = g_strndup(name, archive_suffix(name) ? (gsize)(archive_suffix(name) - name) : strlen(name));
	auto parent = g_file_get_parent(f);
	auto dest = unique_child(parent, stem);
	if (g_file_make_directory(dest, c, err)) {
		auto path = g_file_peek_path(f);
		auto to = g_file_peek_path(dest);
		const char *bsdtar[] = { "bsdtar", "-xf", path, "-C", to, NULL };
		auto o = g_strconcat("-o", to, NULL);
		const char *sevenzip[] = { "7z", "x", "-y", o, path, NULL };
		GError *first = NULL;
		if (!run_tool(bsdtar, c, &first) && !g_cancellable_is_cancelled(c) && !run_tool(sevenzip, c, NULL)) {
			g_propagate_error(err, first);
			first = NULL;
		}
		g_clear_error(&first);
		if (!*err)
			g_cancellable_set_error_if_cancelled(c, err);
		if (*err)
			delete_tree(dest, NULL, NULL);
		g_free(o);
	}
	g_object_unref(dest);
	g_object_unref(parent);
	g_free(stem);
	g_free(name);
}

static void compress_files(GList *files, GFile *dest, GCancellable *c, GError **err)
{
	auto argv = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(argv, g_strdup("bsdtar"));
	g_ptr_array_add(argv, g_strdup("-a"));
	g_ptr_array_add(argv, g_strdup("-cf"));
	g_ptr_array_add(argv, g_file_get_path(dest));
	for (auto l = files; l; l = l->next) {
		auto parent = g_file_get_parent(l->data);
		g_ptr_array_add(argv, g_strdup("-C"));
		g_ptr_array_add(argv, g_file_get_path(parent));
		g_ptr_array_add(argv, g_file_get_basename(l->data));
		g_object_unref(parent);
	}
	g_ptr_array_add(argv, NULL);
	if (!run_tool((const char *const *)argv->pdata, c, err))
		g_file_delete(dest, NULL, NULL);
	g_ptr_array_unref(argv);
}

static void job_free(gpointer data)
{
	job *j = data;
	g_list_free_full(j->files, g_object_unref);
	g_clear_object(&j->dest);
	g_free(j);
}

static void job_thread(GTask *task, gpointer src, gpointer data, GCancellable *c)
{
	(void)src;
	job *j = data;
	GError *err = NULL;
	guint n = g_list_length(j->files), i = 0;
	if (j->kind == JOB_EMPTY) {
		auto files = trash_dir();
		auto top = g_file_get_parent(files);
		auto info = g_file_get_child(top, "info");
		g_idle_add(progress_idle, g_strdup("Emptying the trash..."));
		empty_dir(files, c, &err);
		if (!err)
			empty_dir(info, c, &err);
		g_object_unref(info);
		g_object_unref(top);
		g_object_unref(files);
	}
	if (j->kind == JOB_COMPRESS) {
		auto base = g_file_get_basename(j->dest);
		g_idle_add(progress_idle, g_strdup_printf("Compressing to %s...", base));
		g_free(base);
		compress_files(j->files, j->dest, c, &err);
	}
	for (auto l = j->files; l && !err && j->kind != JOB_COMPRESS; l = l->next) {
		GFile *f = l->data;
		i++;
		if (j->kind == JOB_DELETE) {
			progress("Deleting", i, n, f);
			delete_tree(f, c, &err);
			continue;
		}
		if (j->kind == JOB_EXTRACT) {
			progress("Extracting", i, n, f);
			extract_one(f, c, &err);
			continue;
		}
		if (j->kind == JOB_RESTORE) {
			progress("Restoring", i, n, f);
			restore_one(f, c, &err);
			continue;
		}
		if (g_file_equal(f, j->dest) || g_file_has_prefix(j->dest, f)) {
			g_set_error(&err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Cannot put a folder inside itself");
			break;
		}
		auto parent = g_file_get_parent(f);
		gboolean same = parent && g_file_equal(parent, j->dest);
		g_clear_object(&parent);
		if (j->kind == JOB_MOVE && same)
			continue;
		progress(j->kind == JOB_MOVE ? "Moving" : "Copying", i, n, f);
		auto name = g_file_get_basename(f);
		auto d = unique_child(j->dest, name);
		g_free(name);
		if (j->kind == JOB_MOVE)
			move_file(f, d, c, &err);
		else
			copy_tree(f, d, c, &err);
		g_object_unref(d);
	}
	if (err)
		g_task_return_error(task, err);
	else
		g_task_return_boolean(task, TRUE);
}

static void job_done(GObject *src, GAsyncResult *res, gpointer data)
{
	(void)src;
	(void)data;
	GError *err = NULL;
	busy = FALSE;
	gtk_widget_set_visible(cancelbtn, FALSE);
	g_clear_object(&job_cancel);
	if (!g_task_propagate_boolean(G_TASK(res), &err)) {
		set_status(g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED) ? "Canceled" : err->message);
		g_error_free(err);
		return;
	}
	for (int i = 0; i < 2; i++)
		if (panes[i].cwd)
			query_free(&panes[i]);
	update_status();
}

static void run_job(int kind, GList *files, GFile *dest)
{
	if (busy || (!files && kind != JOB_EMPTY)) {
		if (busy)
			set_status("Wait for the current operation to finish");
		g_list_free_full(files, g_object_unref);
		return;
	}
	auto j = g_new0(job, 1);
	j->kind = kind;
	j->files = files;
	j->dest = dest ? g_object_ref(dest) : NULL;
	busy = TRUE;
	job_cancel = g_cancellable_new();
	gtk_widget_set_visible(cancelbtn, TRUE);
	auto task = g_task_new(NULL, job_cancel, job_done, NULL);
	g_task_set_task_data(task, j, job_free);
	g_task_run_in_thread(task, job_thread);
	g_object_unref(task);
}

typedef struct {
	pane *p;
	GCancellable *cancel;
	GPtrArray *items;
	gboolean done;
} batch;

typedef struct {
	pane *p;
	GFile *root;
	char *needle;
	GPatternSpec *spec;
	gboolean hidden;
} search_job;

static void search_job_free(gpointer data)
{
	search_job *sj = data;
	g_object_unref(sj->root);
	g_free(sj->needle);
	if (sj->spec)
		g_pattern_spec_free(sj->spec);
	g_free(sj);
}

static gboolean add_batch(gpointer data)
{
	batch *b = data;
	if (!g_cancellable_is_cancelled(b->cancel)) {
		g_list_store_splice(b->p->results, g_list_model_get_n_items(G_LIST_MODEL(b->p->results)), 0,
				    b->items->pdata, b->items->len);
		if (b->done)
			b->p->search_running = FALSE;
		if (b->p == act)
			update_status();
	}
	g_ptr_array_unref(b->items);
	g_object_unref(b->cancel);
	g_free(b);
	return G_SOURCE_REMOVE;
}

static void flush(search_job *sj, GCancellable *c, GPtrArray **items, gboolean done)
{
	auto b = g_new0(batch, 1);
	b->p = sj->p;
	b->cancel = g_object_ref(c);
	b->items = *items;
	b->done = done;
	g_idle_add(add_batch, b);
	*items = g_ptr_array_new_with_free_func(g_object_unref);
}

static gboolean name_matches(search_job *sj, const char *name)
{
	auto f = g_utf8_casefold(name, -1);
	gboolean m = sj->spec ? g_pattern_spec_match_string(sj->spec, f) : strstr(f, sj->needle) != NULL;
	g_free(f);
	return m;
}

static gboolean skip_dir(GFile *f)
{
	static const char *skip[] = { "/proc", "/sys", "/dev", "/run" };
	auto path = g_file_peek_path(f);
	for (size_t i = 0; path && i < G_N_ELEMENTS(skip); i++)
		if (!strcmp(path, skip[i]))
			return TRUE;
	return FALSE;
}

static void search_thread(GTask *task, gpointer src, gpointer data, GCancellable *c)
{
	(void)src;
	search_job *sj = data;
	GQueue q = G_QUEUE_INIT;
	auto items = g_ptr_array_new_with_free_func(g_object_unref);
	guint total = 0;
	GFile *dir;
	g_queue_push_tail(&q, g_object_ref(sj->root));
	while (!g_cancellable_is_cancelled(c) && total < 10000 && (dir = g_queue_pop_head(&q))) {
		auto e = g_file_enumerate_children(dir, ATTRS, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, c, NULL);
		GFileInfo *info;
		while (e && (info = g_file_enumerator_next_file(e, c, NULL))) {
			auto f = g_file_get_child(dir, g_file_info_get_name(info));
			if (sj->hidden || !(g_file_info_get_is_hidden(info) || g_file_info_get_is_backup(info))) {
				if (is_dir(info) && !skip_dir(f))
					g_queue_push_tail(&q, g_object_ref(f));
				if (name_matches(sj, g_file_info_get_display_name(info))) {
					g_file_info_set_attribute_object(info, "standard::file", G_OBJECT(f));
					g_ptr_array_add(items, g_object_ref(info));
					total++;
					if (items->len >= 200)
						flush(sj, c, &items, FALSE);
				}
			}
			g_object_unref(f);
			g_object_unref(info);
		}
		g_clear_object(&e);
		g_object_unref(dir);
	}
	g_queue_clear_full(&q, g_object_unref);
	flush(sj, c, &items, TRUE);
	g_ptr_array_unref(items);
	g_task_return_boolean(task, TRUE);
}

static void start_search(pane *p, const char *text)
{
	stop_search(p);
	if (*text) {
		p->searching = TRUE;
		p->search_running = TRUE;
		gtk_filter_list_model_set_model(p->filtered, G_LIST_MODEL(p->results));
		p->search = g_cancellable_new();
		auto sj = g_new0(search_job, 1);
		sj->p = p;
		sj->root = g_object_ref(p->cwd);
		sj->needle = g_utf8_casefold(text, -1);
		sj->spec = strpbrk(text, "*?") ? g_pattern_spec_new(sj->needle) : NULL;
		sj->hidden = show_hidden;
		auto task = g_task_new(NULL, p->search, NULL, NULL);
		g_task_set_task_data(task, sj, search_job_free);
		g_task_run_in_thread(task, search_thread);
		g_object_unref(task);
	}
	sync_sidebar();
	update_actions();
	update_status();
}

static void launched(GObject *src, GAsyncResult *res, gpointer data)
{
	(void)data;
	GError *err = NULL;
	if (!gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(src), res, &err)) {
		if (!g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
			set_status(err->message);
		g_error_free(err);
	}
}

static void open_info(pane *p, GFileInfo *info)
{
	if (is_dir(info)) {
		go(p, info_file(info));
		return;
	}
	auto l = gtk_file_launcher_new(info_file(info));
	gtk_file_launcher_launch(l, GTK_WINDOW(win), NULL, launched, NULL);
	g_object_unref(l);
}

static void on_activate(GtkWidget *v, guint pos, gpointer data)
{
	(void)v;
	pane *p = data;
	GFileInfo *info = g_list_model_get_item(G_LIST_MODEL(p->selection), pos);
	open_info(p, info);
	g_object_unref(info);
}

static void in_terminal(GFile *dir, const char *const *cmd)
{
	auto term = g_getenv("TERMINAL");
	auto argv = g_ptr_array_new();
	g_ptr_array_add(argv, (char *)(term && *term ? term : "foot"));
	for (; cmd && *cmd; cmd++)
		g_ptr_array_add(argv, (char *)*cmd);
	g_ptr_array_add(argv, NULL);
	GError *err = NULL;
	if (!g_spawn_async(dir ? g_file_peek_path(dir) : NULL, (char **)argv->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
		set_status(err->message);
		g_error_free(err);
	}
	g_ptr_array_free(argv, TRUE);
}

#define UNUSED_ACTION_ARGS \
	(void)a;           \
	(void)p;           \
	(void)d

static void act_open(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto pn = act;
	auto infos = selected_infos(pn);
	for (auto l = infos; l; l = l->next) {
		open_info(pn, l->data);
		if (is_dir(l->data))
			break;
	}
	g_list_free_full(infos, g_object_unref);
}

static void run_each(const char *prog)
{
	auto infos = selected_infos(act);
	for (auto l = infos; l; l = l->next) {
		const char *cmd[] = { prog, g_file_peek_path(info_file(l->data)), NULL };
		if (!is_dir(l->data) && cmd[1])
			in_terminal(act->cwd, cmd);
	}
	g_list_free_full(infos, g_object_unref);
}

static void act_view(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	run_each("less");
}

static void act_edit(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto editor = g_getenv("EDITOR");
	run_each(editor && *editor ? editor : "o");
}

static void to_clipboard(gboolean cut)
{
	auto files = selected_files(act);
	if (!files)
		return;
	auto sl = to_slist(files);
	auto fl = gdk_file_list_new_from_list(sl);
	gdk_clipboard_set(gtk_widget_get_clipboard(win), GDK_TYPE_FILE_LIST, fl);
	g_boxed_free(GDK_TYPE_FILE_LIST, fl);
	g_slist_free(sl);
	g_list_free_full(cut_files, g_object_unref);
	cut_files = cut ? files : NULL;
	if (!cut)
		g_list_free_full(files, g_object_unref);
	set_status(cut ? "Cut. Paste to move." : "Copied.");
}

static void act_copy(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	to_clipboard(FALSE);
}

static void act_cut(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	to_clipboard(TRUE);
}

static void pasted(GObject *src, GAsyncResult *res, gpointer data)
{
	pane *p = data;
	GError *err = NULL;
	auto v = gdk_clipboard_read_value_finish(GDK_CLIPBOARD(src), res, &err);
	if (!v) {
		set_status("Nothing to paste");
		g_clear_error(&err);
		return;
	}
	auto files = from_file_list(v);
	gboolean move = cut_files && files && g_file_equal(cut_files->data, files->data);
	if (move) {
		g_list_free_full(cut_files, g_object_unref);
		cut_files = NULL;
	}
	run_job(move ? JOB_MOVE : JOB_COPY, files, p->cwd);
}

static void act_paste(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	gdk_clipboard_read_value_async(gtk_widget_get_clipboard(win), GDK_TYPE_FILE_LIST,
				       G_PRIORITY_DEFAULT, NULL, pasted, act);
}

static void act_copyto(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (dual && !other()->searching)
		run_job(JOB_COPY, selected_files(act), other()->cwd);
}

static void act_moveto(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (dual && !other()->searching)
		run_job(JOB_MOVE, selected_files(act), other()->cwd);
}

static void reload(pane *p)
{
	if (p->searching) {
		start_search(p, gtk_editable_get_text(GTK_EDITABLE(searchentry)));
		return;
	}
	gtk_directory_list_set_file(p->dirlist, NULL);
	gtk_directory_list_set_file(p->dirlist, p->cwd);
	query_free(p);
}

static void act_reload(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	reload(act);
}

static void act_f5(GSimpleAction *a, GVariant *p, gpointer d)
{
	if (dual)
		act_copyto(a, p, d);
	else
		reload(act);
}

static void act_f6(GSimpleAction *a, GVariant *p, gpointer d)
{
	if (dual)
		act_moveto(a, p, d);
}

static void act_link(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto dest = dual && !other()->searching ? other()->cwd : act->cwd;
	auto files = selected_files(act);
	for (auto l = files; l; l = l->next) {
		auto base = g_file_get_basename(l->data);
		auto parent = g_file_get_parent(l->data);
		auto name = parent && g_file_equal(parent, dest) ? g_strdup_printf("Link to %s", base) : g_strdup(base);
		auto link = unique_child(dest, name);
		GError *err = NULL;
		if (!g_file_peek_path(l->data) || !g_file_make_symbolic_link(link, g_file_peek_path(l->data), NULL, &err)) {
			set_status(err ? err->message : "Cannot link to this location");
			g_clear_error(&err);
		}
		g_object_unref(link);
		g_free(name);
		g_clear_object(&parent);
		g_free(base);
	}
	g_list_free_full(files, g_object_unref);
}

static gboolean on_escape(GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, gpointer dlg)
{
	(void)k;
	(void)code;
	(void)st;
	if (keyval != GDK_KEY_Escape)
		return FALSE;
	gtk_window_destroy(GTK_WINDOW(dlg));
	return TRUE;
}

static GtkWidget *dialog_window(const char *title, GtkWidget **box)
{
	auto dlg = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg), title);
	gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	*box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_top(*box, 12);
	gtk_widget_set_margin_bottom(*box, 12);
	gtk_widget_set_margin_start(*box, 12);
	gtk_widget_set_margin_end(*box, 12);
	gtk_window_set_child(GTK_WINDOW(dlg), *box);
	auto k = gtk_event_controller_key_new();
	g_signal_connect(k, "key-pressed", G_CALLBACK(on_escape), dlg);
	gtk_widget_add_controller(dlg, k);
	return dlg;
}

static GtkWidget *button_row(GtkWidget *box, const char *cancel_label, const char *ok_label, GtkWidget **ok)
{
	auto row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(row, GTK_ALIGN_END);
	auto cancel = gtk_button_new_with_mnemonic(cancel_label);
	gtk_box_append(GTK_BOX(row), cancel);
	if (ok_label) {
		*ok = gtk_button_new_with_mnemonic(ok_label);
		gtk_widget_add_css_class(*ok, "suggested-action");
		gtk_box_append(GTK_BOX(row), *ok);
	}
	gtk_box_append(GTK_BOX(box), row);
	return cancel;
}

typedef void (*name_cb)(const char *name, gpointer data);

typedef struct {
	GtkWidget *dlg, *entry;
	name_cb cb;
	gpointer data;
	GDestroyNotify destroy;
	gboolean allow_slash;
} name_prompt;

static void name_free(gpointer data)
{
	name_prompt *np = data;
	if (np->destroy)
		np->destroy(np->data);
	g_free(np);
}

static void name_ok(GtkWidget *w, gpointer data)
{
	(void)w;
	name_prompt *np = data;
	auto name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(np->entry))));
	if (*name && (np->allow_slash || !strchr(name, '/')))
		np->cb(name, np->data);
	g_free(name);
	gtk_window_destroy(GTK_WINDOW(np->dlg));
}

static void ask_name(const char *title, const char *initial, name_cb cb, gpointer data, GDestroyNotify destroy)
{
	GtkWidget *box, *ok;
	auto np = g_new0(name_prompt, 1);
	np->cb = cb;
	np->data = data;
	np->destroy = destroy;
	np->allow_slash = initial[0] == '*';
	np->dlg = dialog_window(title, &box);
	gtk_window_set_default_size(GTK_WINDOW(np->dlg), 360, -1);
	g_object_set_data_full(G_OBJECT(np->dlg), "prompt", np, name_free);
	np->entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(np->entry), initial);
	g_signal_connect(np->entry, "activate", G_CALLBACK(name_ok), np);
	gtk_box_append(GTK_BOX(box), np->entry);
	auto cancel = button_row(box, "_Cancel", "_OK", &ok);
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), np->dlg);
	g_signal_connect(ok, "clicked", G_CALLBACK(name_ok), np);
	gtk_window_present(GTK_WINDOW(np->dlg));
	auto dot = strrchr(initial, '.');
	gtk_editable_select_region(GTK_EDITABLE(np->entry), 0, dot && dot != initial ? (int)g_utf8_pointer_to_offset(initial, dot) : -1);
}

static void do_rename(const char *name, gpointer data)
{
	GError *err = NULL;
	auto f = g_file_set_display_name(data, name, NULL, &err);
	if (!f) {
		set_status(err->message);
		g_error_free(err);
	} else {
		g_object_unref(f);
	}
}

static void act_rename(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	if (files && !files->next) {
		auto base = g_file_get_basename(files->data);
		ask_name("Rename", base, do_rename, g_object_ref(files->data), g_object_unref);
		g_free(base);
	}
	g_list_free_full(files, g_object_unref);
}

static void do_mkdir(const char *name, gpointer data)
{
	GError *err = NULL;
	auto f = g_file_get_child(((pane *)data)->cwd, name);
	if (!g_file_make_directory(f, NULL, &err)) {
		set_status(err->message);
		g_error_free(err);
	}
	g_object_unref(f);
}

static void act_mkdir(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	ask_name("New folder", "New folder", do_mkdir, act, NULL);
}

static void do_newfile(const char *name, gpointer data)
{
	GError *err = NULL;
	auto f = g_file_get_child(((pane *)data)->cwd, name);
	auto out = g_file_create(f, G_FILE_CREATE_NONE, NULL, &err);
	if (!out) {
		set_status(err->message);
		g_error_free(err);
	} else {
		g_object_unref(out);
	}
	g_object_unref(f);
}

static void act_newfile(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	ask_name("New file", "New file.txt", do_newfile, act, NULL);
}

static void act_extract(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	run_job(JOB_EXTRACT, selected_files(act), NULL);
}

static void do_compress(const char *name, gpointer data)
{
	auto dest = g_file_get_child(act->cwd, name);
	if (g_file_query_exists(dest, NULL)) {
		set_status("A file with that name already exists");
		g_object_unref(dest);
		return;
	}
	run_job(JOB_COMPRESS, g_list_copy_deep(data, (GCopyFunc)(void (*)(void))g_object_ref, NULL), dest);
	g_object_unref(dest);
}

static void act_compress(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	if (!files)
		return;
	auto name = g_strdup("Archive.zip");
	if (!files->next) {
		auto base = g_file_get_basename(files->data);
		const char *dot = g_file_query_file_type(files->data, 0, NULL) == G_FILE_TYPE_DIRECTORY ? NULL : strrchr(base, '.');
		g_free(name);
		name = g_strdup_printf("%.*s.zip", dot && dot != base ? (int)(dot - base) : (int)strlen(base), base);
		g_free(base);
	}
	ask_name("Compress", name, do_compress, files, free_files);
	g_free(name);
}

static void delete_answer(GObject *src, GAsyncResult *res, gpointer data)
{
	GList *files = data;
	if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL) == 1)
		run_job(JOB_DELETE, files, NULL);
	else
		g_list_free_full(files, g_object_unref);
}

static void confirm_delete(GList *files, const char *detail)
{
	guint n = g_list_length(files);
	char *msg;
	if (n == 1) {
		auto base = g_file_get_basename(files->data);
		msg = g_strdup_printf("Delete \"%s\" permanently?", base);
		g_free(base);
	} else {
		msg = g_strdup_printf("Delete %u items permanently?", n);
	}
	auto dlg = gtk_alert_dialog_new("%s", msg);
	g_free(msg);
	gtk_alert_dialog_set_detail(dlg, detail);
	const char *buttons[] = { "_Cancel", "_Delete", NULL };
	gtk_alert_dialog_set_buttons(dlg, buttons);
	gtk_alert_dialog_set_cancel_button(dlg, 0);
	gtk_alert_dialog_set_default_button(dlg, 0);
	gtk_alert_dialog_choose(dlg, GTK_WINDOW(win), NULL, delete_answer, files);
	g_object_unref(dlg);
}

static void act_trash(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	GList *left = NULL;
	GError *err = NULL;
	guint n = 0;
	for (auto l = files; l; l = l->next) {
		if (g_file_trash(l->data, NULL, &err)) {
			n++;
			continue;
		}
		if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
			left = g_list_prepend(left, g_object_ref(l->data));
			g_clear_error(&err);
			continue;
		}
		set_status(err->message);
		g_clear_error(&err);
		break;
	}
	g_list_free_full(files, g_object_unref);
	if (left)
		confirm_delete(g_list_reverse(left), "This drive has no trash, so the files cannot be restored.");
	else if (n)
		set_status(n == 1 ? "Moved 1 item to the trash" : "Moved items to the trash");
}

static void act_delete(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	if (files)
		confirm_delete(files, "This cannot be undone.");
}

static void act_restore(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (in_trash(act))
		run_job(JOB_RESTORE, selected_files(act), NULL);
}

static void empty_answer(GObject *src, GAsyncResult *res, gpointer data)
{
	(void)data;
	if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(src), res, NULL) == 1)
		run_job(JOB_EMPTY, NULL, NULL);
}

static void act_emptytrash(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto dlg = gtk_alert_dialog_new("Empty the trash?");
	gtk_alert_dialog_set_detail(dlg, "All items in the trash will be deleted permanently.");
	const char *buttons[] = { "_Cancel", "_Empty trash", NULL };
	gtk_alert_dialog_set_buttons(dlg, buttons);
	gtk_alert_dialog_set_cancel_button(dlg, 0);
	gtk_alert_dialog_set_default_button(dlg, 0);
	gtk_alert_dialog_choose(dlg, GTK_WINDOW(win), NULL, empty_answer, NULL);
	g_object_unref(dlg);
}

typedef struct {
	GList *files;
	char *ctype;
	GtkWidget *dlg, *list, *always;
} open_with;

static void open_with_free(gpointer data)
{
	open_with *ow = data;
	g_list_free_full(ow->files, g_object_unref);
	g_free(ow->ctype);
	g_free(ow);
}

static void launch_app(open_with *ow, GAppInfo *app)
{
	GError *err = NULL;
	if (ow->ctype && gtk_check_button_get_active(GTK_CHECK_BUTTON(ow->always)))
		g_app_info_set_as_default_for_type(app, ow->ctype, NULL);
	auto ctx = gdk_display_get_app_launch_context(gtk_widget_get_display(win));
	if (!g_app_info_launch(app, ow->files, G_APP_LAUNCH_CONTEXT(ctx), &err)) {
		set_status(err->message);
		g_error_free(err);
	}
	g_object_unref(ctx);
	gtk_window_destroy(GTK_WINDOW(ow->dlg));
}

static void ow_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
	(void)list;
	launch_app(data, g_object_get_data(G_OBJECT(row), "app"));
}

static void ow_ok(GtkWidget *w, gpointer data)
{
	(void)w;
	open_with *ow = data;
	auto row = gtk_list_box_get_selected_row(GTK_LIST_BOX(ow->list));
	if (row)
		launch_app(ow, g_object_get_data(G_OBJECT(row), "app"));
}

static void ow_add(open_with *ow, GAppInfo *app, GHashTable *seen)
{
	auto id = g_app_info_get_id(app);
	if (!id || g_hash_table_contains(seen, id) || !g_app_info_should_show(app))
		return;
	g_hash_table_add(seen, g_strdup(id));
	auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_top(box, 4);
	gtk_widget_set_margin_bottom(box, 4);
	gtk_widget_set_margin_start(box, 4);
	auto image = g_app_info_get_icon(app) ? gtk_image_new_from_gicon(g_app_info_get_icon(app)) : gtk_image_new();
	gtk_image_set_pixel_size(GTK_IMAGE(image), 24);
	gtk_box_append(GTK_BOX(box), image);
	gtk_box_append(GTK_BOX(box), gtk_label_new(g_app_info_get_name(app)));
	auto row = gtk_list_box_row_new();
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
	g_object_set_data_full(G_OBJECT(row), "app", g_object_ref(app), g_object_unref);
	gtk_list_box_append(GTK_LIST_BOX(ow->list), row);
}

static void show_open_with(GList *files)
{
	GtkWidget *box, *ok;
	auto ow = g_new0(open_with, 1);
	ow->files = files;
	auto info = g_file_query_info(files->data, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, 0, NULL, NULL);
	if (info) {
		ow->ctype = g_strdup(g_file_info_get_content_type(info));
		g_object_unref(info);
	}
	ow->dlg = dialog_window("Open with", &box);
	gtk_window_set_default_size(GTK_WINDOW(ow->dlg), 380, 460);
	g_object_set_data_full(G_OBJECT(ow->dlg), "ow", ow, open_with_free);
	auto desc = ow->ctype ? g_content_type_get_description(ow->ctype) : g_strdup("this file");
	auto text = g_strdup_printf("Choose a program to open %s", desc);
	auto label = gtk_label_new(text);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_box_append(GTK_BOX(box), label);
	g_free(text);
	ow->list = gtk_list_box_new();
	g_signal_connect(ow->list, "row-activated", G_CALLBACK(ow_activated), ow);
	auto seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	auto apps = ow->ctype ? g_app_info_get_recommended_for_type(ow->ctype) : NULL;
	for (auto l = apps; l; l = l->next)
		ow_add(ow, l->data, seen);
	g_list_free_full(apps, g_object_unref);
	apps = g_app_info_get_all();
	for (auto l = apps; l; l = l->next)
		ow_add(ow, l->data, seen);
	g_list_free_full(apps, g_object_unref);
	g_hash_table_unref(seen);
	gtk_list_box_select_row(GTK_LIST_BOX(ow->list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(ow->list), 0));
	auto scroll = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ow->list);
	gtk_box_append(GTK_BOX(box), scroll);
	text = g_strdup_printf("Always use this program for %s", desc);
	ow->always = gtk_check_button_new_with_label(text);
	g_free(text);
	g_free(desc);
	gtk_widget_set_sensitive(ow->always, ow->ctype != NULL);
	gtk_box_append(GTK_BOX(box), ow->always);
	auto cancel = button_row(box, "_Cancel", "_Open", &ok);
	g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), ow->dlg);
	g_signal_connect(ok, "clicked", G_CALLBACK(ow_ok), ow);
	gtk_window_present(GTK_WINDOW(ow->dlg));
}

static void act_openwith(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	if (files)
		show_open_with(files);
}

static void act_terminal(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	in_terminal(act->cwd, NULL);
}

typedef struct {
	GFile *file;
	guint32 mode;
	GtkWidget *checks[9];
} perm_ui;

static void perm_free(gpointer data)
{
	perm_ui *pu = data;
	g_object_unref(pu->file);
	g_free(pu);
}

static void perm_toggled(GtkCheckButton *b, gpointer data)
{
	(void)b;
	perm_ui *pu = data;
	guint32 mode = pu->mode & ~0777u;
	for (int i = 0; i < 9; i++)
		if (gtk_check_button_get_active(GTK_CHECK_BUTTON(pu->checks[i])))
			mode |= 0400u >> i;
	GError *err = NULL;
	if (g_file_set_attribute_uint32(pu->file, G_FILE_ATTRIBUTE_UNIX_MODE, mode, G_FILE_QUERY_INFO_NONE, NULL, &err)) {
		pu->mode = mode;
	} else {
		set_status(err->message);
		g_error_free(err);
	}
}

static void measure_thread(GTask *task, gpointer src, gpointer data, GCancellable *c)
{
	(void)src;
	auto r = g_new0(guint64, 3);
	for (GList *l = data; l && !g_cancellable_is_cancelled(c); l = l->next) {
		guint64 b = 0, dirs = 0, files = 0;
		if (g_file_measure_disk_usage(l->data, G_FILE_MEASURE_APPARENT_SIZE, c, NULL, NULL, &b, &dirs, &files, NULL)) {
			r[0] += b;
			r[1] += dirs;
			r[2] += files;
		}
	}
	g_task_return_pointer(task, r, g_free);
}

static void measured(GObject *src, GAsyncResult *res, gpointer label)
{
	(void)src;
	guint64 *r = g_task_propagate_pointer(G_TASK(res), NULL);
	if (!r)
		return;
	auto size = g_format_size(r[0]);
	auto text = r[1] ? g_strdup_printf("%s (%" G_GUINT64_FORMAT " files, %" G_GUINT64_FORMAT " folders)", size, r[2], r[1])
			: g_strdup(size);
	gtk_label_set_text(GTK_LABEL(label), text);
	g_free(text);
	g_free(size);
	g_free(r);
}

static void measure_into(GtkWidget *dlg, GtkWidget *label, GList *files)
{
	auto c = g_cancellable_new();
	g_signal_connect_swapped(dlg, "destroy", G_CALLBACK(g_cancellable_cancel), c);
	g_object_set_data_full(G_OBJECT(dlg), "cancel", c, g_object_unref);
	auto task = g_task_new(NULL, c, measured, label);
	g_task_set_task_data(task, g_list_copy_deep(files, (GCopyFunc)(void (*)(void))g_object_ref, NULL), free_files);
	g_task_run_in_thread(task, measure_thread);
	g_object_unref(task);
}

static GtkWidget *prop_key(GtkWidget *grid, int row, const char *key)
{
	auto k = gtk_label_new(key);
	gtk_widget_add_css_class(k, "dim-label");
	gtk_label_set_xalign(GTK_LABEL(k), 1);
	gtk_label_set_yalign(GTK_LABEL(k), 0);
	gtk_grid_attach(GTK_GRID(grid), k, 0, row, 1, 1);
	return k;
}

static GtkWidget *prop_row(GtkWidget *grid, int row, const char *key, const char *value)
{
	prop_key(grid, row, key);
	auto v = gtk_label_new(value);
	gtk_label_set_xalign(GTK_LABEL(v), 0);
	gtk_label_set_selectable(GTK_LABEL(v), TRUE);
	gtk_label_set_wrap(GTK_LABEL(v), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(v), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars(GTK_LABEL(v), 40);
	gtk_widget_set_hexpand(v, TRUE);
	gtk_grid_attach(GTK_GRID(grid), v, 1, row, 1, 1);
	return v;
}

static void props_change_app(GtkWidget *b, gpointer file)
{
	auto dlg = GTK_WIDGET(gtk_widget_get_root(b));
	show_open_with(g_list_prepend(NULL, g_object_ref(file)));
	gtk_window_destroy(GTK_WINDOW(dlg));
}

static int add_permissions(GtkWidget *dlg, GtkWidget *grid, int row, GFile *file, GFileInfo *info)
{
	auto pu = g_new0(perm_ui, 1);
	pu->file = g_object_ref(file);
	pu->mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
	g_object_set_data_full(G_OBJECT(dlg), "perm", pu, perm_free);
	auto pg = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(pg), 12);
	const char *heads[] = { "Read", "Write", is_dir(info) ? "Enter" : "Run" };
	const char *who[] = { "Owner", "Group", "Others" };
	for (int c = 0; c < 3; c++)
		gtk_grid_attach(GTK_GRID(pg), gtk_label_new(heads[c]), c + 1, 0, 1, 1);
	for (int r = 0; r < 3; r++) {
		auto l = gtk_label_new(who[r]);
		gtk_label_set_xalign(GTK_LABEL(l), 0);
		gtk_grid_attach(GTK_GRID(pg), l, 0, r + 1, 1, 1);
		for (int c = 0; c < 3; c++) {
			int i = r * 3 + c;
			pu->checks[i] = gtk_check_button_new();
			gtk_widget_set_halign(pu->checks[i], GTK_ALIGN_CENTER);
			gtk_check_button_set_active(GTK_CHECK_BUTTON(pu->checks[i]), (pu->mode & (0400u >> i)) != 0);
			g_signal_connect(pu->checks[i], "toggled", G_CALLBACK(perm_toggled), pu);
			gtk_grid_attach(GTK_GRID(pg), pu->checks[i], c + 1, r + 1, 1, 1);
		}
	}
	prop_key(grid, row, "Permissions");
	gtk_grid_attach(GTK_GRID(grid), pg, 1, row, 1, 1);
	return row + 1;
}

static void show_properties(GList *files)
{
	GtkWidget *box, *ok = NULL;
	auto dlg = dialog_window("Properties", &box);
	gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);
	auto grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_box_append(GTK_BOX(box), grid);
	int row = 0;
	auto parent = g_file_get_parent(files->data);
	auto where = parent ? g_file_get_parse_name(parent) : g_strdup("");
	auto info = files->next ? NULL : g_file_query_info(files->data, "standard::*,time::*,unix::*,owner::*", 0, NULL, NULL);
	if (info) {
		auto head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		auto image = g_file_info_get_icon(info) ? gtk_image_new_from_gicon(g_file_info_get_icon(info)) : gtk_image_new();
		gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
		gtk_box_append(GTK_BOX(head), image);
		auto name = gtk_label_new(g_file_info_get_display_name(info));
		gtk_label_set_selectable(GTK_LABEL(name), TRUE);
		gtk_label_set_wrap(GTK_LABEL(name), TRUE);
		gtk_label_set_wrap_mode(GTK_LABEL(name), PANGO_WRAP_WORD_CHAR);
		gtk_widget_add_css_class(name, "title-4");
		gtk_box_append(GTK_BOX(head), name);
		gtk_box_prepend(GTK_BOX(box), head);
		auto ctype = g_file_info_get_content_type(info);
		auto desc = is_dir(info) ? g_strdup("Folder") : ctype ? g_content_type_get_description(ctype) : g_strdup("");
		prop_row(grid, row++, "Type", desc);
		g_free(desc);
		prop_row(grid, row++, "Location", where);
		auto link = g_file_query_info(files->data, "standard::symlink-target", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
		if (link && g_file_info_get_symlink_target(link))
			prop_row(grid, row++, "Link to", g_file_info_get_symlink_target(link));
		g_clear_object(&link);
		measure_into(dlg, prop_row(grid, row++, "Size", "Calculating..."), files);
		auto t = format_time(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		prop_row(grid, row++, "Modified", t);
		g_free(t);
		t = format_time(info, G_FILE_ATTRIBUTE_TIME_ACCESS);
		prop_row(grid, row++, "Accessed", t);
		g_free(t);
		auto user = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_USER);
		auto group = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_OWNER_GROUP);
		if (user) {
			t = g_strdup_printf("%s, group %s", user, group ? group : "?");
			prop_row(grid, row++, "Owner", t);
			g_free(t);
		}
		if (!is_dir(info) && ctype) {
			auto app = g_app_info_get_default_for_type(ctype, FALSE);
			prop_key(grid, row, "Opens with");
			auto hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
			gtk_box_append(GTK_BOX(hb), gtk_label_new(app ? g_app_info_get_name(app) : "Nothing"));
			auto change = gtk_button_new_with_mnemonic("C_hange...");
			g_signal_connect_data(change, "clicked", G_CALLBACK(props_change_app), g_object_ref(files->data),
					      (GClosureNotify)(void (*)(void))g_object_unref, 0);
			gtk_box_append(GTK_BOX(hb), change);
			gtk_grid_attach(GTK_GRID(grid), hb, 1, row++, 1, 1);
			g_clear_object(&app);
		}
		if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_UNIX_MODE))
			row = add_permissions(dlg, grid, row, files->data, info);
	} else {
		auto t = g_strdup_printf("%u items", g_list_length(files));
		prop_row(grid, row++, "Selected", t);
		g_free(t);
		prop_row(grid, row++, "Location", where);
		measure_into(dlg, prop_row(grid, row++, "Size", "Calculating..."), files);
	}
	g_clear_object(&info);
	g_free(where);
	g_clear_object(&parent);
	auto close = button_row(box, "_Close", NULL, &ok);
	g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), dlg);
	g_list_free_full(files, g_object_unref);
	gtk_window_present(GTK_WINDOW(dlg));
}

static void act_properties(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto files = selected_files(act);
	if (!files)
		files = g_list_prepend(NULL, g_object_ref(act->cwd));
	show_properties(files);
}

static void select_pattern(const char *pattern, gboolean select)
{
	auto folded = g_utf8_casefold(pattern, -1);
	auto spec = g_pattern_spec_new(folded);
	auto m = G_LIST_MODEL(act->selection);
	guint n = g_list_model_get_n_items(m);
	for (guint i = 0; i < n; i++) {
		GFileInfo *info = g_list_model_get_item(m, i);
		auto name = g_utf8_casefold(g_file_info_get_display_name(info), -1);
		if (g_pattern_spec_match_string(spec, name)) {
			if (select)
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(act->selection), i, FALSE);
			else
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(act->selection), i);
		}
		g_free(name);
		g_object_unref(info);
	}
	g_pattern_spec_free(spec);
	g_free(folded);
}

static void do_select(const char *pattern, gpointer data)
{
	(void)data;
	select_pattern(pattern, TRUE);
}

static void do_unselect(const char *pattern, gpointer data)
{
	(void)data;
	select_pattern(pattern, FALSE);
}

static void act_select(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	ask_name("Select files matching", "*", do_select, NULL, NULL);
}

static void act_unselect(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	ask_name("Unselect files matching", "*", do_unselect, NULL, NULL);
}

static void act_selectall(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	gtk_selection_model_select_all(GTK_SELECTION_MODEL(act->selection));
}

static void act_invert(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto m = GTK_SELECTION_MODEL(act->selection);
	guint n = g_list_model_get_n_items(G_LIST_MODEL(m));
	auto sel = gtk_selection_model_get_selection(m);
	auto inv = gtk_bitset_new_range(0, n);
	auto mask = gtk_bitset_new_range(0, n);
	gtk_bitset_subtract(inv, sel);
	gtk_selection_model_set_selection(m, inv, mask);
	gtk_bitset_unref(mask);
	gtk_bitset_unref(inv);
	gtk_bitset_unref(sel);
}

static void act_hidden(GSimpleAction *a, GVariant *p, gpointer d)
{
	(void)p;
	(void)d;
	show_hidden = !show_hidden;
	g_simple_action_set_state(a, g_variant_new_boolean(show_hidden));
	for (int i = 0; i < 2; i++)
		gtk_filter_changed(panes[i].filter, show_hidden ? GTK_FILTER_CHANGE_LESS_STRICT : GTK_FILTER_CHANGE_MORE_STRICT);
}

static void act_grid(GSimpleAction *a, GVariant *p, gpointer d)
{
	(void)p;
	(void)d;
	act->grid_mode = !act->grid_mode;
	g_simple_action_set_state(a, g_variant_new_boolean(act->grid_mode));
	gtk_stack_set_visible_child_name(GTK_STACK(act->stack), act->grid_mode ? "grid" : "list");
	gtk_widget_grab_focus(current_view(act));
}

static void act_dual(GSimpleAction *a, GVariant *p, gpointer d)
{
	(void)p;
	(void)d;
	dual = !dual;
	g_simple_action_set_state(a, g_variant_new_boolean(dual));
	if (dual && !panes[1].cwd)
		load(&panes[1], panes[0].cwd);
	gtk_widget_set_visible(panes[1].box, dual);
	set_active(!dual && act == &panes[1] ? &panes[0] : act);
	gtk_widget_grab_focus(current_view(act));
}

static void act_swap(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (!dual)
		return;
	GFile *a0 = g_object_ref(panes[0].cwd), *a1 = g_object_ref(panes[1].cwd);
	go(&panes[0], a1);
	go(&panes[1], a0);
	g_object_unref(a0);
	g_object_unref(a1);
}

static void act_back(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (!act->back)
		return;
	GFile *f = act->back->data;
	act->back = g_list_delete_link(act->back, act->back);
	act->forward = g_list_prepend(act->forward, g_object_ref(act->cwd));
	load(act, f);
	g_object_unref(f);
}

static void act_forward(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	if (!act->forward)
		return;
	GFile *f = act->forward->data;
	act->forward = g_list_delete_link(act->forward, act->forward);
	act->back = g_list_prepend(act->back, g_object_ref(act->cwd));
	load(act, f);
	g_object_unref(f);
}

static void act_up(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto parent = g_file_get_parent(act->cwd);
	if (parent) {
		go(act, parent);
		g_object_unref(parent);
	}
}

static void act_home(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto home = g_file_new_for_path(g_get_home_dir());
	go(act, home);
	g_object_unref(home);
}

static void act_location(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	gtk_widget_grab_focus(act->pathentry);
}

static void act_find(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	gtk_widget_grab_focus(searchentry);
}

static void act_close(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	gtk_window_destroy(GTK_WINDOW(win));
}

static char *bookmarks_path(void)
{
	return g_build_filename(g_get_user_config_dir(), "gtk-3.0", "bookmarks", NULL);
}

static gboolean has_bookmark(const char *text, const char *uri)
{
	gboolean found = FALSE;
	auto lines = g_strsplit(text ? text : "", "\n", -1);
	for (char **l = lines; *l && !found; l++) {
		auto parts = g_strsplit(*l, " ", 2);
		found = parts[0] && !strcmp(parts[0], uri);
		g_strfreev(parts);
	}
	g_strfreev(lines);
	return found;
}

static void act_bookmark(GSimpleAction *a, GVariant *p, gpointer d)
{
	UNUSED_ACTION_ARGS;
	auto path = bookmarks_path();
	auto uri = g_file_get_uri(act->cwd);
	char *text = NULL;
	g_file_get_contents(path, &text, NULL, NULL);
	if (!has_bookmark(text, uri)) {
		auto dir = g_path_get_dirname(path);
		g_mkdir_with_parents(dir, 0700);
		g_free(dir);
		gsize len = text ? strlen(text) : 0;
		auto out = g_strdup_printf("%s%s%s\n", text ? text : "", len && text[len - 1] != '\n' ? "\n" : "", uri);
		g_file_set_contents(path, out, -1, NULL);
		g_free(out);
		set_status("Bookmark added");
	}
	g_free(text);
	g_free(uri);
	g_free(path);
}

static gboolean rebuild_sidebar(gpointer data);

static void schedule_sidebar(void)
{
	if (!sidebar_idle)
		sidebar_idle = g_idle_add(rebuild_sidebar, NULL);
}

static void remove_bookmark(GtkWidget *b, gpointer uri)
{
	(void)b;
	auto path = bookmarks_path();
	char *text = NULL;
	if (g_file_get_contents(path, &text, NULL, NULL)) {
		auto out = g_string_new(NULL);
		auto lines = g_strsplit(text, "\n", -1);
		for (char **l = lines; *l; l++) {
			auto parts = g_strsplit(*l, " ", 2);
			if (**l && !(parts[0] && !strcmp(parts[0], uri)))
				g_string_append_printf(out, "%s\n", *l);
			g_strfreev(parts);
		}
		g_strfreev(lines);
		g_file_set_contents(path, out->str, -1, NULL);
		g_string_free(out, TRUE);
	}
	g_free(text);
	g_free(path);
	schedule_sidebar();
}

typedef struct {
	char *path, *name;
} eject_req;

static void eject_free(eject_req *r)
{
	g_free(r->path);
	g_free(r->name);
	g_free(r);
}

static void ejected(GPid pid, int status, gpointer data)
{
	eject_req *r = data;
	g_spawn_close_pid(pid);
	char *msg;
	if (g_spawn_check_wait_status(status, NULL)) {
		char *argv[] = { "doas", "-n", "rmdir", r->path, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		msg = g_strdup_printf("It is now safe to remove %s", r->name);
	} else {
		msg = g_strdup_printf("%s is still in use. Close programs that use it first.", r->name);
	}
	set_status(msg);
	g_free(msg);
	eject_free(r);
	schedule_sidebar();
}

static void eject(GtkWidget *b, gpointer mount)
{
	(void)b;
	auto root = g_mount_get_root(mount);
	auto r = g_new0(eject_req, 1);
	r->path = g_file_get_path(root);
	r->name = g_mount_get_name(mount);
	for (int i = 0; i < 2; i++) {
		if (panes[i].cwd && (g_file_equal(panes[i].cwd, root) || g_file_has_prefix(panes[i].cwd, root))) {
			auto home = g_file_new_for_path(g_get_home_dir());
			go(&panes[i], home);
			g_object_unref(home);
		}
	}
	g_object_unref(root);
	char *argv[] = { "doas", "-n", "systemd-mount", "--umount", r->path, NULL };
	GPid pid;
	GError *err = NULL;
	if (!r->path || !g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL,
				       NULL, NULL, &pid, &err)) {
		set_status(err ? err->message : "Cannot eject this drive");
		g_clear_error(&err);
		eject_free(r);
		return;
	}
	set_status("Ejecting...");
	g_child_watch_add(pid, ejected, r);
}

static void side_header(const char *text)
{
	auto label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_widget_add_css_class(label, "dim-label");
	gtk_widget_add_css_class(label, "caption-heading");
	gtk_widget_set_margin_top(label, 10);
	gtk_widget_set_margin_start(label, 8);
	gtk_widget_set_margin_bottom(label, 2);
	auto row = gtk_list_box_row_new();
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
	gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
	gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
	gtk_list_box_append(GTK_LIST_BOX(sidebar), row);
}

static void side_row(const char *icon_name, GIcon *icon, const char *label, GFile *file, GtkWidget *extra)
{
	auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_start(box, 8);
	gtk_widget_set_margin_end(box, 4);
	gtk_widget_set_margin_top(box, 3);
	gtk_widget_set_margin_bottom(box, 3);
	gtk_box_append(GTK_BOX(box), icon ? gtk_image_new_from_gicon(icon) : gtk_image_new_from_icon_name(icon_name));
	auto l = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(l, TRUE);
	gtk_box_append(GTK_BOX(box), l);
	if (extra)
		gtk_box_append(GTK_BOX(box), extra);
	auto row = gtk_list_box_row_new();
	gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
	g_object_set_data_full(G_OBJECT(row), "file", g_object_ref(file), g_object_unref);
	auto tip = g_file_get_parse_name(file);
	gtk_widget_set_tooltip_text(row, tip);
	g_free(tip);
	gtk_list_box_append(GTK_LIST_BOX(sidebar), row);
}

static void side_path(const char *icon, const char *label, const char *path)
{
	auto f = g_file_new_for_path(path);
	side_row(icon, NULL, label, f, NULL);
	g_object_unref(f);
}

static GtkWidget *small_button(const char *icon, const char *tip)
{
	auto b = gtk_button_new_from_icon_name(icon);
	gtk_widget_add_css_class(b, "flat");
	gtk_widget_set_tooltip_text(b, tip);
	gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
	return b;
}

static void side_bookmarks(void)
{
	auto bpath = bookmarks_path();
	char *text = NULL;
	if (!g_file_get_contents(bpath, &text, NULL, NULL)) {
		g_free(bpath);
		return;
	}
	gboolean header = FALSE;
	auto lines = g_strsplit(text, "\n", -1);
	for (char **l = lines; *l; l++) {
		if (!**l)
			continue;
		auto parts = g_strsplit(*l, " ", 2);
		auto f = g_file_new_for_uri(parts[0]);
		auto label = parts[1] ? g_strdup(parts[1]) : g_file_get_basename(f);
		if (!header) {
			side_header("Bookmarks");
			header = TRUE;
		}
		auto rm = small_button("list-remove-symbolic", "Remove this bookmark");
		g_signal_connect_data(rm, "clicked", G_CALLBACK(remove_bookmark), g_strdup(parts[0]),
				      (GClosureNotify)(void (*)(void))g_free, 0);
		side_row("folder-symbolic", NULL, label ? label : parts[0], f, rm);
		g_free(label);
		g_object_unref(f);
		g_strfreev(parts);
	}
	g_strfreev(lines);
	g_free(text);
	g_free(bpath);
}

static void side_drives(void)
{
	auto mounts = g_volume_monitor_get_mounts(volumes);
	gboolean header = FALSE;
	for (auto l = mounts; l; l = l->next) {
		GMount *m = l->data;
		if (g_mount_is_shadowed(m))
			continue;
		auto root = g_mount_get_root(m);
		auto path = g_file_peek_path(root);
		if (!header) {
			side_header("Drives");
			header = TRUE;
		}
		GtkWidget *ej = NULL;
		if (path && g_str_has_prefix(path, "/media/")) {
			ej = small_button("media-eject-symbolic", "Eject");
			g_signal_connect_data(ej, "clicked", G_CALLBACK(eject), g_object_ref(m),
					      (GClosureNotify)(void (*)(void))g_object_unref, 0);
		}
		auto name = g_mount_get_name(m);
		auto icon = g_mount_get_symbolic_icon(m);
		side_row("drive-removable-media-symbolic", icon, name, root, ej);
		g_clear_object(&icon);
		g_free(name);
		g_object_unref(root);
	}
	g_list_free_full(mounts, g_object_unref);
}

static gboolean rebuild_sidebar(gpointer data)
{
	(void)data;
	static const struct {
		GUserDirectory dir;
		const char *icon;
	} special[] = {
		{ G_USER_DIRECTORY_DESKTOP, "user-desktop-symbolic" },
		{ G_USER_DIRECTORY_DOCUMENTS, "folder-documents-symbolic" },
		{ G_USER_DIRECTORY_DOWNLOAD, "folder-download-symbolic" },
		{ G_USER_DIRECTORY_MUSIC, "folder-music-symbolic" },
		{ G_USER_DIRECTORY_PICTURES, "folder-pictures-symbolic" },
		{ G_USER_DIRECTORY_VIDEOS, "folder-videos-symbolic" },
	};
	sidebar_idle = 0;
	gtk_list_box_remove_all(GTK_LIST_BOX(sidebar));
	side_header("Places");
	auto home = g_get_home_dir();
	side_path("user-home-symbolic", "Home", home);
	for (size_t i = 0; i < G_N_ELEMENTS(special); i++) {
		auto path = g_get_user_special_dir(special[i].dir);
		if (path && strcmp(path, home) && g_file_test(path, G_FILE_TEST_IS_DIR)) {
			auto base = g_path_get_basename(path);
			side_path(special[i].icon, base, path);
			g_free(base);
		}
	}
	side_path("drive-harddisk-symbolic", "File system", "/");
	auto trash = trash_dir();
	side_row("user-trash-symbolic", NULL, "Trash", trash, NULL);
	g_object_unref(trash);
	side_bookmarks();
	side_drives();
	sync_sidebar();
	return G_SOURCE_REMOVE;
}

static void on_side_activate(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
	(void)list;
	(void)data;
	GFile *f = g_object_get_data(G_OBJECT(row), "file");
	if (!f)
		return;
	if (g_file_query_file_type(f, 0, NULL) != G_FILE_TYPE_DIRECTORY) {
		auto trash = trash_dir();
		gboolean made = g_file_equal(f, trash) && g_file_make_directory_with_parents(f, NULL, NULL);
		g_object_unref(trash);
		if (!made) {
			set_status("This folder does not exist");
			return;
		}
	}
	go(act, f);
	gtk_widget_grab_focus(current_view(act));
}

static gboolean drop_files(GtkDropTarget *t, const GValue *v, GFile *dest)
{
	if (!dest)
		return FALSE;
	gboolean move = (gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(t)) & GDK_SHIFT_MASK) != 0;
	auto files = from_file_list(v);
	GList *keep = NULL;
	for (auto l = files; l; l = l->next) {
		auto parent = g_file_get_parent(l->data);
		if (!g_file_equal(l->data, dest) && !(parent && g_file_equal(parent, dest)))
			keep = g_list_prepend(keep, g_object_ref(l->data));
		g_clear_object(&parent);
	}
	g_list_free_full(files, g_object_unref);
	if (!keep)
		return FALSE;
	run_job(move ? JOB_MOVE : JOB_COPY, g_list_reverse(keep), dest);
	return TRUE;
}

static gboolean on_side_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer data)
{
	(void)x;
	(void)data;
	auto row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(sidebar), (int)y);
	return row && drop_files(t, v, g_object_get_data(G_OBJECT(row), "file"));
}

static GtkListItem *item_at(pane *p, double x, double y)
{
	auto w = gtk_widget_pick(p->stack, x, y, GTK_PICK_DEFAULT);
	GtkListItem *item = NULL;
	for (; w && w != p->stack && !item; w = gtk_widget_get_parent(w))
		item = g_object_get_data(G_OBJECT(w), "item");
	return item;
}

static gboolean on_drop(GtkDropTarget *t, const GValue *v, double x, double y, gpointer data)
{
	pane *p = data;
	auto item = item_at(p, x, y);
	auto dest = p->searching ? NULL : p->cwd;
	if (item) {
		GFileInfo *info = gtk_list_item_get_item(item);
		if (info && is_dir(info))
			dest = info_file(info);
	}
	return drop_files(t, v, dest);
}

static GdkContentProvider *drag_prepare(GtkDragSource *src, double x, double y, gpointer item)
{
	(void)x;
	(void)y;
	pane *p = g_object_get_data(G_OBJECT(src), "pane");
	guint pos = gtk_list_item_get_position(item);
	if (pos == GTK_INVALID_LIST_POSITION)
		return NULL;
	if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(p->selection), pos))
		gtk_selection_model_select_item(GTK_SELECTION_MODEL(p->selection), pos, TRUE);
	auto files = selected_files(p);
	auto sl = to_slist(files);
	auto fl = gdk_file_list_new_from_list(sl);
	auto cp = gdk_content_provider_new_typed(GDK_TYPE_FILE_LIST, fl);
	g_boxed_free(GDK_TYPE_FILE_LIST, fl);
	g_slist_free(sl);
	g_list_free_full(files, g_object_unref);
	return cp;
}

static void add_drag_source(pane *p, GtkWidget *w, GtkListItem *item)
{
	auto src = gtk_drag_source_new();
	gtk_drag_source_set_actions(src, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_object_set_data(G_OBJECT(src), "pane", p);
	g_signal_connect(src, "prepare", G_CALLBACK(drag_prepare), item);
	gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(src));
}

static void setup_icon(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
	(void)data;
	pane *p = g_object_get_data(G_OBJECT(f), "pane");
	auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_size_request(box, 96, -1);
	auto image = gtk_image_new();
	gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
	gtk_box_append(GTK_BOX(box), image);
	auto label = gtk_label_new(NULL);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_lines(GTK_LABEL(label), 2);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
	gtk_box_append(GTK_BOX(box), label);
	add_drag_source(p, box, item);
	g_object_set_data(G_OBJECT(box), "item", item);
	gtk_list_item_set_child(item, box);
}

static void bind_icon(GtkSignalListItemFactory *f, GtkListItem *item, gpointer data)
{
	(void)f;
	(void)data;
	GFileInfo *info = gtk_list_item_get_item(item);
	auto box = gtk_list_item_get_child(item);
	gtk_image_set_from_gicon(GTK_IMAGE(gtk_widget_get_first_child(box)), g_file_info_get_icon(info));
	gtk_label_set_text(GTK_LABEL(gtk_widget_get_last_child(box)), g_file_info_get_display_name(info));
	auto tip = g_file_get_parse_name(info_file(info));
	gtk_widget_set_tooltip_text(box, tip);
	g_free(tip);
}

static void setup_cell(GtkSignalListItemFactory *f, GtkListItem *item, gpointer col)
{
	pane *p = g_object_get_data(G_OBJECT(f), "pane");
	auto label = gtk_label_new(NULL);
	auto child = label;
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	if (GPOINTER_TO_INT(col) == COL_NAME) {
		child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		gtk_box_append(GTK_BOX(child), gtk_image_new());
		gtk_label_set_xalign(GTK_LABEL(label), 0);
		gtk_box_append(GTK_BOX(child), label);
		add_drag_source(p, child, item);
	} else {
		gtk_label_set_xalign(GTK_LABEL(label), GPOINTER_TO_INT(col) == COL_SIZE ? 1 : 0);
	}
	g_object_set_data(G_OBJECT(child), "item", item);
	gtk_list_item_set_child(item, child);
}

static void bind_cell(GtkSignalListItemFactory *f, GtkListItem *item, gpointer col)
{
	(void)f;
	GFileInfo *info = gtk_list_item_get_item(item);
	auto child = gtk_list_item_get_child(item);
	char *s;
	switch (GPOINTER_TO_INT(col)) {
	case COL_NAME:
		gtk_image_set_from_gicon(GTK_IMAGE(gtk_widget_get_first_child(child)), g_file_info_get_icon(info));
		gtk_label_set_text(GTK_LABEL(gtk_widget_get_last_child(child)), g_file_info_get_display_name(info));
		s = g_file_get_parse_name(info_file(info));
		gtk_widget_set_tooltip_text(child, s);
		g_free(s);
		return;
	case COL_SIZE:
		s = is_dir(info) ? g_strdup("") : g_format_size(g_file_info_get_size(info));
		break;
	case COL_TYPE: {
		auto t = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
		s = is_dir(info) ? g_strdup("Folder") : t ? g_content_type_get_description(t) : g_strdup("");
		break;
	}
	default:
		s = format_time(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		if (strlen(s) > 16)
			s[16] = 0;
		break;
	}
	gtk_label_set_text(GTK_LABEL(child), s);
	g_free(s);
}

static int cmp_dirs(gconstpointer a, gconstpointer b, gpointer d)
{
	(void)d;
	gboolean da = is_dir((GFileInfo *)a), db = is_dir((GFileInfo *)b);
	return da == db ? 0 : da ? -1 : 1;
}

static int cmp_name(gconstpointer a, gconstpointer b, gpointer d)
{
	(void)d;
	auto ka = g_utf8_collate_key_for_filename(g_file_info_get_display_name((GFileInfo *)a), -1);
	auto kb = g_utf8_collate_key_for_filename(g_file_info_get_display_name((GFileInfo *)b), -1);
	int r = strcmp(ka, kb);
	g_free(ka);
	g_free(kb);
	return r;
}

static int cmp_size(gconstpointer a, gconstpointer b, gpointer d)
{
	goffset sa = g_file_info_get_size((GFileInfo *)a), sb = g_file_info_get_size((GFileInfo *)b);
	return sa < sb ? -1 : sa > sb ? 1 : cmp_name(a, b, d);
}

static int cmp_type(gconstpointer a, gconstpointer b, gpointer d)
{
	auto ta = g_file_info_get_attribute_string((GFileInfo *)a, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
	auto tb = g_file_info_get_attribute_string((GFileInfo *)b, G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE);
	int r = g_strcmp0(ta, tb);
	return r ? r : cmp_name(a, b, d);
}

static int cmp_time(gconstpointer a, gconstpointer b, gpointer d)
{
	guint64 ta = g_file_info_get_attribute_uint64((GFileInfo *)a, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	guint64 tb = g_file_info_get_attribute_uint64((GFileInfo *)b, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	return ta < tb ? -1 : ta > tb ? 1 : cmp_name(a, b, d);
}

static GtkColumnViewColumn *add_column(pane *p, const char *title, int col, gboolean expand, int width, GCompareDataFunc cmp)
{
	auto f = gtk_signal_list_item_factory_new();
	g_object_set_data(G_OBJECT(f), "pane", p);
	g_signal_connect(f, "setup", G_CALLBACK(setup_cell), GINT_TO_POINTER(col));
	g_signal_connect(f, "bind", G_CALLBACK(bind_cell), GINT_TO_POINTER(col));
	auto c = gtk_column_view_column_new(title, f);
	gtk_column_view_column_set_expand(c, expand);
	gtk_column_view_column_set_resizable(c, TRUE);
	if (width > 0)
		gtk_column_view_column_set_fixed_width(c, width);
	auto sorter = GTK_SORTER(gtk_custom_sorter_new(cmp, NULL, NULL));
	gtk_column_view_column_set_sorter(c, sorter);
	g_object_unref(sorter);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(p->view), c);
	g_object_unref(c);
	return c;
}

static void on_menu(GtkGestureClick *g, int n, double x, double y, gpointer data)
{
	(void)n;
	pane *p = data;
	if (act != p)
		set_active(p);
	auto item = item_at(p, x, y);
	if (item) {
		guint pos = gtk_list_item_get_position(item);
		if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(p->selection), pos))
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(p->selection), pos, TRUE);
	} else {
		gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(p->selection));
	}
	GtkWidget *menu = g_object_get_data(G_OBJECT(p->stack), "menu");
	gtk_popover_set_pointing_to(GTK_POPOVER(menu), &(GdkRectangle){ (int)x, (int)y, 1, 1 });
	gtk_popover_popup(GTK_POPOVER(menu));
	gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
}

static void on_path(GtkEntry *e, gpointer data)
{
	pane *p = data;
	auto text = gtk_editable_get_text(GTK_EDITABLE(e));
	auto path = text[0] == '~' ? g_build_filename(g_get_home_dir(), text + 1, NULL) : g_strdup(text);
	auto f = g_file_parse_name(path);
	g_free(path);
	if (g_file_query_file_type(f, 0, NULL) == G_FILE_TYPE_DIRECTORY) {
		go(p, f);
		gtk_widget_grab_focus(current_view(p));
	} else {
		set_status("No such folder");
	}
	g_object_unref(f);
}

static gboolean match(gpointer item, gpointer data)
{
	(void)data;
	return show_hidden || !(g_file_info_get_is_hidden(item) || g_file_info_get_is_backup(item));
}

static void on_items_changed(GListModel *m, guint pos, guint removed, guint added, gpointer data)
{
	(void)m;
	(void)pos;
	(void)removed;
	(void)added;
	if (data == act)
		update_status();
}

static void on_selection_changed(GtkSelectionModel *m, guint pos, guint n, gpointer data)
{
	(void)m;
	(void)pos;
	(void)n;
	if (data == act) {
		update_status();
		update_actions();
	}
}

static void on_dir_error(GObject *o, GParamSpec *ps, gpointer data)
{
	(void)o;
	(void)ps;
	if (data == act)
		update_status();
}

static void on_focus(GtkEventControllerFocus *c, gpointer data)
{
	(void)c;
	if (act != data)
		set_active(data);
}

static gboolean on_pane_key(GtkEventControllerKey *k, guint keyval, guint code, GdkModifierType st, gpointer data)
{
	(void)k;
	(void)code;
	(void)data;
	if (keyval != GDK_KEY_Tab || (st & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) || !dual)
		return FALSE;
	set_active(other());
	gtk_widget_grab_focus(current_view(act));
	return TRUE;
}

static const struct {
	const char *trigger, *action;
} pane_keys[] = {
	{ "<Control>x", "win.cut" },
	{ "<Control>c", "win.copy" },
	{ "<Control>v", "win.paste" },
	{ "F2", "win.rename" },
	{ "F3", "win.view" },
	{ "F4", "win.edit" },
	{ "F5", "win.f5" },
	{ "F6", "win.f6" },
	{ "F7", "win.mkdir" },
	{ "Delete|F8|KP_Delete", "win.trash" },
	{ "<Shift>Delete|<Shift>F8", "win.delete" },
	{ "BackSpace", "win.back" },
	{ "plus|KP_Add", "win.select" },
	{ "backslash|KP_Subtract|minus", "win.unselect" },
	{ "asterisk|KP_Multiply", "win.invert" },
	{ "<Alt>Return", "win.properties" },
};

static void build_pane(pane *p, GMenuModel *menu)
{
	p->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_hexpand(p->box, TRUE);
	p->pathentry = gtk_entry_new();
	gtk_widget_set_margin_start(p->pathentry, 4);
	gtk_widget_set_margin_end(p->pathentry, 4);
	gtk_widget_set_margin_bottom(p->pathentry, 4);
	g_signal_connect(p->pathentry, "activate", G_CALLBACK(on_path), p);
	gtk_box_append(GTK_BOX(p->box), p->pathentry);

	p->dirlist = gtk_directory_list_new(ATTRS, NULL);
	g_signal_connect(p->dirlist, "notify::error", G_CALLBACK(on_dir_error), p);
	p->results = g_list_store_new(G_TYPE_FILE_INFO);
	p->filter = GTK_FILTER(gtk_custom_filter_new(match, NULL, NULL));
	p->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(p->dirlist)), g_object_ref(p->filter));
	g_signal_connect(p->filtered, "items-changed", G_CALLBACK(on_items_changed), p);

	p->view = gtk_column_view_new(NULL);
	gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(p->view), FALSE);
	g_signal_connect(p->view, "activate", G_CALLBACK(on_activate), p);
	auto name = add_column(p, "Name", COL_NAME, TRUE, -1, cmp_name);
	add_column(p, "Size", COL_SIZE, FALSE, 90, cmp_size);
	add_column(p, "Type", COL_TYPE, FALSE, 150, cmp_type);
	add_column(p, "Modified", COL_TIME, FALSE, 140, cmp_time);
	auto sorter = gtk_multi_sorter_new();
	gtk_multi_sorter_append(sorter, GTK_SORTER(gtk_custom_sorter_new(cmp_dirs, NULL, NULL)));
	gtk_multi_sorter_append(sorter, g_object_ref(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(p->view))));
	auto sorted = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(p->filtered)), GTK_SORTER(sorter));
	p->selection = gtk_multi_selection_new(G_LIST_MODEL(sorted));
	g_signal_connect(p->selection, "selection-changed", G_CALLBACK(on_selection_changed), p);
	gtk_column_view_set_model(GTK_COLUMN_VIEW(p->view), GTK_SELECTION_MODEL(p->selection));
	gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(p->view), name, GTK_SORT_ASCENDING);

	auto icons = gtk_signal_list_item_factory_new();
	g_object_set_data(G_OBJECT(icons), "pane", p);
	g_signal_connect(icons, "setup", G_CALLBACK(setup_icon), NULL);
	g_signal_connect(icons, "bind", G_CALLBACK(bind_icon), NULL);
	p->grid = gtk_grid_view_new(GTK_SELECTION_MODEL(g_object_ref(p->selection)), icons);
	gtk_grid_view_set_max_columns(GTK_GRID_VIEW(p->grid), 64);
	g_signal_connect(p->grid, "activate", G_CALLBACK(on_activate), p);

	p->stack = gtk_stack_new();
	gtk_widget_set_vexpand(p->stack, TRUE);
	auto scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->view);
	gtk_stack_add_named(GTK_STACK(p->stack), scroll, "list");
	scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->grid);
	gtk_stack_add_named(GTK_STACK(p->stack), scroll, "grid");
	gtk_stack_set_visible_child_name(GTK_STACK(p->stack), "list");
	gtk_box_append(GTK_BOX(p->box), p->stack);

	auto popover = gtk_popover_menu_new_from_model(menu);
	gtk_widget_set_parent(popover, p->stack);
	gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
	gtk_widget_set_halign(popover, GTK_ALIGN_START);
	g_object_set_data(G_OBJECT(p->stack), "menu", popover);
	auto click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
	g_signal_connect(click, "pressed", G_CALLBACK(on_menu), p);
	gtk_widget_add_controller(p->stack, GTK_EVENT_CONTROLLER(click));

	auto drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect(drop, "drop", G_CALLBACK(on_drop), p);
	gtk_widget_add_controller(p->stack, GTK_EVENT_CONTROLLER(drop));

	auto shortcuts = gtk_shortcut_controller_new();
	for (size_t i = 0; i < G_N_ELEMENTS(pane_keys); i++)
		gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_shortcut_trigger_parse_string(pane_keys[i].trigger), gtk_named_action_new(pane_keys[i].action)));
	gtk_widget_add_controller(p->stack, shortcuts);
	auto keys = gtk_event_controller_key_new();
	gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
	g_signal_connect(keys, "key-pressed", G_CALLBACK(on_pane_key), p);
	gtk_widget_add_controller(p->stack, keys);
	auto focus = gtk_event_controller_focus_new();
	g_signal_connect(focus, "enter", G_CALLBACK(on_focus), p);
	gtk_widget_add_controller(p->box, focus);
}

static GtkWidget *tool_button(const char *icon, const char *tip, const char *action)
{
	auto b = gtk_button_new_from_icon_name(icon);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
	return b;
}

static GtkWidget *tool_toggle(const char *icon, const char *tip, const char *action)
{
	auto b = gtk_toggle_button_new();
	gtk_button_set_icon_name(GTK_BUTTON(b), icon);
	gtk_widget_set_tooltip_text(b, tip);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(b), action);
	return b;
}

static void menu_add(GMenu *m, const char *label, const char *action, const char *accel, gboolean hide)
{
	auto item = g_menu_item_new(label, action);
	if (accel)
		g_menu_item_set_attribute(item, "accel", "s", accel);
	if (hide)
		g_menu_item_set_attribute(item, "hidden-when", "s", "action-disabled");
	g_menu_append_item(m, item);
	g_object_unref(item);
}

static GMenu *section(GMenu *menu)
{
	auto sec = g_menu_new();
	g_menu_append_section(menu, NULL, G_MENU_MODEL(sec));
	g_object_unref(sec);
	return sec;
}

static GMenuModel *context_menu(void)
{
	auto menu = g_menu_new();
	auto s = section(menu);
	menu_add(s, "Open", "win.open", "Return", TRUE);
	menu_add(s, "Open with...", "win.openwith", NULL, TRUE);
	menu_add(s, "View", "win.view", "F3", TRUE);
	menu_add(s, "Edit", "win.edit", "F4", TRUE);
	s = section(menu);
	menu_add(s, "Cut", "win.cut", "<Control>x", TRUE);
	menu_add(s, "Copy", "win.copy", "<Control>c", TRUE);
	menu_add(s, "Paste", "win.paste", "<Control>v", FALSE);
	menu_add(s, "Copy to other pane", "win.copyto", "F5", TRUE);
	menu_add(s, "Move to other pane", "win.moveto", "F6", TRUE);
	menu_add(s, "Create link", "win.link", NULL, TRUE);
	s = section(menu);
	menu_add(s, "Extract here", "win.extract", NULL, TRUE);
	menu_add(s, "Compress...", "win.compress", NULL, TRUE);
	s = section(menu);
	menu_add(s, "Rename...", "win.rename", "F2", TRUE);
	menu_add(s, "Restore", "win.restore", NULL, TRUE);
	menu_add(s, "Move to trash", "win.trash", "Delete", TRUE);
	menu_add(s, "Delete permanently", "win.delete", "<Shift>Delete", TRUE);
	menu_add(s, "Empty trash", "win.emptytrash", NULL, TRUE);
	s = section(menu);
	menu_add(s, "New folder...", "win.mkdir", "F7", TRUE);
	menu_add(s, "New file...", "win.newfile", "<Control><Alt>n", TRUE);
	menu_add(s, "Open a terminal here", "win.terminal", "<Control><Shift>t", FALSE);
	s = section(menu);
	menu_add(s, "Properties", "win.properties", "<Alt>Return", FALSE);
	return G_MENU_MODEL(menu);
}

static GMenuModel *main_menu(void)
{
	auto menu = g_menu_new();
	auto s = section(menu);
	menu_add(s, "Show hidden files", "win.hidden", "<Control>h", FALSE);
	menu_add(s, "Show as icons", "win.grid", "<Control>g", FALSE);
	menu_add(s, "Two panes", "win.dual", "F9", FALSE);
	menu_add(s, "Swap panes", "win.swap", "<Control>u", TRUE);
	s = section(menu);
	menu_add(s, "Select all", "win.selectall", "<Control>a", FALSE);
	menu_add(s, "Select by pattern...", "win.select", "plus", FALSE);
	menu_add(s, "Unselect by pattern...", "win.unselect", "backslash", FALSE);
	menu_add(s, "Invert selection", "win.invert", "asterisk", FALSE);
	s = section(menu);
	menu_add(s, "Find files", "win.find", "<Control>f", FALSE);
	menu_add(s, "Bookmark this folder", "win.bookmark", "<Control>d", FALSE);
	menu_add(s, "Reload", "win.reload", "<Control>r", FALSE);
	menu_add(s, "Empty trash", "win.emptytrash", NULL, TRUE);
	s = section(menu);
	menu_add(s, "Close", "win.close", "<Control>w", FALSE);
	return G_MENU_MODEL(menu);
}

static const GActionEntry actions[] = {
	{ "open", act_open, NULL, NULL, NULL, { 0 } },
	{ "openwith", act_openwith, NULL, NULL, NULL, { 0 } },
	{ "view", act_view, NULL, NULL, NULL, { 0 } },
	{ "edit", act_edit, NULL, NULL, NULL, { 0 } },
	{ "cut", act_cut, NULL, NULL, NULL, { 0 } },
	{ "copy", act_copy, NULL, NULL, NULL, { 0 } },
	{ "paste", act_paste, NULL, NULL, NULL, { 0 } },
	{ "copyto", act_copyto, NULL, NULL, NULL, { 0 } },
	{ "moveto", act_moveto, NULL, NULL, NULL, { 0 } },
	{ "f5", act_f5, NULL, NULL, NULL, { 0 } },
	{ "f6", act_f6, NULL, NULL, NULL, { 0 } },
	{ "link", act_link, NULL, NULL, NULL, { 0 } },
	{ "extract", act_extract, NULL, NULL, NULL, { 0 } },
	{ "compress", act_compress, NULL, NULL, NULL, { 0 } },
	{ "rename", act_rename, NULL, NULL, NULL, { 0 } },
	{ "trash", act_trash, NULL, NULL, NULL, { 0 } },
	{ "delete", act_delete, NULL, NULL, NULL, { 0 } },
	{ "restore", act_restore, NULL, NULL, NULL, { 0 } },
	{ "emptytrash", act_emptytrash, NULL, NULL, NULL, { 0 } },
	{ "mkdir", act_mkdir, NULL, NULL, NULL, { 0 } },
	{ "newfile", act_newfile, NULL, NULL, NULL, { 0 } },
	{ "terminal", act_terminal, NULL, NULL, NULL, { 0 } },
	{ "properties", act_properties, NULL, NULL, NULL, { 0 } },
	{ "selectall", act_selectall, NULL, NULL, NULL, { 0 } },
	{ "select", act_select, NULL, NULL, NULL, { 0 } },
	{ "unselect", act_unselect, NULL, NULL, NULL, { 0 } },
	{ "invert", act_invert, NULL, NULL, NULL, { 0 } },
	{ "hidden", act_hidden, NULL, "false", NULL, { 0 } },
	{ "grid", act_grid, NULL, "false", NULL, { 0 } },
	{ "dual", act_dual, NULL, "false", NULL, { 0 } },
	{ "swap", act_swap, NULL, NULL, NULL, { 0 } },
	{ "back", act_back, NULL, NULL, NULL, { 0 } },
	{ "forward", act_forward, NULL, NULL, NULL, { 0 } },
	{ "up", act_up, NULL, NULL, NULL, { 0 } },
	{ "home", act_home, NULL, NULL, NULL, { 0 } },
	{ "reload", act_reload, NULL, NULL, NULL, { 0 } },
	{ "location", act_location, NULL, NULL, NULL, { 0 } },
	{ "find", act_find, NULL, NULL, NULL, { 0 } },
	{ "bookmark", act_bookmark, NULL, NULL, NULL, { 0 } },
	{ "close", act_close, NULL, NULL, NULL, { 0 } },
};

static const struct {
	const char *action;
	const char *accels[4];
} accels[] = {
	{ "win.mkdir", { "<Control><Shift>n", NULL } },
	{ "win.newfile", { "<Control><Alt>n", NULL } },
	{ "win.terminal", { "<Control><Shift>t", NULL } },
	{ "win.hidden", { "<Control>h", NULL } },
	{ "win.grid", { "<Control>g", NULL } },
	{ "win.dual", { "F9", NULL } },
	{ "win.swap", { "<Control>u", NULL } },
	{ "win.back", { "<Alt>Left", NULL } },
	{ "win.forward", { "<Alt>Right", NULL } },
	{ "win.up", { "<Alt>Up", NULL } },
	{ "win.home", { "<Alt>Home", NULL } },
	{ "win.reload", { "<Control>r", NULL } },
	{ "win.location", { "<Control>l", NULL } },
	{ "win.find", { "<Control>f", NULL } },
	{ "win.bookmark", { "<Control>d", NULL } },
	{ "win.close", { "<Control>w", "<Control>q", "F10", NULL } },
};

static void on_search(GtkSearchEntry *e, gpointer data)
{
	(void)data;
	start_search(act, gtk_editable_get_text(GTK_EDITABLE(e)));
	gtk_widget_grab_focus(current_view(act));
}

static void on_search_changed(GtkSearchEntry *e, gpointer data)
{
	(void)data;
	if (!*gtk_editable_get_text(GTK_EDITABLE(e)) && act->searching)
		start_search(act, "");
}

static void on_stop_search(GtkSearchEntry *e, gpointer data)
{
	(void)data;
	gtk_editable_set_text(GTK_EDITABLE(e), "");
	start_search(act, "");
	gtk_widget_grab_focus(current_view(act));
}

static void on_cancel(GtkWidget *b, gpointer data)
{
	(void)b;
	(void)data;
	if (job_cancel)
		g_cancellable_cancel(job_cancel);
}

static GtkWidget *build_toolbar(void)
{
	auto bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_widget_set_margin_top(bar, 6);
	gtk_widget_set_margin_bottom(bar, 6);
	gtk_widget_set_margin_start(bar, 6);
	gtk_widget_set_margin_end(bar, 6);
	gtk_box_append(GTK_BOX(bar), tool_button("go-previous-symbolic", "Back", "win.back"));
	gtk_box_append(GTK_BOX(bar), tool_button("go-next-symbolic", "Forward", "win.forward"));
	gtk_box_append(GTK_BOX(bar), tool_button("go-up-symbolic", "Up", "win.up"));
	gtk_box_append(GTK_BOX(bar), tool_button("go-home-symbolic", "Home", "win.home"));
	gtk_box_append(GTK_BOX(bar), tool_button("view-refresh-symbolic", "Reload", "win.reload"));
	auto spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_hexpand(spacer, TRUE);
	gtk_box_append(GTK_BOX(bar), spacer);
	searchentry = gtk_search_entry_new();
	gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(searchentry), "Find files");
	gtk_editable_set_width_chars(GTK_EDITABLE(searchentry), 22);
	g_signal_connect(searchentry, "activate", G_CALLBACK(on_search), NULL);
	g_signal_connect(searchentry, "search-changed", G_CALLBACK(on_search_changed), NULL);
	g_signal_connect(searchentry, "stop-search", G_CALLBACK(on_stop_search), NULL);
	gtk_box_append(GTK_BOX(bar), searchentry);
	gtk_box_append(GTK_BOX(bar), tool_button("folder-new-symbolic", "New folder", "win.mkdir"));
	gtk_box_append(GTK_BOX(bar), tool_toggle("view-grid-symbolic", "Show as icons", "win.grid"));
	gtk_box_append(GTK_BOX(bar), tool_toggle("view-dual-symbolic", "Two panes", "win.dual"));
	gtk_box_append(GTK_BOX(bar), tool_button("utilities-terminal-symbolic", "Open a terminal here", "win.terminal"));
	auto menubtn = gtk_menu_button_new();
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menubtn), "open-menu-symbolic");
	gtk_widget_set_tooltip_text(menubtn, "Menu");
	auto mm = main_menu();
	gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menubtn), mm);
	g_object_unref(mm);
	gtk_box_append(GTK_BOX(bar), menubtn);
	return bar;
}

static GtkWidget *build_sidebar(void)
{
	sidebar = gtk_list_box_new();
	gtk_widget_add_css_class(sidebar, "navigation-sidebar");
	g_signal_connect(sidebar, "row-activated", G_CALLBACK(on_side_activate), NULL);
	auto drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect(drop, "drop", G_CALLBACK(on_side_drop), NULL);
	gtk_widget_add_controller(sidebar, GTK_EVENT_CONTROLLER(drop));
	auto scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), sidebar);

	volumes = g_volume_monitor_get();
	g_signal_connect_swapped(volumes, "mount-added", G_CALLBACK(schedule_sidebar), NULL);
	g_signal_connect_swapped(volumes, "mount-removed", G_CALLBACK(schedule_sidebar), NULL);
	g_signal_connect_swapped(volumes, "mount-changed", G_CALLBACK(schedule_sidebar), NULL);
	auto bpath = bookmarks_path();
	auto bfile = g_file_new_for_path(bpath);
	auto bmon = g_file_monitor_file(bfile, G_FILE_MONITOR_NONE, NULL, NULL);
	if (bmon) {
		g_signal_connect_swapped(bmon, "changed", G_CALLBACK(schedule_sidebar), NULL);
		g_object_set_data_full(G_OBJECT(scroll), "bookmarks-monitor", bmon, g_object_unref);
	}
	g_object_unref(bfile);
	g_free(bpath);
	return scroll;
}

static GtkWidget *build_statusbar(void)
{
	auto bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_margin_top(bar, 4);
	gtk_widget_set_margin_bottom(bar, 4);
	gtk_widget_set_margin_start(bar, 8);
	gtk_widget_set_margin_end(bar, 8);
	statuslbl = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(statuslbl), 0);
	gtk_label_set_ellipsize(GTK_LABEL(statuslbl), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(statuslbl, TRUE);
	gtk_box_append(GTK_BOX(bar), statuslbl);
	cancelbtn = gtk_button_new_with_mnemonic("_Cancel");
	g_signal_connect(cancelbtn, "clicked", G_CALLBACK(on_cancel), NULL);
	gtk_widget_set_visible(cancelbtn, FALSE);
	gtk_box_append(GTK_BOX(bar), cancelbtn);
	return bar;
}

static void build(GtkApplication *app, GFile *start)
{
	win = gtk_application_window_new(app);
	gtk_window_set_default_size(GTK_WINDOW(win), 960, 600);
	g_action_map_add_action_entries(G_ACTION_MAP(win), actions, G_N_ELEMENTS(actions), NULL);
	for (size_t i = 0; i < G_N_ELEMENTS(accels); i++)
		gtk_application_set_accels_for_action(app, accels[i].action, accels[i].accels);

	auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(win), box);
	gtk_box_append(GTK_BOX(box), build_toolbar());

	auto side = build_sidebar();
	auto cm = context_menu();
	build_pane(&panes[0], cm);
	build_pane(&panes[1], cm);
	g_object_unref(cm);
	gtk_widget_set_visible(panes[1].box, FALSE);
	auto split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(split), panes[0].box);
	gtk_paned_set_end_child(GTK_PANED(split), panes[1].box);
	gtk_paned_set_shrink_start_child(GTK_PANED(split), FALSE);
	gtk_paned_set_shrink_end_child(GTK_PANED(split), FALSE);
	auto hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_start_child(GTK_PANED(hpaned), side);
	gtk_paned_set_end_child(GTK_PANED(hpaned), split);
	gtk_paned_set_resize_start_child(GTK_PANED(hpaned), FALSE);
	gtk_paned_set_shrink_start_child(GTK_PANED(hpaned), FALSE);
	gtk_paned_set_position(GTK_PANED(hpaned), 180);
	gtk_widget_set_vexpand(hpaned, TRUE);
	gtk_box_append(GTK_BOX(box), hpaned);
	gtk_box_append(GTK_BOX(box), build_statusbar());

	act = &panes[0];
	rebuild_sidebar(NULL);
	load(&panes[0], start);
	set_active(&panes[0]);
	gtk_window_present(GTK_WINDOW(win));
	gtk_widget_grab_focus(panes[0].view);
}

static void on_activate_app(GApplication *app, gpointer data)
{
	(void)data;
	if (win)
		return;
	auto home = g_file_new_for_path(g_get_home_dir());
	build(GTK_APPLICATION(app), home);
	g_object_unref(home);
}

static void on_open(GApplication *app, GFile **files, int n, const char *hint, gpointer data)
{
	(void)n;
	(void)hint;
	(void)data;
	auto dir = g_object_ref(files[0]);
	if (g_file_query_file_type(dir, 0, NULL) != G_FILE_TYPE_DIRECTORY) {
		auto parent = g_file_get_parent(dir);
		if (parent) {
			g_object_unref(dir);
			dir = parent;
		}
	}
	if (win)
		go(act, dir);
	else
		build(GTK_APPLICATION(app), dir);
	g_object_unref(dir);
}

int main(int argc, char **argv)
{
	auto app = gtk_application_new("org.tinyhat.Files", G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_OPEN);
	g_signal_connect(app, "activate", G_CALLBACK(on_activate_app), NULL);
	g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
	int status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
