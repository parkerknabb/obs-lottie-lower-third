#pragma once

#include "lottie-lower-third-internal.h"

void lottie_lower_third_destroy_render_texture(struct lottie_lower_third *ctx);
bool lottie_lower_third_render_frame_to_buffer(struct lottie_lower_third *ctx, float frame);
bool lottie_lower_third_render_buffer_to_obs(struct lottie_lower_third *ctx, uint32_t draw_width, uint32_t draw_height);
