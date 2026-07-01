#pragma once

#include "lottie-lower-third-internal.h"

void lottie_lower_third_detach_hide_transition_owner(struct lottie_lower_third *ctx);
void lottie_lower_third_try_attach_hide_transition(struct lottie_lower_third *ctx);
void lottie_lower_third_connect_scene_signals(struct lottie_lower_third *ctx);
void lottie_lower_third_disconnect_scene_signals(struct lottie_lower_third *ctx);
void lottie_lower_third_connect_global_source_create(struct lottie_lower_third *ctx);
void lottie_lower_third_disconnect_global_source_create(struct lottie_lower_third *ctx);
