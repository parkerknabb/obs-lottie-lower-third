#pragma once

#include <obs-module.h>

struct lottie_lower_third;

void lottie_lower_third_begin_exit(struct lottie_lower_third *ctx);
void lottie_lower_third_tick_exit(struct lottie_lower_third *ctx, uint32_t cx, uint32_t cy, gs_texture_t *a);
