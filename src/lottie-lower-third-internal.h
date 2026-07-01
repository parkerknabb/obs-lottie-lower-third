#pragma once

#include <obs-module.h>
#include <thorvg_capi.h>

#include <stdbool.h>
#include <stdint.h>

#define SETTING_STYLE_PATH "lottie_style"
#define SETTING_CUSTOM_FILE "custom_file"
#define SETTING_LOTTIE_PATH "lottie_path"
#define SETTING_PLACEMENT_INITIALIZED "__lottie_lower_third_placement_initialized"

enum lottie_lower_third_state {
	STATE_HIDDEN,
	STATE_ENTERING,
	STATE_HOLDING,
	STATE_EXITING,
};

struct lottie_lower_third {
	obs_source_t *source;
	obs_source_t *hide_transition;
	enum lottie_lower_third_state state;
	uint32_t current_frame;
	uint32_t hold_start_frame;
	uint32_t exit_start_frame;
	uint32_t end_frame;

	gs_texture_t *texture;
	uint32_t texture_width;
	uint32_t texture_height;

	char *lottie_path;
	char *style_path;
	bool custom_file;
	Tvg_Canvas canvas;
	Tvg_Animation anim;
	Tvg_Paint pic;
	float lottie_width, lottie_height;
	uint32_t *buffer;
	int buffer_width, buffer_height;

	float lottie_fps;
	float total_frames;
	float intro_start_frame;
	float hold_start_frame_lottie;
	float hold_end_frame_lottie;
	float outro_start_frame;
	float pvw_time_frame;

	float current_lottie_frame;
	bool lottie_loaded;
	bool is_looping_hold;
	bool has_hold_end_marker;
	bool suppress_hidden_preview;
	float last_rendered_lottie_frame;
	uint32_t set_frame_failure_count;
	bool warned_canvas_update_failure;
	bool warned_canvas_draw_failure;
	bool warned_canvas_sync_failure;

	char *text1_value;
	char *text2_value;
	bool has_text1;
	bool has_text2;
	bool default_placement_pending;
};
