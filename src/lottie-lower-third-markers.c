#include "lottie-lower-third-markers.h"

#include <string.h>

void lottie_lower_third_reset_markers(struct lottie_lower_third *ctx)
{
	ctx->intro_start_frame = 0.0f;
	ctx->hold_start_frame_lottie = -1.0f;
	ctx->hold_end_frame_lottie = -1.0f;
	ctx->outro_start_frame = -1.0f;
	ctx->pvw_time_frame = -1.0f;
	ctx->has_hold_end_marker = false;
}

void lottie_lower_third_apply_marker_defaults(struct lottie_lower_third *ctx)
{
	if (ctx->hold_start_frame_lottie < 0) {
		ctx->hold_start_frame_lottie = ctx->total_frames * 0.3f;
	}
	if (ctx->hold_end_frame_lottie < 0) {
		ctx->hold_end_frame_lottie = ctx->hold_start_frame_lottie;
	}
	if (ctx->pvw_time_frame < 0) {
		ctx->pvw_time_frame = ctx->hold_start_frame_lottie;
	}
	if (ctx->outro_start_frame < 0) {
		ctx->outro_start_frame = ctx->total_frames * 0.8f;
	}
}

static void set_lottie_marker(struct lottie_lower_third *ctx, const char *name, float frame)
{
	if (strcmp(name, "intro") == 0) {
		ctx->intro_start_frame = frame;
	} else if (strcmp(name, "hold start") == 0 || strcmp(name, "hold_start") == 0) {
		ctx->hold_start_frame_lottie = frame;
	} else if (strcmp(name, "hold end") == 0 || strcmp(name, "hold_end") == 0) {
		ctx->hold_end_frame_lottie = frame;
		ctx->has_hold_end_marker = true;
	} else if (strcmp(name, "outro") == 0) {
		ctx->outro_start_frame = frame;
	} else if (strcmp(name, "pvw time") == 0 || strcmp(name, "pvw_time") == 0) {
		ctx->pvw_time_frame = frame;
	}
}

bool lottie_lower_third_parse_markers_from_json(cJSON *root, struct lottie_lower_third *ctx)
{
	cJSON *markers = root ? cJSON_GetObjectItemCaseSensitive(root, "markers") : NULL;
	if (!cJSON_IsArray(markers))
		return false;

	lottie_lower_third_reset_markers(ctx);

	int marker_count = cJSON_GetArraySize(markers);

	cJSON *marker = NULL;
	cJSON_ArrayForEach(marker, markers)
	{
		cJSON *name = cJSON_GetObjectItemCaseSensitive(marker, "cm");
		cJSON *time = cJSON_GetObjectItemCaseSensitive(marker, "tm");

		if (!cJSON_IsString(name) || !cJSON_IsNumber(time))
			continue;

		set_lottie_marker(ctx, name->valuestring, (float)time->valuedouble);
	}

	return marker_count > 0;
}

void lottie_lower_third_parse_markers_from_animation(struct lottie_lower_third *ctx)
{
	if (!ctx->anim)
		return;

	lottie_lower_third_reset_markers(ctx);

	uint32_t marker_count = 0;
	tvg_lottie_animation_get_markers_cnt(ctx->anim, &marker_count);

	for (uint32_t i = 0; i < marker_count; i++) {
		const char *name = NULL;
		float begin = 0, end = 0;
		tvg_lottie_animation_get_marker_info(ctx->anim, i, &name, &begin, &end);

		if (name) {
			set_lottie_marker(ctx, name, begin);
		}
	}

	lottie_lower_third_apply_marker_defaults(ctx);
}

float lottie_lower_third_stable_marker_render_frame(struct lottie_lower_third *ctx, float frame)
{
	if (ctx && ctx->total_frames > 1.0f && frame >= 0.0f && frame < ctx->total_frames - 1.0f) {
		return frame + 0.001f;
	}

	return frame;
}

float lottie_lower_third_get_preview_frame(struct lottie_lower_third *ctx)
{
	if (!ctx || ctx->total_frames <= 1.0f)
		return 0.0f;

	if (ctx->pvw_time_frame >= 0.0f && ctx->pvw_time_frame < ctx->total_frames) {
		return ctx->pvw_time_frame;
	}

	if (ctx->hold_start_frame_lottie >= 0.0f && ctx->hold_start_frame_lottie < ctx->total_frames) {
		return ctx->hold_start_frame_lottie;
	}

	if (ctx->hold_start_frame_lottie >= 0.0f && ctx->hold_end_frame_lottie > ctx->hold_start_frame_lottie &&
	    ctx->hold_end_frame_lottie < ctx->total_frames) {
		return (ctx->hold_start_frame_lottie + ctx->hold_end_frame_lottie) * 0.5f;
	}

	return ctx->total_frames * 0.3f;
}
