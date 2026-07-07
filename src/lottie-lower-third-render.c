#include "lottie-lower-third-render.h"

#include "lottie-lower-third-exit.h"

#include <plugin-support.h>

#include <string.h>

static double get_global_framerate(void)
{
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_num != 0 && ovi.fps_den != 0) {
		return (double)ovi.fps_num / (double)ovi.fps_den;
	}
	return 30.0f;
}

static bool ensure_render_texture(struct lottie_lower_third *ctx)
{
	if (!ctx || !ctx->buffer)
		return false;

	uint32_t w = (uint32_t)ctx->buffer_width;
	uint32_t h = (uint32_t)ctx->buffer_height;

	if (w == 0 || h == 0)
		return false;

	if (ctx->texture && ctx->texture_width == w && ctx->texture_height == h) {
		return true;
	}

	if (ctx->texture) {
		gs_texture_destroy(ctx->texture);
		ctx->texture = NULL;
	}

	const uint8_t *planes[1] = {
		(const uint8_t *)ctx->buffer,
	};

	ctx->texture = gs_texture_create(w, h, GS_BGRA, 1, planes, GS_DYNAMIC);

	if (!ctx->texture) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to create render texture %ux%u", w, h);
		return false;
	}

	ctx->texture_width = w;
	ctx->texture_height = h;

	return true;
}

bool lottie_lower_third_render_buffer_to_obs(struct lottie_lower_third *ctx, uint32_t draw_width, uint32_t draw_height)
{
	if (!ctx || !ctx->buffer)
		return false;

	if (!ensure_render_texture(ctx))
		return false;

	gs_texture_set_image(ctx->texture, (const uint8_t *)ctx->buffer, (uint32_t)ctx->buffer_width * 4, false);

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!default_effect)
		return false;

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");

	if (!image)
		return false;

	gs_effect_set_texture(image, ctx->texture);

	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(ctx->texture, 0, draw_width, draw_height);

	return true;
}

bool lottie_lower_third_render_existing_texture(struct lottie_lower_third *ctx, uint32_t draw_width,
						uint32_t draw_height)
{
	if (!ctx || !ctx->texture)
		return false;

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!default_effect)
		return false;

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");

	if (!image)
		return false;

	gs_effect_set_texture(image, ctx->texture);

	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(ctx->texture, 0, draw_width, draw_height);

	return true;
}

bool lottie_lower_third_render_frame_to_buffer(struct lottie_lower_third *ctx, float frame)
{
	if (!ctx || !ctx->lottie_loaded || !ctx->anim || !ctx->canvas || !ctx->buffer)
		return false;

	if (ctx->total_frames > 1.0f) {
		if (frame < 0.0f)
			frame = 0.0f;

		if (frame > ctx->total_frames - 1.0f)
			frame = ctx->total_frames - 1.0f;
	}

	Tvg_Result result;

	result = tvg_animation_set_frame(ctx->anim, frame);
	if (result == TVG_RESULT_INSUFFICIENT_CONDITION) {
		ctx->set_frame_failure_count = 0;
	} else if (result != TVG_RESULT_SUCCESS) {
		ctx->set_frame_failure_count++;
		if (ctx->set_frame_failure_count == 1) {
			blog(LOG_WARNING,
			     "[lottie_lower_third] tvg_animation_set_frame failed at frame %.3f; preserving previous rendered frame %.3f",
			     frame, ctx->last_rendered_lottie_frame);
		}
		return true;
	} else {
		ctx->set_frame_failure_count = 0;
	}

	size_t buffer_bytes = (size_t)ctx->buffer_width * (size_t)ctx->buffer_height * sizeof(uint32_t);

	memset(ctx->buffer, 0, buffer_bytes);

	result = tvg_canvas_update(ctx->canvas);
	if (result != TVG_RESULT_SUCCESS) {
		if (!ctx->warned_canvas_update_failure) {
			blog(LOG_WARNING, "[lottie_lower_third] tvg_canvas_update failed");
			ctx->warned_canvas_update_failure = true;
		}
		return false;
	}
	ctx->warned_canvas_update_failure = false;

	result = tvg_canvas_draw(ctx->canvas, true);
	if (result != TVG_RESULT_SUCCESS) {
		if (!ctx->warned_canvas_draw_failure) {
			blog(LOG_WARNING, "[lottie_lower_third] tvg_canvas_draw failed");
			ctx->warned_canvas_draw_failure = true;
		}
		return false;
	}
	ctx->warned_canvas_draw_failure = false;

	result = tvg_canvas_sync(ctx->canvas);
	if (result != TVG_RESULT_SUCCESS) {
		if (!ctx->warned_canvas_sync_failure) {
			blog(LOG_WARNING, "[lottie_lower_third] tvg_canvas_sync failed");
			ctx->warned_canvas_sync_failure = true;
		}
		return false;
	}
	ctx->warned_canvas_sync_failure = false;

	ctx->last_rendered_lottie_frame = frame;
	return true;
}

void lottie_lower_third_destroy_render_texture(struct lottie_lower_third *ctx)
{
	if (!ctx || !ctx->texture)
		return;

	obs_enter_graphics();
	gs_texture_destroy(ctx->texture);
	obs_leave_graphics();

	ctx->texture = NULL;
	ctx->texture_width = 0;
	ctx->texture_height = 0;
}

void lottie_lower_third_tick_exit(struct lottie_lower_third *ctx, uint32_t cx, uint32_t cy, gs_texture_t *a)
{
	if (ctx->state == STATE_EXITING) {
		if (ctx->lottie_loaded) {
			float frame_advance = ctx->lottie_fps / (float)get_global_framerate();
			float next_frame = ctx->current_lottie_frame + frame_advance;

			if (ctx->hold_end_frame_lottie >= 0.0f && ctx->outro_start_frame >= 0.0f &&
			    ctx->outro_start_frame > ctx->hold_end_frame_lottie &&
			    ctx->current_lottie_frame < ctx->hold_end_frame_lottie &&
			    next_frame >= ctx->hold_end_frame_lottie) {
				next_frame = ctx->outro_start_frame;
			}

			if (next_frame >= ctx->total_frames - 1.0f) {
				ctx->current_lottie_frame = ctx->total_frames - 1.0f;
				ctx->state = STATE_HIDDEN;
			} else {
				ctx->current_lottie_frame = next_frame;
			}
		}

		ctx->current_frame++;
	}

	if (!a)
		return;

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!default_effect)
		return;

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
	if (!image)
		return;

	gs_effect_set_texture(image, a);

	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(a, 0, cx, cy);
}
