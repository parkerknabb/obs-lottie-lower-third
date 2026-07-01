#pragma once

#include "lottie-lower-third-internal.h"

bool lottie_lower_third_setting_has_user_value(obs_data_t *settings, const char *name);
const char *lottie_lower_third_get_effective_lottie_path_from_settings(obs_data_t *settings);
void lottie_lower_third_get_defaults(obs_data_t *settings);
obs_properties_t *lottie_lower_third_get_properties(void *data);
