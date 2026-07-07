#include "lottie-passthrough-transition.h"
#include "lottie-lower-third-exit.h"
#include "lottie-lower-third-internal.h"

#include <plugin-support.h>

struct passthrough_transition {
	obs_source_t *source;
	struct lottie_lower_third *owner;
};

static const char *passthrough_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Lottie Lower Third Hide Transition";
}

static void *passthrough_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	struct passthrough_transition *ctx = bzalloc(sizeof(struct passthrough_transition));
	ctx->source = source;
	ctx->owner = NULL;
	return ctx;
}

static void passthrough_destroy(void *data)
{
	bfree(data);
}

static void passthrough_start(void *data)
{
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;

	if (ctx->owner)
		lottie_lower_third_begin_exit(ctx->owner);
}

static void passthrough_stop(void *data)
{
	UNUSED_PARAMETER(data);
}

static void passthrough_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(b);
	UNUSED_PARAMETER(t);
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;

	if (!a)
		return;

	if (ctx->owner && ctx->owner->suppress_hide_transition_render)
		return;

	if (ctx->owner) {
		lottie_lower_third_tick_exit(ctx->owner, cx, cy, a);
		return;
	}

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(image, a);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(a, 0, cx, cy);
}

static void passthrough_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;
	obs_transition_video_render(ctx->source, passthrough_callback);
}

static bool passthrough_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio, uint32_t mixers,
				     size_t channels, size_t sample_rate)
{
	return obs_transition_audio_render(((struct passthrough_transition *)data)->source, ts_out, audio, mixers,
					   channels, sample_rate, NULL, NULL);
}

static uint32_t passthrough_get_width(void *data)
{
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;
	uint32_t cx, cy;
	obs_transition_get_size(ctx->source, &cx, &cy);
	return cx;
}

static uint32_t passthrough_get_height(void *data)
{
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;
	uint32_t cx, cy;
	obs_transition_get_size(ctx->source, &cx, &cy);
	return cy;
}

static struct obs_source_info passthrough_transition_info = {
	.id = "lottie_lower_third_passthrough_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = passthrough_get_name,
	.create = passthrough_create,
	.destroy = passthrough_destroy,
	.video_render = passthrough_render,
	.audio_render = passthrough_audio_render,
	.get_width = passthrough_get_width,
	.get_height = passthrough_get_height,
	.transition_start = passthrough_start,
	.transition_stop = passthrough_stop,
};

void lottie_passthrough_transition_register(void)
{
	obs_register_source(&passthrough_transition_info);
}

obs_source_t *lottie_passthrough_transition_create(struct lottie_lower_third *owner)
{
	obs_source_t *transition = obs_source_create_private("lottie_lower_third_passthrough_transition",
							     "lower_third_hide_transition", NULL);

	if (!transition)
		return NULL;

	struct passthrough_transition *td = (struct passthrough_transition *)obs_obj_get_data(transition);
	if (td) {
		td->owner = owner;
	} else {
		blog(LOG_ERROR, "[lottie_lower_third] attach: could not get transition instance data");
	}

	return transition;
}

void lottie_passthrough_transition_clear_owner(obs_source_t *transition, struct lottie_lower_third *owner)
{
	if (!transition)
		return;

	struct passthrough_transition *td = (struct passthrough_transition *)obs_obj_get_data(transition);
	if (td && td->owner == owner)
		td->owner = NULL;
}
