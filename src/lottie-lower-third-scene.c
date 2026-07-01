#include "lottie-lower-third-scene.h"

#include "lottie-lower-third-playback.h"
#include "lottie-passthrough-transition.h"

#include <graphics/vec2.h>
#include <plugin-support.h>

struct find_item_data {
	obs_source_t *target;
	obs_sceneitem_t *result;
};

static bool find_item_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct find_item_data *fd = (struct find_item_data *)data;
	if (obs_sceneitem_get_source(item) == fd->target) {
		fd->result = item;
		return false;
	}
	return true;
}

struct find_item_in_scenes_data {
	obs_source_t *target;
	obs_sceneitem_t *result;
};

static bool find_item_in_scenes_cb(void *data, obs_source_t *scene_source)
{
	struct find_item_in_scenes_data *fd = (struct find_item_in_scenes_data *)data;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return true;

	struct find_item_data find = {.target = fd->target, .result = NULL};
	obs_scene_enum_items(scene, find_item_cb, &find);

	if (find.result) {
		fd->result = find.result;
		return false;
	}
	return true;
}

void lottie_lower_third_detach_hide_transition_owner(struct lottie_lower_third *ctx)
{
	if (!ctx || !ctx->hide_transition)
		return;

	lottie_passthrough_transition_clear_owner(ctx->hide_transition, ctx);
}

static uint32_t get_exit_duration_ms(struct lottie_lower_third *ctx)
{
	if (ctx->lottie_loaded && ctx->total_frames > 0) {
		float outro_frames = ctx->total_frames - ctx->hold_start_frame_lottie;
		if (outro_frames > 0) {
			float lottie_fps = 30.0f;
			uint32_t duration_ms = (uint32_t)((outro_frames / lottie_fps) * 1000.0f);
			return duration_ms;
		}
	}

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.fps_den == 0) {
		blog(LOG_WARNING, "[lottie_lower_third] get_exit_duration_ms: using 1000ms fallback");
		return 1000;
	}
	float exit_frames = (float)ctx->end_frame - ctx->outro_start_frame;
	if (exit_frames <= 0.0f)
		return 0;

	uint32_t ms = (uint32_t)((double)exit_frames * ovi.fps_den / ovi.fps_num * 1000.0);
	return ms;
}

static void attach_hide_transition(struct lottie_lower_third *ctx, obs_sceneitem_t *item)
{
	if (ctx->hide_transition) {
		lottie_lower_third_detach_hide_transition_owner(ctx);
		obs_source_release(ctx->hide_transition);
		ctx->hide_transition = NULL;
	}

	ctx->hide_transition = lottie_passthrough_transition_create(ctx);

	if (!ctx->hide_transition) {
		blog(LOG_ERROR, "[lottie_lower_third] attach: create_private FAILED");
		return;
	}

	uint32_t duration_ms = get_exit_duration_ms(ctx);

	obs_sceneitem_set_transition(item, false, ctx->hide_transition);
	obs_sceneitem_set_transition_duration(item, false, duration_ms);

	obs_source_t *check = obs_sceneitem_get_transition(item, false);
	if (check != ctx->hide_transition)
		blog(LOG_ERROR, "[lottie_lower_third] attach: READBACK MISMATCH");
}

void lottie_lower_third_try_attach_hide_transition(struct lottie_lower_third *ctx)
{
	struct find_item_in_scenes_data fd = {
		.target = ctx->source,
		.result = NULL,
	};
	obs_enum_scenes(find_item_in_scenes_cb, &fd);

	if (fd.result) {
		attach_hide_transition(ctx, fd.result);
	}
}

static void scene_item_visible(void *data, calldata_t *params)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	obs_sceneitem_t *item = calldata_ptr(params, "item");
	bool visible = calldata_bool(params, "visible");

	if (!item || obs_sceneitem_get_source(item) != ctx->source)
		return;

	if (ctx->hide_transition) {
		obs_sceneitem_set_transition_duration(item, false, get_exit_duration_ms(ctx));
	}

	if (visible) {
		lottie_lower_third_show(ctx);
	} else {
		lottie_lower_third_hide(ctx);
	}
}

static void place_sceneitem_bottom_left(struct lottie_lower_third *ctx, obs_sceneitem_t *item)
{
	if (!ctx || !item)
		return;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	struct vec2 current_pos;
	obs_sceneitem_get_pos(item, &current_pos);
	if (current_pos.x != 0.0f || current_pos.y != 0.0f)
		return;

	float source_height = ctx->lottie_height > 0.0f ? ctx->lottie_height : (float)ctx->buffer_height;
	struct vec2 pos = {
		.x = 0.0f,
		.y = (float)ovi.base_height - source_height,
	};

	if (pos.y < 0.0f)
		pos.y = 0.0f;

	obs_sceneitem_set_pos(item, &pos);
}

static void scene_item_add(void *data, calldata_t *params)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	obs_sceneitem_t *item = calldata_ptr(params, "item");

	if (!ctx || !ctx->default_placement_pending || !item || obs_sceneitem_get_source(item) != ctx->source)
		return;

	place_sceneitem_bottom_left(ctx, item);
	ctx->default_placement_pending = false;
}

static void connect_scene_signals(struct lottie_lower_third *ctx, obs_source_t *scene_source)
{
	if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source))
		return;
	signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
	signal_handler_connect(handler, "item_add", scene_item_add, ctx);
	signal_handler_connect(handler, "item_visible", scene_item_visible, ctx);
}

static void disconnect_scene_signals(struct lottie_lower_third *ctx, obs_source_t *scene_source)
{
	if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source))
		return;
	signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
	signal_handler_disconnect(handler, "item_add", scene_item_add, ctx);
	signal_handler_disconnect(handler, "item_visible", scene_item_visible, ctx);
}

static bool connect_scene_enum(void *data, obs_source_t *scene_source)
{
	connect_scene_signals((struct lottie_lower_third *)data, scene_source);
	return true;
}

static bool disconnect_scene_enum(void *data, obs_source_t *scene_source)
{
	disconnect_scene_signals((struct lottie_lower_third *)data, scene_source);
	return true;
}

static void global_source_create(void *data, calldata_t *params)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	obs_source_t *source = calldata_ptr(params, "source");
	if (source)
		connect_scene_signals(ctx, source);
}

void lottie_lower_third_connect_scene_signals(struct lottie_lower_third *ctx)
{
	obs_enum_scenes(connect_scene_enum, ctx);
}

void lottie_lower_third_disconnect_scene_signals(struct lottie_lower_third *ctx)
{
	obs_enum_scenes(disconnect_scene_enum, ctx);
}

void lottie_lower_third_connect_global_source_create(struct lottie_lower_third *ctx)
{
	signal_handler_connect(obs_get_signal_handler(), "source_create", global_source_create, ctx);
}

void lottie_lower_third_disconnect_global_source_create(struct lottie_lower_third *ctx)
{
	signal_handler_disconnect(obs_get_signal_handler(), "source_create", global_source_create, ctx);
}
