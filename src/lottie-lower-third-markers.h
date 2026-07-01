#pragma once

#include "lottie-lower-third-internal.h"
#include <cJSON.h>

void lottie_lower_third_reset_markers(struct lottie_lower_third *ctx);
void lottie_lower_third_apply_marker_defaults(struct lottie_lower_third *ctx);
bool lottie_lower_third_parse_markers_from_json(cJSON *root, struct lottie_lower_third *ctx);
void lottie_lower_third_parse_markers_from_animation(struct lottie_lower_third *ctx);
float lottie_lower_third_stable_marker_render_frame(struct lottie_lower_third *ctx, float frame);
float lottie_lower_third_get_preview_frame(struct lottie_lower_third *ctx);
