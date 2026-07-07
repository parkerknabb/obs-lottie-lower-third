#include "lottie-lower-third-internal.h"

#include "lottie-lower-third-lottie.h"
#include "lottie-lower-third-markers.h"
#include "lottie-lower-third-playback.h"
#include "lottie-lower-third-properties.h"
#include "lottie-lower-third-render.h"
#include "lottie-lower-third-scene.h"
#include "lottie-passthrough-transition.h"

#include <plugin-support.h>
#include <util/platform.h>

#include <string.h>

#define TEXT_RELOAD_DEBOUNCE_NS 350000000ULL

static bool set_bstr(char **dst, const char *src)
{
	if (!src)
		src = "";

	if (*dst && strcmp(*dst, src) == 0)
		return false;

	bfree(*dst);
	*dst = bstrdup(src);

	return true;
}

static const char *lottie_lower_third_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Lottie Lower Third";
}

static void *lottie_lower_third_create(obs_data_t *settings, obs_source_t *source)
{
	struct lottie_lower_third *ctx = bzalloc(sizeof(struct lottie_lower_third));
	pthread_mutex_init(&ctx->load_mutex, NULL);
	ctx->load_mutex_initialized = true;

	ctx->source = source;
	ctx->state = STATE_HIDDEN;
	ctx->suppress_hidden_preview = false;
	ctx->hold_start_frame = 30;

	ctx->buffer_width = 1920;
	ctx->buffer_height = 1080;
	ctx->lottie_loaded = false;
	ctx->lottie_fps = 30.0f;
	ctx->lottie_width = 1920.0f;
	ctx->lottie_height = 1080.0f;
	ctx->is_looping_hold = false;
	ctx->default_placement_pending =
		!lottie_lower_third_setting_has_user_value(settings, SETTING_PLACEMENT_INITIALIZED) ||
		!obs_data_get_bool(settings, SETTING_PLACEMENT_INITIALIZED);
	obs_data_set_bool(settings, SETTING_PLACEMENT_INITIALIZED, true);

	ctx->texture = NULL;
	ctx->texture_width = 0;
	ctx->texture_height = 0;
	ctx->lottie_fps = 30.0f;

	ctx->custom_file = obs_data_get_bool(settings, SETTING_CUSTOM_FILE);
	ctx->auto_hide_on_scene_transition = obs_data_get_bool(settings, SETTING_AUTO_HIDE_ON_SCENE_TRANSITION);
	const char *style_path = obs_data_get_string(settings, SETTING_STYLE_PATH);
	const char *lottie_path = lottie_lower_third_get_effective_lottie_path_from_settings(settings);

	ctx->style_path = bstrdup(style_path ? style_path : "");

	if (lottie_path && strlen(lottie_path) > 0) {
		ctx->lottie_path = bstrdup(lottie_path);
	} else {
		ctx->lottie_path = NULL;
	}

	const char *text1 = obs_data_get_string(settings, "text1");
	const char *text2 = obs_data_get_string(settings, "text2");
	ctx->text1_value = bstrdup(text1 && *text1 ? text1 : "NAME");
	ctx->text2_value = bstrdup(text2 && *text2 ? text2 : "Title");

	ctx->state = STATE_HIDDEN;

	lottie_lower_third_connect_scene_signals(ctx);
	lottie_lower_third_connect_global_source_create(ctx);
	lottie_lower_third_request_preload(ctx);
	return ctx;
}

static void lottie_lower_third_update(void *data, obs_data_t *settings)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;

	bool changed = false;

	obs_data_set_bool(settings, SETTING_PLACEMENT_INITIALIZED, true);

	const char *style_path = obs_data_get_string(settings, SETTING_STYLE_PATH);
	const char *custom_path = obs_data_get_string(settings, SETTING_LOTTIE_PATH);
	bool custom_file = obs_data_get_bool(settings, SETTING_CUSTOM_FILE);
	ctx->auto_hide_on_scene_transition = obs_data_get_bool(settings, SETTING_AUTO_HIDE_ON_SCENE_TRANSITION);

	const char *lottie_path = custom_file ? custom_path : style_path;
	const char *text1 = obs_data_get_string(settings, "text1");
	const char *text2 = obs_data_get_string(settings, "text2");

	pthread_mutex_lock(&ctx->load_mutex);

	if (ctx->custom_file != custom_file) {
		ctx->custom_file = custom_file;
		changed = true;
	}
	changed |= set_bstr(&ctx->style_path, style_path);
	changed |= set_bstr(&ctx->text1_value, text1);
	changed |= set_bstr(&ctx->text2_value, text2);

	if (lottie_path && *lottie_path) {
		if (!ctx->lottie_path || strcmp(ctx->lottie_path, lottie_path) != 0) {
			bfree(ctx->lottie_path);
			ctx->lottie_path = bstrdup(lottie_path);
			changed = true;
		}
	} else if (ctx->lottie_path) {
		bfree(ctx->lottie_path);
		ctx->lottie_path = NULL;
		lottie_lower_third_unload_lottie(ctx);
		changed = true;
	}

	if (changed) {
		ctx->preload_queued = false;
		ctx->preload_failed = false;
		ctx->reload_pending = true;
		ctx->reload_after_ns = os_gettime_ns() + TEXT_RELOAD_DEBOUNCE_NS;
	}

	pthread_mutex_unlock(&ctx->load_mutex);
}

static void lottie_lower_third_destroy(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;

	lottie_lower_third_disconnect_global_source_create(ctx);
	lottie_lower_third_disconnect_scene_signals(ctx);

	lottie_lower_third_detach_hide_transition_owner(ctx);
	if (ctx->hide_transition)
		obs_source_release(ctx->hide_transition);

	if (ctx->lottie_path)
		bfree(ctx->lottie_path);
	if (ctx->style_path)
		bfree(ctx->style_path);
	if (ctx->canvas)
		tvg_canvas_destroy(ctx->canvas);
	if (ctx->anim)
		tvg_animation_del(ctx->anim);
	lottie_lower_third_destroy_render_texture(ctx);
	if (ctx->buffer)
		bfree(ctx->buffer);
	if (ctx->text1_value)
		bfree(ctx->text1_value);
	if (ctx->text2_value)
		bfree(ctx->text2_value);
	if (ctx->load_mutex_initialized)
		pthread_mutex_destroy(&ctx->load_mutex);

	bfree(data);
}

static uint32_t lottie_lower_third_get_width(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	return (uint32_t)ctx->lottie_width;
}

static uint32_t lottie_lower_third_get_height(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	return (uint32_t)ctx->lottie_height;
}

static struct obs_source_info lottie_lower_third_info = {
	.id = "lottie_lower_third",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = lottie_lower_third_get_name,
	.create = lottie_lower_third_create,
	.destroy = lottie_lower_third_destroy,
	.get_width = lottie_lower_third_get_width,
	.get_height = lottie_lower_third_get_height,
	.show = lottie_lower_third_show,
	.hide = lottie_lower_third_hide,
	.video_tick = lottie_lower_third_video_tick,
	.video_render = lottie_lower_third_render,
	.get_properties = lottie_lower_third_get_properties,
	.update = lottie_lower_third_update,
	.get_defaults = lottie_lower_third_get_defaults,
};

void register_lottie_lower_third_source(void)
{
	lottie_lower_third_preload_system_init();
	obs_register_source(&lottie_lower_third_info);
	lottie_passthrough_transition_register();
}

void unregister_lottie_lower_third_source(void)
{
	lottie_lower_third_preload_system_destroy();
}
