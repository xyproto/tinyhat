#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pipewire/impl-module.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/utils/result.h>

#define TIMEOUT_MS 5000

struct wait {
	struct pw_main_loop *loop;
	struct pw_core *core;
	struct pw_registry *registry;
	struct pw_metadata *metadata;
	struct spa_hook core_listener;
	struct spa_hook registry_listener;
	struct spa_hook metadata_listener;
	uint64_t cookie;
	char marker[512];
	bool known;
	bool done;
};

static bool marked(struct wait *w)
{
	char buf[32] = "";
	FILE *f = fopen(w->marker, "r");
	if (!f)
		return false;
	bool ok = fgets(buf, sizeof buf, f) && strtoull(buf, NULL, 10) == w->cookie;
	fclose(f);
	return ok;
}

static void finish(struct wait *w)
{
	if (w->done)
		return;
	w->done = true;
	if (w->marker[0] && w->known && !marked(w)) {
		char tmp[sizeof w->marker + 8];
		snprintf(tmp, sizeof tmp, "%s.%d", w->marker, getpid());
		FILE *f = fopen(tmp, "w");
		if (f) {
			fprintf(f, "%llu\n", (unsigned long long)w->cookie);
			if (fclose(f) == 0)
				rename(tmp, w->marker);
			else
				unlink(tmp);
		}
	}
	pw_main_loop_quit(w->loop);
}

static int metadata_property(void *data, uint32_t subject, const char *key, const char *type, const char *value)
{
	struct wait *w = data;
	if (key && value && strcmp(key, "default.audio.sink") == 0)
		finish(w);
	return 0;
}

static const struct pw_metadata_events metadata_events = {
	PW_VERSION_METADATA_EVENTS,
	.property = metadata_property,
};

static void registry_global(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version,
	const struct spa_dict *props)
{
	struct wait *w = data;
	if (w->metadata || strcmp(type, PW_TYPE_INTERFACE_Metadata) != 0 || !props)
		return;
	const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
	if (!name || strcmp(name, "default") != 0)
		return;
	w->metadata = pw_registry_bind(w->registry, id, type, PW_VERSION_METADATA, 0);
	if (w->metadata)
		pw_metadata_add_listener(w->metadata, &w->metadata_listener, &metadata_events, w);
}

static const struct pw_registry_events registry_events = {
	PW_VERSION_REGISTRY_EVENTS,
	.global = registry_global,
};

static void core_info(void *data, const struct pw_core_info *info)
{
	struct wait *w = data;
	w->cookie = info->cookie;
	w->known = true;
}

static void core_done(void *data, uint32_t id, int seq)
{
	struct wait *w = data;
	if (id == PW_ID_CORE && w->known && marked(w))
		finish(w);
}

static void core_error(void *data, uint32_t id, int seq, int res, const char *message)
{
	struct wait *w = data;
	if (id == PW_ID_CORE && res == -EPIPE) {
		w->known = false;
		finish(w);
	}
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.info = core_info,
	.done = core_done,
	.error = core_error,
};

static void timeout(void *data, uint64_t expirations)
{
	finish(data);
}

static void wait_for_sink(void)
{
	struct wait w = {0};
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir && *dir)
		snprintf(w.marker, sizeof w.marker, "%s/tinyhat-pipewire-wait", dir);
	w.loop = pw_main_loop_new(NULL);
	if (!w.loop)
		return;
	struct pw_loop *loop = pw_main_loop_get_loop(w.loop);
	struct pw_context *context = pw_context_new(loop, NULL, 0);
	if (context) {
		w.core = pw_context_connect(context, NULL, 0);
		if (w.core) {
			pw_core_add_listener(w.core, &w.core_listener, &core_events, &w);
			w.registry = pw_core_get_registry(w.core, PW_VERSION_REGISTRY, 0);
			pw_registry_add_listener(w.registry, &w.registry_listener, &registry_events, &w);
			pw_core_sync(w.core, PW_ID_CORE, 0);
			struct spa_source *timer = pw_loop_add_timer(loop, timeout, &w);
			struct timespec ts = { TIMEOUT_MS / 1000, (TIMEOUT_MS % 1000) * 1000000L };
			pw_loop_update_timer(loop, timer, &ts, NULL, false);
			pw_main_loop_run(w.loop);
			pw_loop_destroy_source(loop, timer);
			if (w.metadata) {
				spa_hook_remove(&w.metadata_listener);
				pw_proxy_destroy((struct pw_proxy *)w.metadata);
			}
			spa_hook_remove(&w.registry_listener);
			pw_proxy_destroy((struct pw_proxy *)w.registry);
			spa_hook_remove(&w.core_listener);
			pw_core_disconnect(w.core);
		}
		pw_context_destroy(context);
	}
	pw_main_loop_destroy(w.loop);
}

SPA_EXPORT int pipewire__module_init(struct pw_impl_module *module, const char *args)
{
	static __thread bool busy;
	if (busy)
		return 0;
	busy = true;
	wait_for_sink();
	busy = false;
	return 0;
}
