#pragma once

#include "lottie-lower-third-internal.h"

void lottie_lower_third_show(void *data);
void lottie_lower_third_hide(void *data);
void lottie_lower_third_video_tick(void *data, float seconds);
void lottie_lower_third_render(void *data, gs_effect_t *effect);
void lottie_lower_third_begin_exit(struct lottie_lower_third *ctx);
