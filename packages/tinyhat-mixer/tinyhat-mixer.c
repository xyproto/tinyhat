#include <gtk/gtk.h>
#include <glib-unix.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/utils/json.h>
#include <spa/utils/result.h>

#define MAXCH 64
#define MAXROUTES 32

enum kind { SINK, SOURCE, PLAYBACK, RECORD, NKINDS };

static const char *tab_names[NKINDS] = { "Output Devices", "Input Devices", "Playback", "Recording" };
static const char *empty_texts[NKINDS] = { "No output devices", "No input devices", "Nothing is playing",
	"Nothing is recording" };

struct dev {
	uint32_t id;
	struct pw_proxy *proxy;
	struct spa_hook listener;
	int nroutes;
	struct {
		int device, index, nch;
		float vol;
		bool mute, have_props;
	} routes[MAXROUTES];
	struct dev *next;
};

struct obj {
	uint32_t id;
	enum kind kind;
	struct pw_proxy *proxy;
	struct spa_hook listener;
	char name[256], title[512], serial[32], target[256];
	int device_id, profile_device, nch;
	float vol;
	bool mute, have_props;
	GtkWidget *row, *label, *scale, *pct, *mute_btn, *default_btn, *dropdown;
	gulong scale_handler, mute_handler, default_handler, dropdown_handler;
	struct obj *next;
};

static struct pw_loop *loop;
static struct pw_context *context;
static struct pw_core *core;
static struct pw_registry *registry;
static struct spa_hook core_listener, registry_listener, metadata_listener;
static struct pw_metadata *metadata;
static uint32_t metadata_id = SPA_ID_INVALID;
static struct obj *objs;
static struct dev *devs;
static char default_name[2][256];
static bool listing, synced;
static int sync_seq;

static GtkWidget *window, *lists[NKINDS], *empties[NKINDS];
static GtkStringList *target_models[2];
static char target_serials[2][64][32];
static int ntargets[2];

static struct obj *find_obj(uint32_t id)
{
	for (struct obj *o = objs; o; o = o->next)
		if (o->id == id)
			return o;
	return NULL;
}

static struct dev *find_dev(uint32_t id)
{
	for (struct dev *d = devs; d; d = d->next)
		if (d->id == id)
			return d;
	return NULL;
}

static bool json_name(const char *json, char *out, size_t len)
{
	struct spa_json it[2];
	char key[64];
	const char *v;
	if (!json)
		return false;
	spa_json_init(&it[0], json, strlen(json));
	if (spa_json_enter_object(&it[0], &it[1]) <= 0)
		return false;
	while (spa_json_get_string(&it[1], key, sizeof key) > 0) {
		if (strcmp(key, "name") == 0)
			return spa_json_get_string(&it[1], out, len) > 0;
		if (spa_json_next(&it[1], &v) <= 0)
			break;
	}
	return false;
}

static bool parse_props(const struct spa_pod *pod, float *vol, int *nch, bool *mute)
{
	const struct spa_pod_prop *prop;
	bool found = false;
	if (!pod || !spa_pod_is_object(pod))
		return false;
	SPA_POD_OBJECT_FOREACH((const struct spa_pod_object *)pod, prop) {
		if (prop->key == SPA_PROP_channelVolumes) {
			float vols[MAXCH], max = 0;
			uint32_t n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, vols, MAXCH);
			if (n == 0)
				continue;
			for (uint32_t i = 0; i < n; i++)
				max = SPA_MAX(max, vols[i]);
			*nch = n;
			*vol = max;
			found = true;
		} else if (prop->key == SPA_PROP_mute) {
			bool b;
			if (spa_pod_get_bool(&prop->value, &b) == 0)
				*mute = b;
		}
	}
	return found;
}

static int to_percent(float linear)
{
	return (int)lround(cbrt(linear) * 100.0);
}

static void update_empty(enum kind k)
{
	bool any = false;
	for (struct obj *o = objs; o; o = o->next)
		if (o->kind == k && o->row)
			any = true;
	if (empties[k])
		gtk_widget_set_visible(empties[k], !any);
}

static void update_row(struct obj *o)
{
	char buf[16];
	if (!o->row)
		return;
	gtk_label_set_text(GTK_LABEL(o->label), o->title);
	g_signal_handler_block(o->scale, o->scale_handler);
	gtk_range_set_value(GTK_RANGE(o->scale), to_percent(o->vol));
	g_signal_handler_unblock(o->scale, o->scale_handler);
	snprintf(buf, sizeof buf, "%d%%", to_percent(o->vol));
	gtk_label_set_text(GTK_LABEL(o->pct), buf);
	g_signal_handler_block(o->mute_btn, o->mute_handler);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(o->mute_btn), o->mute);
	g_signal_handler_unblock(o->mute_btn, o->mute_handler);
	gtk_button_set_icon_name(GTK_BUTTON(o->mute_btn), o->mute ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic");
	gtk_widget_set_tooltip_text(o->mute_btn, o->mute ? "Unmute" : "Mute");
	if (o->default_btn) {
		g_signal_handler_block(o->default_btn, o->default_handler);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(o->default_btn), strcmp(o->name, default_name[o->kind]) == 0);
		g_signal_handler_unblock(o->default_btn, o->default_handler);
	}
}

static void update_defaults(void)
{
	for (struct obj *o = objs; o; o = o->next)
		if (o->kind == SINK || o->kind == SOURCE)
			update_row(o);
}

static void select_target(struct obj *o)
{
	int t = o->kind == PLAYBACK ? SINK : SOURCE, sel = 0;
	if (!o->dropdown)
		return;
	for (int i = 0; i < ntargets[t] && o->target[0]; i++)
		if (strcmp(target_serials[t][i], o->target) == 0)
			sel = i + 1;
	if (!sel && o->target[0])
		for (struct obj *d = objs; d; d = d->next)
			if (d->kind == (enum kind)t && strcmp(d->name, o->target) == 0)
				for (int i = 0; i < ntargets[t]; i++)
					if (strcmp(target_serials[t][i], d->serial) == 0)
						sel = i + 1;
	g_signal_handler_block(o->dropdown, o->dropdown_handler);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(o->dropdown), sel);
	g_signal_handler_unblock(o->dropdown, o->dropdown_handler);
}

static void update_targets(enum kind t)
{
	const char *names[65];
	int n = 0;
	if (listing || !target_models[t])
		return;
	names[n] = "Default";
	for (struct obj *d = objs; d && n < 64; d = d->next)
		if (d->kind == t && d->serial[0]) {
			snprintf(target_serials[t][n], sizeof target_serials[t][n], "%s", d->serial);
			names[++n] = d->title;
		}
	ntargets[t] = n;
	names[n + 1] = NULL;
	for (struct obj *o = objs; o; o = o->next)
		if (o->dropdown && (o->kind == PLAYBACK ? SINK : SOURCE) == t)
			g_signal_handler_block(o->dropdown, o->dropdown_handler);
	gtk_string_list_splice(target_models[t], 0, g_list_model_get_n_items(G_LIST_MODEL(target_models[t])), names);
	for (struct obj *o = objs; o; o = o->next)
		if (o->dropdown && (o->kind == PLAYBACK ? SINK : SOURCE) == t) {
			g_signal_handler_unblock(o->dropdown, o->dropdown_handler);
			select_target(o);
		}
}

static struct dev *route_for(struct obj *o, int *index)
{
	struct dev *d;
	if (o->kind > SOURCE || o->device_id < 0 || o->profile_device < 0 || !(d = find_dev(o->device_id)))
		return NULL;
	for (int i = 0; i < d->nroutes; i++)
		if (d->routes[i].device == o->profile_device) {
			*index = d->routes[i].index;
			if (d->routes[i].have_props) {
				o->vol = d->routes[i].vol;
				o->nch = d->routes[i].nch;
				o->mute = d->routes[i].mute;
			}
			return d;
		}
	return NULL;
}

static void set_props(struct obj *o, const float *vols, int n, const bool *mute)
{
	uint8_t buf[2048];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
	struct spa_pod_frame f[2];
	struct spa_pod *param;
	int index;
	struct dev *d = route_for(o, &index);
	if (d) {
		spa_pod_builder_push_object(&b, &f[0], SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
		spa_pod_builder_add(&b, SPA_PARAM_ROUTE_index, SPA_POD_Int(index), SPA_PARAM_ROUTE_device,
			SPA_POD_Int(o->profile_device), 0);
		spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
	}
	spa_pod_builder_push_object(&b, &f[1], SPA_TYPE_OBJECT_Props, d ? SPA_PARAM_Route : SPA_PARAM_Props);
	if (vols) {
		spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
		spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, n, vols);
	}
	if (mute) {
		spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
		spa_pod_builder_bool(&b, *mute);
	}
	param = spa_pod_builder_pop(&b, &f[1]);
	if (d) {
		spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_save, 0);
		spa_pod_builder_bool(&b, true);
		param = spa_pod_builder_pop(&b, &f[0]);
		pw_device_set_param((struct pw_device *)d->proxy, SPA_PARAM_Route, 0, param);
	} else {
		pw_node_set_param((struct pw_node *)o->proxy, SPA_PARAM_Props, 0, param);
	}
}

static void on_scale(GtkRange *range, gpointer data)
{
	struct obj *o = data;
	float vols[MAXCH];
	char buf[16];
	double pct = gtk_range_get_value(range);
	int n = o->nch > 0 ? o->nch : 2;
	float lin = (float)pow(pct / 100.0, 3.0);
	for (int i = 0; i < n; i++)
		vols[i] = lin;
	o->vol = lin;
	snprintf(buf, sizeof buf, "%d%%", (int)lround(pct));
	gtk_label_set_text(GTK_LABEL(o->pct), buf);
	set_props(o, vols, n, NULL);
}

static void on_mute(GtkToggleButton *btn, gpointer data)
{
	struct obj *o = data;
	bool mute = gtk_toggle_button_get_active(btn);
	o->mute = mute;
	gtk_button_set_icon_name(GTK_BUTTON(btn), mute ? "audio-volume-muted-symbolic" : "audio-volume-high-symbolic");
	set_props(o, NULL, 0, &mute);
}

static void on_default(GtkCheckButton *btn, gpointer data)
{
	struct obj *o = data;
	char json[300];
	if (!gtk_check_button_get_active(btn)) {
		update_row(o);
		return;
	}
	if (!metadata)
		return;
	snprintf(json, sizeof json, "{ \"name\": \"%s\" }", o->name);
	pw_metadata_set_property(metadata, PW_ID_CORE,
		o->kind == SINK ? "default.configured.audio.sink" : "default.configured.audio.source", "Spa:String:JSON", json);
}

static void on_target(GtkDropDown *dd, GParamSpec *pspec, gpointer data)
{
	struct obj *o = data;
	int t = o->kind == PLAYBACK ? SINK : SOURCE;
	guint sel = gtk_drop_down_get_selected(dd);
	(void)pspec;
	if (!metadata)
		return;
	if (sel == 0 || sel > (guint)ntargets[t]) {
		o->target[0] = 0;
		pw_metadata_set_property(metadata, o->id, "target.object", NULL, NULL);
	} else {
		snprintf(o->target, sizeof o->target, "%s", target_serials[t][sel - 1]);
		pw_metadata_set_property(metadata, o->id, "target.object", "Spa:Id", o->target);
	}
}

static void add_row(struct obj *o)
{
	GtkWidget *box, *top, *bottom;
	if (listing || o->row || !lists[o->kind])
		return;
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_margin_start(box, 12);
	gtk_widget_set_margin_end(box, 12);
	gtk_widget_set_margin_top(box, 8);
	gtk_widget_set_margin_bottom(box, 8);
	top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	o->label = gtk_label_new(o->title);
	gtk_label_set_xalign(GTK_LABEL(o->label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(o->label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(o->label, TRUE);
	gtk_widget_add_css_class(o->label, "heading");
	gtk_box_append(GTK_BOX(top), o->label);
	if (o->kind == SINK || o->kind == SOURCE) {
		o->default_btn = gtk_check_button_new_with_label("Default");
		gtk_widget_set_tooltip_text(o->default_btn,
			o->kind == SINK ? "Play sound through this device" : "Record sound from this device");
		o->default_handler = g_signal_connect(o->default_btn, "toggled", G_CALLBACK(on_default), o);
		gtk_box_append(GTK_BOX(top), o->default_btn);
	} else {
		int t = o->kind == PLAYBACK ? SINK : SOURCE;
		o->dropdown = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(target_models[t])), NULL);
		gtk_widget_set_tooltip_text(o->dropdown, o->kind == PLAYBACK ? "Output device" : "Input device");
		o->dropdown_handler = g_signal_connect(o->dropdown, "notify::selected", G_CALLBACK(on_target), o);
		gtk_box_append(GTK_BOX(top), o->dropdown);
		select_target(o);
	}
	bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	o->mute_btn = gtk_toggle_button_new();
	gtk_button_set_icon_name(GTK_BUTTON(o->mute_btn), "audio-volume-high-symbolic");
	o->mute_handler = g_signal_connect(o->mute_btn, "toggled", G_CALLBACK(on_mute), o);
	gtk_box_append(GTK_BOX(bottom), o->mute_btn);
	o->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 150, 1);
	gtk_scale_set_draw_value(GTK_SCALE(o->scale), FALSE);
	gtk_scale_add_mark(GTK_SCALE(o->scale), 100, GTK_POS_BOTTOM, NULL);
	gtk_widget_set_hexpand(o->scale, TRUE);
	o->scale_handler = g_signal_connect(o->scale, "value-changed", G_CALLBACK(on_scale), o);
	gtk_box_append(GTK_BOX(bottom), o->scale);
	o->pct = gtk_label_new("0%");
	gtk_label_set_width_chars(GTK_LABEL(o->pct), 5);
	gtk_label_set_xalign(GTK_LABEL(o->pct), 1);
	gtk_box_append(GTK_BOX(bottom), o->pct);
	gtk_box_append(GTK_BOX(box), top);
	gtk_box_append(GTK_BOX(box), bottom);
	o->row = box;
	gtk_box_append(GTK_BOX(lists[o->kind]), box);
	gtk_widget_set_visible(box, o->have_props);
	update_row(o);
	update_empty(o->kind);
}

static void node_info(void *data, const struct pw_node_info *info)
{
	struct obj *o = data;
	const struct spa_dict *p = info->props;
	const char *s;
	if (!(info->change_mask & PW_NODE_CHANGE_MASK_PROPS) || !p)
		return;
	if ((s = spa_dict_lookup(p, PW_KEY_NODE_NAME)))
		snprintf(o->name, sizeof o->name, "%s", s);
	if ((s = spa_dict_lookup(p, PW_KEY_OBJECT_SERIAL)))
		snprintf(o->serial, sizeof o->serial, "%s", s);
	o->device_id = (s = spa_dict_lookup(p, PW_KEY_DEVICE_ID)) ? atoi(s) : -1;
	o->profile_device = (s = spa_dict_lookup(p, "card.profile.device")) ? atoi(s) : -1;
	if (o->kind == SINK || o->kind == SOURCE) {
		if (!(s = spa_dict_lookup(p, PW_KEY_NODE_DESCRIPTION)) && !(s = spa_dict_lookup(p, PW_KEY_NODE_NICK)))
			s = o->name;
		snprintf(o->title, sizeof o->title, "%s", s);
	} else {
		const char *app = spa_dict_lookup(p, PW_KEY_APP_NAME), *media = spa_dict_lookup(p, PW_KEY_MEDIA_NAME);
		if (!app)
			app = spa_dict_lookup(p, PW_KEY_NODE_DESCRIPTION);
		if (!app)
			app = o->name;
		if (media && strcmp(media, app) != 0)
			snprintf(o->title, sizeof o->title, "%s: %s", app, media);
		else
			snprintf(o->title, sizeof o->title, "%s", app);
	}
	if (o->kind == SINK || o->kind == SOURCE)
		update_targets(o->kind);
	update_row(o);
}

static void node_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
	struct obj *o = data;
	int ri;
	(void)seq, (void)index, (void)next;
	if (id != SPA_PARAM_Props || !param)
		return;
	if (route_for(o, &ri)) {
		o->have_props = true;
	} else if (parse_props(param, &o->vol, &o->nch, &o->mute)) {
		o->have_props = true;
	}
	if (o->row && o->have_props)
		gtk_widget_set_visible(o->row, TRUE);
	update_row(o);
}

static const struct pw_node_events node_events = {
	PW_VERSION_NODE_EVENTS,
	.info = node_info,
	.param = node_param,
};

static void device_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next, const struct spa_pod *param)
{
	struct dev *d = data;
	int32_t idx, device;
	struct spa_pod *props = NULL;
	int i;
	(void)seq, (void)index, (void)next;
	if (id != SPA_PARAM_Route || !param)
		return;
	if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamRoute, NULL, SPA_PARAM_ROUTE_index, SPA_POD_Int(&idx),
			SPA_PARAM_ROUTE_device, SPA_POD_Int(&device), SPA_PARAM_ROUTE_props, SPA_POD_OPT_Pod(&props)) < 0)
		return;
	for (i = 0; i < d->nroutes; i++)
		if (d->routes[i].device == device)
			break;
	if (i == d->nroutes) {
		if (d->nroutes == MAXROUTES)
			return;
		memset(&d->routes[i], 0, sizeof d->routes[i]);
		d->nroutes++;
	}
	d->routes[i].device = device;
	d->routes[i].index = idx;
	if (parse_props(props, &d->routes[i].vol, &d->routes[i].nch, &d->routes[i].mute))
		d->routes[i].have_props = true;
	for (struct obj *o = objs; o; o = o->next)
		if (o->device_id == (int)d->id && o->profile_device == device) {
			int ri;
			if (route_for(o, &ri) && d->routes[i].have_props) {
				o->have_props = true;
				if (o->row)
					gtk_widget_set_visible(o->row, TRUE);
			}
			update_row(o);
		}
}

static void device_info(void *data, const struct pw_device_info *info)
{
	struct dev *d = data;
	if (info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS)
		d->nroutes = 0;
}

static const struct pw_device_events device_events = {
	PW_VERSION_DEVICE_EVENTS,
	.info = device_info,
	.param = device_param,
};

static int metadata_property(void *data, uint32_t subject, const char *key, const char *type, const char *value)
{
	(void)data, (void)type;
	if (subject == PW_ID_CORE) {
		int k = -1;
		if (!key || strcmp(key, "default.audio.sink") == 0)
			k = SINK;
		if (key && strcmp(key, "default.audio.source") == 0)
			k = SOURCE;
		if (k >= 0 && !(value && json_name(value, default_name[k], sizeof default_name[k])))
			default_name[k][0] = 0;
		if (!key) {
			default_name[SOURCE][0] = 0;
		}
		update_defaults();
	} else if (!key || strcmp(key, "target.object") == 0) {
		struct obj *o = find_obj(subject);
		if (o) {
			snprintf(o->target, sizeof o->target, "%s", key && value ? value : "");
			select_target(o);
		}
	}
	return 0;
}

static const struct pw_metadata_events metadata_events = {
	PW_VERSION_METADATA_EVENTS,
	.property = metadata_property,
};

static void registry_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version,
	const struct spa_dict *props)
{
	(void)data, (void)permissions, (void)version;
	if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
		const char *cls = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : NULL;
		enum kind k;
		struct obj *o;
		uint32_t ids[] = { SPA_PARAM_Props };
		if (!cls)
			return;
		if (strcmp(cls, "Audio/Sink") == 0)
			k = SINK;
		else if (strncmp(cls, "Audio/Source", 12) == 0)
			k = SOURCE;
		else if (strcmp(cls, "Stream/Output/Audio") == 0)
			k = PLAYBACK;
		else if (strcmp(cls, "Stream/Input/Audio") == 0)
			k = RECORD;
		else
			return;
		o = calloc(1, sizeof *o);
		o->id = id;
		o->kind = k;
		o->device_id = o->profile_device = -1;
		o->vol = 1;
		o->proxy = pw_registry_bind(registry, id, type, PW_VERSION_NODE, 0);
		if (!o->proxy) {
			free(o);
			return;
		}
		pw_proxy_add_object_listener(o->proxy, &o->listener, &node_events, o);
		pw_node_subscribe_params((struct pw_node *)o->proxy, ids, 1);
		o->next = objs;
		objs = o;
		add_row(o);
	} else if (strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
		const char *cls = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : NULL;
		struct dev *d;
		uint32_t ids[] = { SPA_PARAM_Route };
		if (!cls || strcmp(cls, "Audio/Device") != 0)
			return;
		d = calloc(1, sizeof *d);
		d->id = id;
		d->proxy = pw_registry_bind(registry, id, type, PW_VERSION_DEVICE, 0);
		if (!d->proxy) {
			free(d);
			return;
		}
		pw_proxy_add_object_listener(d->proxy, &d->listener, &device_events, d);
		pw_device_subscribe_params((struct pw_device *)d->proxy, ids, 1);
		d->next = devs;
		devs = d;
	} else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !metadata) {
		const char *name = props ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : NULL;
		if (!name || strcmp(name, "default") != 0)
			return;
		metadata = pw_registry_bind(registry, id, type, PW_VERSION_METADATA, 0);
		if (!metadata)
			return;
		metadata_id = id;
		pw_metadata_add_listener(metadata, &metadata_listener, &metadata_events, NULL);
	}
}

static void registry_global_remove(void *data, uint32_t id)
{
	(void)data;
	for (struct obj **p = &objs; *p; p = &(*p)->next)
		if ((*p)->id == id) {
			struct obj *o = *p;
			*p = o->next;
			spa_hook_remove(&o->listener);
			pw_proxy_destroy(o->proxy);
			if (o->row)
				gtk_box_remove(GTK_BOX(lists[o->kind]), o->row);
			if (o->kind == SINK || o->kind == SOURCE)
				update_targets(o->kind);
			update_empty(o->kind);
			free(o);
			return;
		}
	for (struct dev **p = &devs; *p; p = &(*p)->next)
		if ((*p)->id == id) {
			struct dev *d = *p;
			*p = d->next;
			spa_hook_remove(&d->listener);
			pw_proxy_destroy(d->proxy);
			free(d);
			return;
		}
	if (id == metadata_id && metadata) {
		spa_hook_remove(&metadata_listener);
		pw_proxy_destroy((struct pw_proxy *)metadata);
		metadata = NULL;
		metadata_id = SPA_ID_INVALID;
	}
}

static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void core_done(void *data, uint32_t id, int seq)
{
	(void)data;
	if (id == PW_ID_CORE && seq == sync_seq)
		synced = true;
}

static void core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	(void)data, (void)seq;
	fprintf(stderr, "tinyhat-mixer: PipeWire error on object %u: %s (%s)\n", id, message, spa_strerror(res));
	if (id == PW_ID_CORE && res == -EPIPE) {
		if (listing)
			synced = true;
		else if (window)
			gtk_window_destroy(GTK_WINDOW(window));
	}
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = core_done,
	.error = core_error,
};

static gboolean on_pw(int fd, GIOCondition cond, gpointer data)
{
	(void)fd, (void)cond, (void)data;
	pw_loop_iterate(loop, 0);
	return G_SOURCE_CONTINUE;
}

static bool connect_pw(void)
{
	loop = pw_loop_new(NULL);
	if (!loop)
		return false;
	pw_loop_enter(loop);
	context = pw_context_new(loop, NULL, 0);
	if (!context)
		return false;
	core = pw_context_connect(context, NULL, 0);
	if (!core)
		return false;
	pw_core_add_listener(core, &core_listener, &core_events, NULL);
	registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
	pw_registry_add_listener(registry, &registry_listener, &registry_events, NULL);
	return true;
}

static int list(void)
{
	static const char *kinds[NKINDS] = { "output", "input", "playback", "recording" };
	if (!connect_pw()) {
		fprintf(stderr, "tinyhat-mixer: cannot connect to PipeWire\n");
		return 1;
	}
	for (int round = 0; round < 3; round++) {
		synced = false;
		sync_seq = pw_core_sync(core, PW_ID_CORE, 0);
		while (!synced)
			pw_loop_iterate(loop, -1);
	}
	for (int k = 0; k < NKINDS; k++)
		for (struct obj *o = objs; o; o = o->next) {
			int index;
			if (o->kind != (enum kind)k)
				continue;
			printf("%-9s %3u %4d%% %s%s%s%s\n", kinds[k], o->id, to_percent(o->vol), o->mute ? "muted " : "",
				(k <= SOURCE && strcmp(o->name, default_name[k]) == 0) ? "default " : "",
				route_for(o, &index) ? "route " : "", o->title);
		}
	return 0;
}

static GtkWidget *make_tab(enum kind k)
{
	GtkWidget *scroll = gtk_scrolled_window_new(), *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	lists[k] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	empties[k] = gtk_label_new(empty_texts[k]);
	gtk_widget_add_css_class(empties[k], "dim-label");
	gtk_widget_set_margin_top(empties[k], 24);
	gtk_box_append(GTK_BOX(outer), empties[k]);
	gtk_box_append(GTK_BOX(outer), lists[k]);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), outer);
	gtk_widget_set_vexpand(scroll, TRUE);
	return scroll;
}

static void activate(GtkApplication *app, gpointer data)
{
	GtkWidget *notebook;
	(void)data;
	if (window) {
		gtk_window_present(GTK_WINDOW(window));
		return;
	}
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Volume Control");
	gtk_window_set_default_size(GTK_WINDOW(window), 560, 440);
	target_models[SINK] = gtk_string_list_new(NULL);
	target_models[SOURCE] = gtk_string_list_new(NULL);
	notebook = gtk_notebook_new();
	for (int k = 0; k < NKINDS; k++)
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), make_tab(k), gtk_label_new(tab_names[k]));
	gtk_window_set_child(GTK_WINDOW(window), notebook);
	if (!connect_pw()) {
		GtkWidget *label = gtk_label_new("Cannot connect to PipeWire");
		gtk_window_set_child(GTK_WINDOW(window), label);
	} else {
		g_unix_fd_add(pw_loop_get_fd(loop), G_IO_IN, on_pw, NULL);
		update_targets(SINK);
		update_targets(SOURCE);
	}
	for (int k = 0; k < NKINDS; k++)
		update_empty(k);
	gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv)
{
	GtkApplication *app;
	int status;
	pw_init(&argc, &argv);
	if (argc > 1 && strcmp(argv[1], "--list") == 0) {
		listing = true;
		return list();
	}
	app = gtk_application_new("org.tinyhat.Mixer", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}
