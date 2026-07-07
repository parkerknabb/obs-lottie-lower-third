#pragma once

#include "lottie-lower-third-internal.h"

void lottie_lower_third_unload_lottie(struct lottie_lower_third *ctx);
bool lottie_lower_third_load_with_current_text(struct lottie_lower_third *ctx);
void lottie_lower_third_preload_system_init(void);
void lottie_lower_third_preload_system_destroy(void);
void lottie_lower_third_request_preload(struct lottie_lower_third *ctx);
bool lottie_lower_third_is_loaded(struct lottie_lower_third *ctx);
void lottie_lower_third_request_pending_preload(struct lottie_lower_third *ctx, bool force);
