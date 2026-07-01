#include "lottie-lower-third-internal.h"

#include "lottie-lower-third-lottie.h"
#include "lottie-lower-third-markers.h"
#include "lottie-lower-third-playback.h"
#include "lottie-lower-third-properties.h"
#include "lottie-lower-third-render.h"
#include "lottie-lower-third-scene.h"
#include "lottie-passthrough-transition.h"

#include <plugin-support.h>

#include <string.h>

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
	ctx->source = source;
	ctx->state = STATE_HIDDEN;
	ctx->suppress_hidden_preview = false;
	ctx->hold_start_frame = 30;

	ctx->buffer_width = 1920;
	ctx->buffer_height = 1080;
	ctx->buffer = bzalloc(ctx->buffer_width * ctx->buffer_height * sizeof(uint32_t));
	ctx->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
	tvg_swcanvas_set_target(ctx->canvas, ctx->buffer, ctx->buffer_width, ctx->buffer_width, ctx->buffer_height,
				TVG_COLORSPACE_ARGB8888);
	if (!ctx->canvas) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to create ThorVG canvas");
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
	ctx->anim = tvg_lottie_animation_new();
	if (!ctx->anim) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to create ThorVG animation");
		tvg_canvas_destroy(ctx->canvas);
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
	ctx->pic = tvg_animation_get_picture(ctx->anim);
	if (!ctx->pic) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to get ThorVG animation picture");
		tvg_animation_del(ctx->anim);
		tvg_canvas_destroy(ctx->canvas);
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
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
	if (ctx->lottie_path && *ctx->lottie_path) {
		if (!lottie_lower_third_load_with_current_text(ctx)) {
			blog(LOG_WARNING,
			     "[lottie_lower_third] create: Lottie was configured but could not be loaded for preview");
		}
	}
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

	const char *lottie_path = custom_file ? custom_path : style_path;
	const char *text1 = obs_data_get_string(settings, "text1");
	const char *text2 = obs_data_get_string(settings, "text2");

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

	if ((changed || !ctx->lottie_loaded) && ctx->lottie_path) {
		float old_frame = ctx->current_lottie_frame;
		enum lottie_lower_third_state old_state = ctx->state;

		if (lottie_lower_third_load_with_current_text(ctx)) {
			ctx->state = old_state;

			if (old_frame >= 0.0f && old_frame < ctx->total_frames)
				ctx->current_lottie_frame = old_frame;
			else if (ctx->state == STATE_HIDDEN)
				ctx->current_lottie_frame = lottie_lower_third_get_preview_frame(ctx);
		}
	}
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
	obs_register_source(&lottie_lower_third_info);
	lottie_passthrough_transition_register();
}

void unregister_lottie_lower_third_source(void) {}
