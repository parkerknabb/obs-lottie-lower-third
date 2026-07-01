#pragma once

#include <obs-module.h>

struct lottie_lower_third;

void lottie_passthrough_transition_register(void);
obs_source_t *lottie_passthrough_transition_create(struct lottie_lower_third *owner);
void lottie_passthrough_transition_clear_owner(obs_source_t *transition, struct lottie_lower_third *owner);
