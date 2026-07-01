#include "lottie-lower-third-playback.h"

#include "lottie-lower-third-lottie.h"
#include "lottie-lower-third-markers.h"
#include "lottie-lower-third-render.h"
#include "lottie-lower-third-scene.h"

#include <plugin-support.h>

void lottie_lower_third_show(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;

	ctx->state = STATE_ENTERING;
	ctx->suppress_hidden_preview = false;
	ctx->current_frame = 0;
	ctx->is_looping_hold = false;

	if (!ctx->lottie_path) {
		return;
	}

	if (!ctx->lottie_loaded) {
		if (!lottie_lower_third_load_with_current_text(ctx)) {
			blog(LOG_ERROR, "[lottie_lower_third] Failed to load patched Lottie");
			return;
		}
	}

	ctx->current_lottie_frame = ctx->intro_start_frame;
	ctx->state = STATE_ENTERING;
}

void lottie_lower_third_hide(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	ctx->suppress_hidden_preview = true;
}

void lottie_lower_third_begin_exit(struct lottie_lower_third *ctx)
{
	if (ctx->state == STATE_HOLDING || ctx->state == STATE_ENTERING) {
		ctx->state = STATE_EXITING;
		ctx->suppress_hidden_preview = true;
		ctx->is_looping_hold = false;
		ctx->current_frame = 0;
	}
}

void lottie_lower_third_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;

	if (!ctx->lottie_loaded)
		return;

	float frame_advance = ctx->lottie_fps * seconds;

	switch (ctx->state) {
	case STATE_ENTERING:
		ctx->current_lottie_frame += frame_advance;
		ctx->current_frame++;

		if (ctx->current_lottie_frame >= ctx->hold_start_frame_lottie) {
			ctx->state = STATE_HOLDING;
			ctx->is_looping_hold = ctx->has_hold_end_marker &&
					       ctx->hold_end_frame_lottie > ctx->hold_start_frame_lottie;
			ctx->current_lottie_frame =
				lottie_lower_third_stable_marker_render_frame(ctx, ctx->hold_start_frame_lottie);
			lottie_lower_third_try_attach_hide_transition(ctx);
		}
		break;

	case STATE_HOLDING:
		if (ctx->is_looping_hold) {
			ctx->current_lottie_frame += frame_advance;
			if (ctx->current_lottie_frame > ctx->hold_end_frame_lottie) {
				ctx->current_lottie_frame = lottie_lower_third_stable_marker_render_frame(
					ctx, ctx->hold_start_frame_lottie);
			}
		} else {
			ctx->current_lottie_frame =
				lottie_lower_third_stable_marker_render_frame(ctx, ctx->hold_start_frame_lottie);
		}
		break;

	case STATE_EXITING:
		break;

	case STATE_HIDDEN:
		break;
	}
}

void lottie_lower_third_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	if (!ctx)
		return;

	if (!ctx->lottie_loaded)
		return;

	if (ctx->state == STATE_HIDDEN && ctx->suppress_hidden_preview)
		return;

	float frame = ctx->current_lottie_frame;

	if (ctx->state == STATE_HIDDEN)
		frame = lottie_lower_third_get_preview_frame(ctx);

	if (!lottie_lower_third_render_frame_to_buffer(ctx, frame))
		return;

	lottie_lower_third_render_buffer_to_obs(ctx, (uint32_t)ctx->buffer_width, (uint32_t)ctx->buffer_height);
}
