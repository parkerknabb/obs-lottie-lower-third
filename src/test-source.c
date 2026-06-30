#include <obs-module.h>
#include <plugin-support.h>
#include <thorvg_capi.h>
#include <stdio.h>
#include <string.h>
#include <cJSON.h>

// ─────────────────────────────────────────────────────────
// Passthrough hide transition
// ─────────────────────────────────────────────────────────

struct test_source; // forward declaration
static void test_source_tick_exit(struct test_source *ctx, uint32_t cx, uint32_t cy, gs_texture_t *a);
static void test_source_begin_exit(struct test_source *ctx);

struct passthrough_transition {
	obs_source_t *source;
	struct test_source *owner;
	uint32_t frame_count; // per-instance instead of static
};

static const char *passthrough_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Lower Third Passthrough";
}

static void *passthrough_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);
	blog(LOG_INFO, "[passthrough] create: source=%p", (void *)source);
	struct passthrough_transition *ctx = bzalloc(sizeof(struct passthrough_transition));
	ctx->source = source;
	ctx->owner = NULL;
	ctx->frame_count = 0;
	return ctx;
}

static void passthrough_destroy(void *data)
{
	blog(LOG_INFO, "[passthrough] destroy");
	bfree(data);
}

static void passthrough_start(void *data)
{
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;
	blog(LOG_INFO, "[passthrough] transition_start");

	// Signal the owner source to begin its exit animation
	if (ctx->owner)
		test_source_begin_exit(ctx->owner);
}

static void passthrough_stop(void *data)
{
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;
	blog(LOG_INFO, "[passthrough] transition_stop");
	UNUSED_PARAMETER(ctx);
}

static void passthrough_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	UNUSED_PARAMETER(b);
	struct passthrough_transition *ctx = (struct passthrough_transition *)data;

	ctx->frame_count++;
	if (ctx->frame_count % 30 == 0) {
		blog(LOG_INFO, "[passthrough] callback frame=%u t=%.3f a=%p cx=%u cy=%u", ctx->frame_count, t,
		     (void *)a, cx, cy);
	}

	if (!a) {
		blog(LOG_WARNING, "[passthrough] callback: texture A is NULL");
		return;
	}

	if (ctx->owner) {
		test_source_tick_exit(ctx->owner, cx, cy, a);
		return;
	}

	// Fallback: just draw the texture directly
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
	.id = "test_passthrough_transition",
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

// ─────────────────────────────────────────────────────────
// Test lower third source
// ─────────────────────────────────────────────────────────

enum test_state {
	STATE_HIDDEN,
	STATE_ENTERING,
	STATE_HOLDING,
	STATE_EXITING,
};

struct test_source {
	obs_source_t *source;
	obs_source_t *hide_transition;
	enum test_state state;
	uint32_t current_frame;
	uint32_t hold_start_frame;
	uint32_t exit_start_frame;
	uint32_t end_frame;

	gs_texture_t *texture;
	uint32_t texture_width;
	uint32_t texture_height;

	// Lottie-specific fields
	char *lottie_path;
	Tvg_Canvas canvas;
	Tvg_Animation anim;
	Tvg_Paint pic;
	float lottie_width, lottie_height;
	uint32_t *buffer;
	int buffer_width, buffer_height;

	// Marker information
	float lottie_fps;
	float total_frames;
	float intro_start_frame;
	float hold_start_frame_lottie;
	float hold_end_frame_lottie;
	float outro_start_frame;
	float pvw_time_frame;

	// Animation state
	float current_lottie_frame;
	bool lottie_loaded;
	bool is_looping_hold;
	bool has_hold_end_marker;
	bool suppress_hidden_preview;
	float last_rendered_lottie_frame;
	uint32_t set_frame_failure_count;

	// Text layer info
	char *text1_value;
	char *text2_value;
	bool has_text1;
	bool has_text2;
};

static void scene_item_visible(void *data, calldata_t *params);
static void global_source_create(void *data, calldata_t *params);

static void detach_hide_transition_owner(struct test_source *ctx)
{
	if (!ctx || !ctx->hide_transition)
		return;

	struct passthrough_transition *td = (struct passthrough_transition *)obs_obj_get_data(ctx->hide_transition);

	if (td && td->owner == ctx)
		td->owner = NULL;
}

// ── Scene item search ─────────────────────────────────────

struct find_item_data {
	obs_source_t *target;
	obs_sceneitem_t *result;
};

static bool find_item_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *data)
{
	UNUSED_PARAMETER(scene);
	struct find_item_data *fd = (struct find_item_data *)data;
	if (obs_sceneitem_get_source(item) == fd->target) {
		fd->result = item;
		return false;
	}
	return true;
}

struct find_item_in_scenes_data {
	obs_source_t *target;
	obs_sceneitem_t *result;
};

static bool find_item_in_scenes_cb(void *data, obs_source_t *scene_source)
{
	struct find_item_in_scenes_data *fd = (struct find_item_in_scenes_data *)data;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return true;

	struct find_item_data find = {.target = fd->target, .result = NULL};
	obs_scene_enum_items(scene, find_item_cb, &find);

	if (find.result) {
		fd->result = find.result;
		return false;
	}
	return true;
}

// ── Marker parsing ────────────────────────────────────────

static void reset_lottie_markers(struct test_source *ctx)
{
	ctx->intro_start_frame = 0.0f;
	ctx->hold_start_frame_lottie = -1.0f;
	ctx->hold_end_frame_lottie = -1.0f;
	ctx->outro_start_frame = -1.0f;
	ctx->pvw_time_frame = -1.0f;
	ctx->has_hold_end_marker = false;
}

static void apply_lottie_marker_defaults(struct test_source *ctx)
{
	if (ctx->hold_start_frame_lottie < 0) {
		ctx->hold_start_frame_lottie = ctx->total_frames * 0.3f;
	}
	if (ctx->hold_end_frame_lottie < 0) {
		ctx->hold_end_frame_lottie = ctx->hold_start_frame_lottie;
	}
	if (ctx->pvw_time_frame < 0) {
		ctx->pvw_time_frame = ctx->hold_start_frame_lottie;
	}
	if (ctx->outro_start_frame < 0) {
		ctx->outro_start_frame = ctx->total_frames * 0.8f;
	}

	blog(LOG_INFO,
	     "[test_source] Markers parsed - intro: %.1f, hold start: %.1f, hold end: %.1f%s, pvw time: %.1f, outro: %.1f",
	     ctx->intro_start_frame, ctx->hold_start_frame_lottie, ctx->hold_end_frame_lottie,
	     ctx->has_hold_end_marker ? "" : " (static hold)", ctx->pvw_time_frame, ctx->outro_start_frame);
}

static void set_lottie_marker(struct test_source *ctx, const char *name, float frame)
{
	if (strcmp(name, "intro") == 0) {
		ctx->intro_start_frame = frame;
	} else if (strcmp(name, "hold start") == 0 || strcmp(name, "hold_start") == 0) {
		ctx->hold_start_frame_lottie = frame;
	} else if (strcmp(name, "hold end") == 0 || strcmp(name, "hold_end") == 0) {
		ctx->hold_end_frame_lottie = frame;
		ctx->has_hold_end_marker = true;
	} else if (strcmp(name, "outro") == 0) {
		ctx->outro_start_frame = frame;
	} else if (strcmp(name, "pvw time") == 0 || strcmp(name, "pvw_time") == 0) {
		ctx->pvw_time_frame = frame;
	}
}

static bool parse_lottie_markers_from_json(cJSON *root, struct test_source *ctx)
{
	cJSON *markers = root ? cJSON_GetObjectItemCaseSensitive(root, "markers") : NULL;
	if (!cJSON_IsArray(markers))
		return false;

	reset_lottie_markers(ctx);

	int marker_count = cJSON_GetArraySize(markers);
	blog(LOG_INFO, "[test_source] Found %d JSON markers", marker_count);

	cJSON *marker = NULL;
	cJSON_ArrayForEach(marker, markers)
	{
		cJSON *name = cJSON_GetObjectItemCaseSensitive(marker, "cm");
		cJSON *time = cJSON_GetObjectItemCaseSensitive(marker, "tm");

		if (!cJSON_IsString(name) || !cJSON_IsNumber(time))
			continue;

		blog(LOG_INFO, "[test_source] JSON marker: %s frame=%.1f", name->valuestring, time->valuedouble);
		set_lottie_marker(ctx, name->valuestring, (float)time->valuedouble);
	}

	return marker_count > 0;
}

static void parse_lottie_markers(struct test_source *ctx)
{
	if (!ctx->anim)
		return;

	reset_lottie_markers(ctx);

	uint32_t marker_count = 0;
	tvg_lottie_animation_get_markers_cnt(ctx->anim, &marker_count);

	blog(LOG_INFO, "[test_source] Found %u markers", marker_count);

	for (uint32_t i = 0; i < marker_count; i++) {
		const char *name = NULL;
		float begin = 0, end = 0;
		tvg_lottie_animation_get_marker_info(ctx->anim, i, &name, &begin, &end);

		if (name) {
			blog(LOG_INFO, "[test_source] Marker [%u]: %s begin=%.1f end=%.1f", i, name, begin, end);
			set_lottie_marker(ctx, name, begin);
		}
	}

	apply_lottie_marker_defaults(ctx);
}

// ───────────────────────────────────
// Text
// ───────────────────────────────────
static char *read_file_to_string(const char *path, size_t *size_out)
{
	if (!path || !*path)
		return NULL;

	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}

	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}

	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}

	char *data = bmalloc((size_t)size + 1);
	if (!data) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(data, 1, (size_t)size, f);
	fclose(f);

	if (read != (size_t)size) {
		bfree(data);
		return NULL;
	}

	data[read] = '\0';

	if (size_out)
		*size_out = read;

	return data;
}
static bool set_bstr(char **dst, const char *src)
{
	if (!src)
		src = "";

	if (*dst && strcmp(*dst, src) == 0)
		return false;

	bfree(*dst);
	*dst = bstrdup(src);

	return true;
}
static void test_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "text1", "Main Text");
	obs_data_set_default_string(settings, "text2", "Subtext");
}

static bool layer_matches_text_id(cJSON *layer, const char *wanted_nm, const char *wanted_ln)
{
	cJSON *ty = cJSON_GetObjectItemCaseSensitive(layer, "ty");
	if (!cJSON_IsNumber(ty) || ty->valueint != 5)
		return false;

	cJSON *nm = cJSON_GetObjectItemCaseSensitive(layer, "nm");
	cJSON *ln = cJSON_GetObjectItemCaseSensitive(layer, "ln");

	if (cJSON_IsString(nm) && strcmp(nm->valuestring, wanted_nm) == 0)
		return true;

	if (cJSON_IsString(ln) && strcmp(ln->valuestring, wanted_ln) == 0)
		return true;

	return false;
}
static bool patch_text_document_value(cJSON *layer, const char *new_text)
{
	cJSON *t = cJSON_GetObjectItemCaseSensitive(layer, "t");
	cJSON *d = t ? cJSON_GetObjectItemCaseSensitive(t, "d") : NULL;
	cJSON *k = d ? cJSON_GetObjectItemCaseSensitive(d, "k") : NULL;

	if (!cJSON_IsArray(k))
		return false;

	bool patched = false;

	cJSON *keyframe = NULL;
	cJSON_ArrayForEach(keyframe, k)
	{
		cJSON *s = cJSON_GetObjectItemCaseSensitive(keyframe, "s");
		cJSON *text = s ? cJSON_GetObjectItemCaseSensitive(s, "t") : NULL;

		if (cJSON_IsString(text)) {
			cJSON_SetValuestring(text, new_text ? new_text : "");
			patched = true;
		}
	}

	return patched;
}

static bool lottie_chars_has_glyph(cJSON *chars, char ch)
{
	if (!cJSON_IsArray(chars))
		return true;

	cJSON *glyph = NULL;
	cJSON_ArrayForEach(glyph, chars)
	{
		cJSON *glyph_ch = cJSON_GetObjectItemCaseSensitive(glyph, "ch");
		if (cJSON_IsString(glyph_ch) && glyph_ch->valuestring[0] == ch && glyph_ch->valuestring[1] == '\0') {
			return true;
		}
	}

	return false;
}

static void warn_missing_text_glyphs(cJSON *root, const char *layer_name, const char *text)
{
	cJSON *chars = root ? cJSON_GetObjectItemCaseSensitive(root, "chars") : NULL;
	if (!cJSON_IsArray(chars) || !text)
		return;

	char missing[128] = {0};
	size_t missing_len = 0;

	for (const char *p = text; *p; p++) {
		unsigned char ch = (unsigned char)*p;
		if (ch > 0x7f)
			continue;

		if (!lottie_chars_has_glyph(chars, (char)ch) && !strchr(missing, ch) &&
		    missing_len + 2 < sizeof(missing)) {
			missing[missing_len++] = (char)ch;
			missing[missing_len] = '\0';
		}
	}

	if (missing_len > 0) {
		blog(LOG_WARNING, "[test_source] %s patched text contains glyphs not embedded in Lottie chars: \"%s\"",
		     layer_name, missing);
	}
}
static void patch_lower_third_text_layer(cJSON *layer, struct test_source *ctx)
{
	if (!cJSON_IsObject(layer))
		return;

	if (layer_matches_text_id(layer, "NAME", "NAME")) {
		if (patch_text_document_value(layer, ctx->text1_value)) {
			ctx->has_text1 = true;
			blog(LOG_INFO, "[test_source] Patched NAME text layer");
		} else {
			blog(LOG_WARNING, "[test_source] Found NAME layer but could not patch text document");
		}
	}

	if (layer_matches_text_id(layer, "SUBTITLE", "SUBTITLE")) {
		if (patch_text_document_value(layer, ctx->text2_value)) {
			ctx->has_text2 = true;
			blog(LOG_INFO, "[test_source] Patched SUBTITLE text layer");
		} else {
			blog(LOG_WARNING, "[test_source] Found SUBTITLE layer but could not patch text document");
		}
	}
}
static bool is_text_layer(cJSON *layer, const char *primary_nm, const char *primary_ln, const char *fallback_nm,
			  const char *fallback_ln)
{
	cJSON *ty = cJSON_GetObjectItemCaseSensitive(layer, "ty");
	if (!cJSON_IsNumber(ty) || ty->valueint != 5)
		return false;

	cJSON *nm = cJSON_GetObjectItemCaseSensitive(layer, "nm");
	cJSON *ln = cJSON_GetObjectItemCaseSensitive(layer, "ln");

	if (cJSON_IsString(nm) && strcmp(nm->valuestring, primary_nm) == 0)
		return true;

	if (cJSON_IsString(ln) && strcmp(ln->valuestring, primary_ln) == 0)
		return true;

	if (fallback_nm && cJSON_IsString(nm) && strcmp(nm->valuestring, fallback_nm) == 0)
		return true;

	if (fallback_ln && cJSON_IsString(ln) && strcmp(ln->valuestring, fallback_ln) == 0)
		return true;

	return false;
}
static void patch_lottie_text_layers(cJSON *node, struct test_source *ctx)
{
	if (!node)
		return;

	if (cJSON_IsObject(node)) {
		cJSON *layers = cJSON_GetObjectItemCaseSensitive(node, "layers");

		if (cJSON_IsArray(layers)) {
			cJSON *layer = NULL;

			cJSON_ArrayForEach(layer, layers)
			{
				if (!cJSON_IsObject(layer))
					continue;

				if (is_text_layer(layer, "NAME", "NAME", "#text1", "text1")) {
					if (patch_text_document_value(layer, ctx->text1_value)) {
						ctx->has_text1 = true;
						blog(LOG_INFO, "[test_source] Patched NAME");
					}
				}

				if (is_text_layer(layer, "SUBTITLE", "SUBTITLE", "#text2", "text2")) {
					if (patch_text_document_value(layer, ctx->text2_value)) {
						ctx->has_text2 = true;
						blog(LOG_INFO, "[test_source] Patched SUBTITLE");
					}
				}

				patch_lottie_text_layers(layer, ctx);
			}
		}

		cJSON *child = NULL;
		cJSON_ArrayForEach(child, node)
		{
			if (child != layers)
				patch_lottie_text_layers(child, ctx);
		}

	} else if (cJSON_IsArray(node)) {
		cJSON *child = NULL;

		cJSON_ArrayForEach(child, node)
		{
			patch_lottie_text_layers(child, ctx);
		}
	}
}

static void unload_lottie(struct test_source *ctx)
{
	if (!ctx)
		return;

	ctx->lottie_loaded = false;

	/*
     * Confirm your ThorVG version's ownership rules.
     * If tvg_canvas_remove(canvas, NULL) is valid and removes all paints,
     * you can use that. Removing ctx->pic is usually clearer if supported.
     */
	if (ctx->canvas && ctx->pic)
		tvg_canvas_remove(ctx->canvas, ctx->pic);

	if (ctx->anim) {
		tvg_animation_del(ctx->anim);
		ctx->anim = NULL;
		ctx->pic = NULL;
	}

	ctx->anim = tvg_lottie_animation_new();
	if (!ctx->anim) {
		blog(LOG_ERROR, "[test_source] failed to create ThorVG animation");
		return;
	}

	ctx->pic = tvg_animation_get_picture(ctx->anim);
	if (!ctx->pic) {
		blog(LOG_ERROR, "[test_source] failed to get ThorVG animation picture");
		tvg_animation_del(ctx->anim);
		ctx->anim = NULL;
		return;
	}

	ctx->total_frames = 0.0f;
	ctx->current_lottie_frame = 0.0f;
	ctx->intro_start_frame = 0.0f;
	ctx->hold_start_frame_lottie = -1.0f;
	ctx->hold_end_frame_lottie = -1.0f;
	ctx->outro_start_frame = -1.0f;
	ctx->pvw_time_frame = -1.0f;
	ctx->has_hold_end_marker = false;
	ctx->last_rendered_lottie_frame = 0.0f;
	ctx->set_frame_failure_count = 0;
}

static bool load_lottie_with_current_text(struct test_source *ctx)
{
	if (!ctx || !ctx->lottie_path || !*ctx->lottie_path)
		return false;

	blog(LOG_INFO, "[test_source] Loading patched Lottie: %s", ctx->lottie_path);

	size_t json_size = 0;
	char *json = read_file_to_string(ctx->lottie_path, &json_size);

	if (!json) {
		blog(LOG_ERROR, "[test_source] Failed to read Lottie file: %s", ctx->lottie_path);
		return false;
	}

	cJSON *root = cJSON_ParseWithLength(json, json_size);
	bfree(json);

	if (!root) {
		blog(LOG_ERROR, "[test_source] Failed to parse Lottie JSON: %s", ctx->lottie_path);
		return false;
	}

	cJSON *fr = cJSON_GetObjectItemCaseSensitive(root, "fr");
	if (cJSON_IsNumber(fr) && fr->valuedouble > 0.0) {
		ctx->lottie_fps = (float)fr->valuedouble;
	} else {
		blog(LOG_WARNING, "[test_source] Lottie JSON has no valid 'fr'; using 30fps fallback");
	}

	ctx->has_text1 = false;
	ctx->has_text2 = false;

	/*
     * PATCHING HAPPENS HERE.
     * This modifies t.d.k[].s.t before ThorVG loads the animation.
     */
	patch_lottie_text_layers(root, ctx);

	if (!ctx->has_text1)
		blog(LOG_WARNING, "[test_source] NAME text layer not found");

	if (!ctx->has_text2)
		blog(LOG_WARNING, "[test_source] SUBTITLE text layer not found");

	warn_missing_text_glyphs(root, "NAME", ctx->text1_value);
	warn_missing_text_glyphs(root, "SUBTITLE", ctx->text2_value);

	char *patched_json = cJSON_PrintUnformatted(root);

	if (!patched_json) {
		cJSON_Delete(root);
		blog(LOG_ERROR, "[test_source] Failed to serialize patched Lottie JSON");
		return false;
	}

	/*
     * If this source had already loaded a Lottie, reset the ThorVG objects.
     * This avoids repeatedly adding old pictures to the same canvas.
	 */
	unload_lottie(ctx);

	bool parsed_json_markers = parse_lottie_markers_from_json(root, ctx);
	cJSON_Delete(root);

	if (!ctx->anim || !ctx->pic) {
		cJSON_free(patched_json);
		return false;
	}

	/*
     * Load patched JSON from memory instead of loading the original file.
     *
     * Depending on your ThorVG version, the size parameter may be uint32_t
     * or size_t. Cast as needed for your installed header.
     */
	Tvg_Result result = tvg_picture_load_data(ctx->pic, patched_json, (uint32_t)strlen(patched_json),
						  "application/json", NULL, true);

	cJSON_free(patched_json);

	if (result != TVG_RESULT_SUCCESS) {
		blog(LOG_ERROR, "[test_source] tvg_picture_load_data failed");
		return false;
	}

	tvg_picture_get_size(ctx->pic, &ctx->lottie_width, &ctx->lottie_height);

	if (ctx->lottie_width <= 0.0f || ctx->lottie_height <= 0.0f) {
		blog(LOG_WARNING, "[test_source] Lottie reported invalid size %.2fx%.2f; using buffer size",
		     ctx->lottie_width, ctx->lottie_height);

		ctx->lottie_width = (float)ctx->buffer_width;
		ctx->lottie_height = (float)ctx->buffer_height;
	}

	if (ctx->lottie_width <= 0 || ctx->lottie_height <= 0) {
		ctx->lottie_width = 1920.0f;
		ctx->lottie_height = 1080.0f;
	}

	tvg_animation_get_total_frame(ctx->anim, &ctx->total_frames);

	if (ctx->total_frames <= 1.0f) {
		blog(LOG_ERROR, "[test_source] Lottie has invalid total frame count: %.2f", ctx->total_frames);
		unload_lottie(ctx);
		return false;
	}

	ctx->exit_start_frame = (uint32_t)(ctx->total_frames * 0.75f);
	ctx->end_frame = (uint32_t)ctx->total_frames;

	blog(LOG_INFO, "[test_source] Lottie natural size: %.0fx%.0f", ctx->lottie_width, ctx->lottie_height);

	blog(LOG_INFO, "[test_source] Lottie total frames: %.1f", ctx->total_frames);

	tvg_picture_set_size(ctx->pic, ctx->lottie_width, ctx->lottie_height);
	tvg_canvas_add(ctx->canvas, ctx->pic);

	ctx->lottie_loaded = true;

	if (parsed_json_markers) {
		apply_lottie_marker_defaults(ctx);
	} else {
		parse_lottie_markers(ctx);
	}

	ctx->current_lottie_frame = ctx->intro_start_frame;

	blog(LOG_INFO, "[test_source] Patched Lottie loaded. text1=%d text2=%d", ctx->has_text1, ctx->has_text2);

	return true;
}
// ── Transition attachment ─────────────────────────────────

static uint32_t get_exit_duration_ms(struct test_source *ctx)
{
	// If we have Lottie loaded, calculate based on actual outro duration
	if (ctx->lottie_loaded && ctx->total_frames > 0) {
		// Calculate outro duration in frames
		float outro_frames = ctx->total_frames - ctx->hold_start_frame_lottie;
		if (outro_frames > 0) {
			// Assume 30fps for Lottie (common default)
			float lottie_fps = 30.0f;
			uint32_t duration_ms = (uint32_t)((outro_frames / lottie_fps) * 1000.0f);
			blog(LOG_INFO, "[test_source] Calculated outro duration: %u ms (%.1f frames)", duration_ms,
			     outro_frames);
			return duration_ms;
		}
	}
	// Fallback to fixed duration
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || ovi.fps_den == 0) {
		blog(LOG_WARNING, "[test_source] get_exit_duration_ms: using 1000ms fallback");
		return 1000;
	}
	float exit_frames = (float)ctx->end_frame - ctx->outro_start_frame;
	if (exit_frames <= 0.0f)
		return 0;

	uint32_t ms = (uint32_t)((double)exit_frames * ovi.fps_den / ovi.fps_num * 1000.0);
	return ms;
}

static void attach_hide_transition(struct test_source *ctx, obs_sceneitem_t *item)
{
	if (ctx->hide_transition) {
		detach_hide_transition_owner(ctx);
		obs_source_release(ctx->hide_transition);
		ctx->hide_transition = NULL;
	}

	ctx->hide_transition =
		obs_source_create_private("test_passthrough_transition", "lower_third_hide_transition", NULL);

	if (!ctx->hide_transition) {
		blog(LOG_ERROR, "[test_source] attach: create_private FAILED");
		return;
	}

	// FIX: use obs_obj_get_data to get the INSTANCE data returned by
	// passthrough_create, not obs_source_get_type_data which returns
	// the static type_data field from obs_source_info (always NULL here).
	struct passthrough_transition *td = (struct passthrough_transition *)obs_obj_get_data(ctx->hide_transition);
	if (td) {
		td->owner = ctx;
		blog(LOG_INFO, "[test_source] attach: owner back-pointer set");
	} else {
		blog(LOG_ERROR, "[test_source] attach: could not get transition instance data");
	}

	uint32_t duration_ms = get_exit_duration_ms(ctx);

	obs_sceneitem_set_transition(item, false, ctx->hide_transition);
	obs_sceneitem_set_transition_duration(item, false, duration_ms);

	obs_source_t *check = obs_sceneitem_get_transition(item, false);
	uint32_t check_dur = obs_sceneitem_get_transition_duration(item, false);
	blog(LOG_INFO, "[test_source] attach: set=%p dur=%ums | readback=%p dur=%ums", (void *)ctx->hide_transition,
	     duration_ms, (void *)check, check_dur);

	if (check != ctx->hide_transition)
		blog(LOG_ERROR, "[test_source] attach: READBACK MISMATCH");
}

static void try_attach_hide_transition(struct test_source *ctx)
{
	struct find_item_in_scenes_data fd = {
		.target = ctx->source,
		.result = NULL,
	};
	obs_enum_scenes(find_item_in_scenes_cb, &fd);

	if (fd.result) {
		blog(LOG_INFO, "[test_source] tick: found item %p, pre-attaching hide transition", (void *)fd.result);
		attach_hide_transition(ctx, fd.result);
	} else {
		blog(LOG_WARNING, "[test_source] tick: source not found in any scene");
	}
}

// ── Exit animation ────────────────────────────────────────

// Called by the transition's transition_start callback
static void test_source_begin_exit_old(struct test_source *ctx)
{
	if (ctx->state == STATE_HOLDING || ctx->state == STATE_ENTERING) {
		blog(LOG_INFO, "[test_source] begin_exit: entering EXITING state");
		ctx->state = STATE_EXITING;
		ctx->current_frame = ctx->exit_start_frame;
		// Start outro animation
		ctx->current_lottie_frame = ctx->outro_start_frame;
	}
}
static void test_source_begin_exit(struct test_source *ctx)
{
	if (ctx->state == STATE_HOLDING || ctx->state == STATE_ENTERING) {
		blog(LOG_INFO, "[test_source] begin_exit: EXITING from current Lottie frame %.2f",
		     ctx->current_lottie_frame);

		ctx->state = STATE_EXITING;
		ctx->suppress_hidden_preview = true;

		/*
         * Important:
         * Do NOT jump to ctx->outro_start_frame.
         *
         * We want to continue from the current hold-loop position and simply
         * stop looping.
         */
		ctx->is_looping_hold = false;

		/*
         * This is now a local counter for logging/debugging during exit.
         * It should not imply the Lottie frame should jump.
         */
		ctx->current_frame = 0;
	}
}

static double get_global_framerate(void)
{
	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.fps_num != 0 && ovi.fps_den != 0) {
		return (double)ovi.fps_num / (double)ovi.fps_den;
	}
	return 30.0f;
}

static float stable_marker_render_frame(struct test_source *ctx, float frame)
{
	if (ctx && ctx->total_frames > 1.0f && frame >= 0.0f && frame < ctx->total_frames - 1.0f) {
		return frame + 0.001f;
	}

	return frame;
}

static bool ensure_render_texture(struct test_source *ctx)
{
	if (!ctx || !ctx->buffer)
		return false;

	uint32_t w = (uint32_t)ctx->buffer_width;
	uint32_t h = (uint32_t)ctx->buffer_height;

	if (w == 0 || h == 0)
		return false;

	if (ctx->texture && ctx->texture_width == w && ctx->texture_height == h) {
		return true;
	}

	if (ctx->texture) {
		gs_texture_destroy(ctx->texture);
		ctx->texture = NULL;
	}

	const uint8_t *planes[1] = {
		(const uint8_t *)ctx->buffer,
	};

	ctx->texture = gs_texture_create(w, h, GS_BGRA, 1, planes, GS_DYNAMIC);

	if (!ctx->texture) {
		blog(LOG_ERROR, "[test_source] failed to create render texture %ux%u", w, h);
		return false;
	}

	ctx->texture_width = w;
	ctx->texture_height = h;

	return true;
}

static bool render_buffer_to_obs(struct test_source *ctx, uint32_t draw_width, uint32_t draw_height)
{
	if (!ctx || !ctx->buffer)
		return false;

	if (!ensure_render_texture(ctx))
		return false;

	/*
     * Confirm gs_texture_set_image availability in your OBS headers.
     * This is preferable to recreating the texture every frame.
     */
	gs_texture_set_image(ctx->texture, (const uint8_t *)ctx->buffer, (uint32_t)ctx->buffer_width * 4, false);

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!default_effect)
		return false;

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");

	if (!image)
		return false;

	gs_effect_set_texture(image, ctx->texture);

	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(ctx->texture, 0, draw_width, draw_height);

	return true;
}

static bool render_lottie_frame_to_buffer(struct test_source *ctx, float frame)
{
	if (!ctx || !ctx->lottie_loaded || !ctx->anim || !ctx->canvas || !ctx->buffer)
		return false;

	if (ctx->total_frames > 1.0f) {
		if (frame < 0.0f)
			frame = 0.0f;

		if (frame > ctx->total_frames - 1.0f)
			frame = ctx->total_frames - 1.0f;
	}

	Tvg_Result result;

	result = tvg_animation_set_frame(ctx->anim, frame);
	if (result == TVG_RESULT_INSUFFICIENT_CONDITION) {
		ctx->set_frame_failure_count = 0;
	} else if (result != TVG_RESULT_SUCCESS) {
		ctx->set_frame_failure_count++;
		blog(LOG_WARNING,
		     "[test_source] tvg_animation_set_frame failed at frame %.3f; preserving previous rendered frame %.3f",
		     frame, ctx->last_rendered_lottie_frame);
		return true;
	} else {
		ctx->set_frame_failure_count = 0;
	}

	size_t buffer_bytes = (size_t)ctx->buffer_width * (size_t)ctx->buffer_height * sizeof(uint32_t);

	memset(ctx->buffer, 0, buffer_bytes);

	result = tvg_canvas_update(ctx->canvas);
	if (result != TVG_RESULT_SUCCESS) {
		blog(LOG_WARNING, "[test_source] tvg_canvas_update failed");
		return false;
	}

	result = tvg_canvas_draw(ctx->canvas, true);
	if (result != TVG_RESULT_SUCCESS) {
		blog(LOG_WARNING, "[test_source] tvg_canvas_draw failed");
		return false;
	}

	result = tvg_canvas_sync(ctx->canvas);
	if (result != TVG_RESULT_SUCCESS) {
		blog(LOG_WARNING, "[test_source] tvg_canvas_sync failed");
		return false;
	}

	ctx->last_rendered_lottie_frame = frame;
	return true;
}

static bool render_lottie_to_buffer(struct test_source *ctx)
{
	if (!ctx)
		return false;

	return render_lottie_frame_to_buffer(ctx, ctx->current_lottie_frame);
}

static float get_lottie_preview_frame(struct test_source *ctx)
{
	if (!ctx || ctx->total_frames <= 1.0f)
		return 0.0f;

	/*
     * Preferred: explicit preview marker from the Lottie.
     */
	if (ctx->pvw_time_frame >= 0.0f && ctx->pvw_time_frame < ctx->total_frames) {
		return ctx->pvw_time_frame;
	}

	/*
     * Fallback: show the beginning of the hold loop.
     */
	if (ctx->hold_start_frame_lottie >= 0.0f && ctx->hold_start_frame_lottie < ctx->total_frames) {
		return ctx->hold_start_frame_lottie;
	}

	/*
     * Fallback: midpoint of hold loop if both markers exist.
     */
	if (ctx->hold_start_frame_lottie >= 0.0f && ctx->hold_end_frame_lottie > ctx->hold_start_frame_lottie &&
	    ctx->hold_end_frame_lottie < ctx->total_frames) {
		return (ctx->hold_start_frame_lottie + ctx->hold_end_frame_lottie) * 0.5f;
	}

	/*
     * Last fallback: 30% into the animation.
     */
	return ctx->total_frames * 0.3f;
}

static obs_properties_t *test_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_path(props, "lottie_path", "Lottie File", OBS_PATH_FILE, "Lottie files (*.json)", NULL);

	obs_properties_add_text(props, "text1", "Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "text2", "Title", OBS_TEXT_DEFAULT);

	return props;
}

// ── Scene signal handling ─────────────────────────────────

static void connect_scene_signals(struct test_source *ctx, obs_source_t *scene_source)
{
	if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source))
		return;
	signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
	signal_handler_connect(handler, "item_visible", scene_item_visible, ctx);
}

static void disconnect_scene_signals(struct test_source *ctx, obs_source_t *scene_source)
{
	if (!obs_scene_from_source(scene_source) && !obs_group_from_source(scene_source))
		return;
	signal_handler_t *handler = obs_source_get_signal_handler(scene_source);
	signal_handler_disconnect(handler, "item_visible", scene_item_visible, ctx);
}

static bool connect_scene_enum(void *data, obs_source_t *scene_source)
{
	connect_scene_signals((struct test_source *)data, scene_source);
	return true;
}

static bool disconnect_scene_enum(void *data, obs_source_t *scene_source)
{
	disconnect_scene_signals((struct test_source *)data, scene_source);
	return true;
}

static const char *test_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Test Lower Third";
}

static void global_source_create(void *data, calldata_t *params)
{
	struct test_source *ctx = (struct test_source *)data;
	obs_source_t *source = calldata_ptr(params, "source");
	if (source)
		connect_scene_signals(ctx, source);
}
static void test_show(void *data)
{
	struct test_source *ctx = (struct test_source *)data;

	blog(LOG_INFO, "[test_source] show");

	ctx->state = STATE_ENTERING;
	ctx->suppress_hidden_preview = false;
	ctx->current_frame = 0;
	ctx->is_looping_hold = false;

	if (!ctx->lottie_path) {
		blog(LOG_WARNING, "[test_source] No Lottie path configured");
		return;
	}

	if (!ctx->lottie_loaded) {
		if (!load_lottie_with_current_text(ctx)) {
			blog(LOG_ERROR, "[test_source] Failed to load patched Lottie");
			return;
		}
	}

	ctx->current_lottie_frame = ctx->intro_start_frame;
	ctx->state = STATE_ENTERING;

	blog(LOG_INFO, "[test_source] Starting at intro frame: %.1f", ctx->intro_start_frame);
}
static void test_show_old(void *data)
{
	struct test_source *ctx = (struct test_source *)data;
	blog(LOG_INFO, "[test_source] show");
	ctx->state = STATE_ENTERING;
	ctx->current_frame = 0;
	ctx->is_looping_hold = false;

	// Load Lottie if not already loaded
	if (!ctx->lottie_path) {
		blog(LOG_WARNING, "[test_source] No Lottie path configured");
		return;
	}

	if (!ctx->lottie_loaded && ctx->lottie_path) {
		blog(LOG_INFO, "[test_source] Loading Lottie: %s", ctx->lottie_path);
		if (tvg_picture_load(ctx->pic, ctx->lottie_path) == TVG_RESULT_SUCCESS) {
			// Get natural Lottie dimensions before setting size
			tvg_picture_get_size(ctx->pic, &ctx->lottie_width, &ctx->lottie_height);

			// Get total frames for proper timing
			tvg_animation_get_total_frame(ctx->anim, &ctx->total_frames);
			blog(LOG_INFO, "[test_source] Lottie total frames: %.1f", ctx->total_frames);

			// Set exit frames based on actual animation
			ctx->exit_start_frame = (uint32_t)(ctx->total_frames * 0.75f); // 75% as exit start
			ctx->end_frame = (uint32_t)ctx->total_frames;                  // End at last frame

			blog(LOG_INFO, "[test_source] Lottie natural size: %.0fx%.0f", ctx->lottie_width,
			     ctx->lottie_height);
			// Set picture size to natural dimensions
			tvg_picture_set_size(ctx->pic, ctx->lottie_width, ctx->lottie_height);
			tvg_canvas_add(ctx->canvas, ctx->pic);
			ctx->lottie_loaded = true;

			// Get total frames and parse markers
			tvg_animation_get_total_frame(ctx->anim, &ctx->total_frames);
			blog(LOG_INFO, "[test_source] Lottie loaded, total frames: %.1f", ctx->total_frames);
			parse_lottie_markers(ctx);

			// Start at intro marker
			ctx->current_lottie_frame = ctx->intro_start_frame;
		} else {
			blog(LOG_ERROR, "[test_source] Failed to load Lottie file: %s", ctx->lottie_path);
		}
	} else if (ctx->lottie_loaded) {
		// Reset to intro start
		ctx->current_lottie_frame = ctx->intro_start_frame;
		blog(LOG_INFO, "[test_source] Resetting to intro frame: %.1f", ctx->intro_start_frame);
		ctx->state = STATE_ENTERING;
	}
}

static void test_hide(void *data)
{
	struct test_source *ctx = (struct test_source *)data;
	blog(LOG_INFO, "[test_source] hide: state=%d", ctx->state);
	ctx->suppress_hidden_preview = true;
	// The exit animation is handled by the transition callback
}

static void scene_item_visible(void *data, calldata_t *params)
{
	struct test_source *ctx = (struct test_source *)data;
	obs_sceneitem_t *item = calldata_ptr(params, "item");
	bool visible = calldata_bool(params, "visible");

	if (!item || obs_sceneitem_get_source(item) != ctx->source)
		return;

	// Update transition duration when visibility changes
	if (ctx->hide_transition) {
		obs_sceneitem_set_transition_duration(item, false, get_exit_duration_ms(ctx));
	}

	blog(LOG_INFO, "[test_source] scene_item_visible: visible=%d", visible);

	if (visible) {
		test_show(ctx);
	} else {
		test_hide(ctx);
	}
}

// ── Source lifecycle ──────────────────────────────────────

static void *test_create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_INFO, "[test_source] create: source=%p", (void *)source);

	struct test_source *ctx = bzalloc(sizeof(struct test_source));
	ctx->source = source;
	ctx->state = STATE_HIDDEN;
	ctx->suppress_hidden_preview = false;
	ctx->hold_start_frame = 30;

	ctx->buffer_width = 1920;
	ctx->buffer_height = 1080;
	ctx->buffer = bzalloc(ctx->buffer_width * ctx->buffer_height * sizeof(uint32_t));
	ctx->canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
	tvg_swcanvas_set_target(ctx->canvas, ctx->buffer, ctx->buffer_width, ctx->buffer_width, ctx->buffer_height,
				TVG_COLORSPACE_ARGB8888);
	if (!ctx->canvas) {
		blog(LOG_ERROR, "[test_source] failed to create ThorVG canvas");
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
	ctx->anim = tvg_lottie_animation_new();
	if (!ctx->anim) {
		blog(LOG_ERROR, "[test_source] failed to create ThorVG animation");
		tvg_canvas_destroy(ctx->canvas);
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
	ctx->pic = tvg_animation_get_picture(ctx->anim);
	if (!ctx->pic) {
		blog(LOG_ERROR, "[test_source] failed to get ThorVG animation picture");
		tvg_animation_del(ctx->anim);
		tvg_canvas_destroy(ctx->canvas);
		bfree(ctx->buffer);
		bfree(ctx);
		return NULL;
	}
	ctx->lottie_loaded = false;
	ctx->lottie_fps = 30.0f;
	ctx->lottie_width = 1920.0f;
	ctx->lottie_height = 1080.0f;
	ctx->is_looping_hold = false;

	ctx->texture = NULL;
	ctx->texture_width = 0;
	ctx->texture_height = 0;
	ctx->lottie_fps = 30.0f;

	// Get Lottie file path from settings - add proper validation
	const char *lottie_path = obs_data_get_string(settings, "lottie_path");
	if (lottie_path && strlen(lottie_path) > 0) {
		ctx->lottie_path = bstrdup(lottie_path);
		blog(LOG_INFO, "[test_source] Lottie path set to: %s", ctx->lottie_path);
	} else {
		ctx->lottie_path = NULL; // Explicitly set to NULL
		blog(LOG_INFO, "[test_source] No Lottie path configured in settings");
	}

	const char *text1 = obs_data_get_string(settings, "text1");
	const char *text2 = obs_data_get_string(settings, "text2");
	ctx->text1_value = bstrdup(text1 && *text1 ? text1 : "NAME");
	ctx->text2_value = bstrdup(text2 && *text2 ? text2 : "Title");

	// Load immediately if path is provided
	//test_show(ctx);
	ctx->state = STATE_HIDDEN;

	obs_enum_scenes(connect_scene_enum, ctx);
	signal_handler_connect(obs_get_signal_handler(), "source_create", global_source_create, ctx);
	if (ctx->lottie_path && *ctx->lottie_path) {
		if (!load_lottie_with_current_text(ctx)) {
			blog(LOG_WARNING,
			     "[test_source] create: Lottie was configured but could not be loaded for preview");
		}
	}
	return ctx;
}
static void test_update(void *data, obs_data_t *settings)
{
	struct test_source *ctx = (struct test_source *)data;

	bool changed = false;

	const char *lottie_path = obs_data_get_string(settings, "lottie_path");
	const char *text1 = obs_data_get_string(settings, "text1");
	const char *text2 = obs_data_get_string(settings, "text2");

	changed |= set_bstr(&ctx->text1_value, text1);
	changed |= set_bstr(&ctx->text2_value, text2);

	if (lottie_path && *lottie_path) {
		if (!ctx->lottie_path || strcmp(ctx->lottie_path, lottie_path) != 0) {
			bfree(ctx->lottie_path);
			ctx->lottie_path = bstrdup(lottie_path);
			changed = true;
		}
	} else if (ctx->lottie_path) {
		bfree(ctx->lottie_path);
		ctx->lottie_path = NULL;
		unload_lottie(ctx);
		changed = true;
	}

	if ((changed || !ctx->lottie_loaded) && ctx->lottie_path) {
		float old_frame = ctx->current_lottie_frame;
		enum test_state old_state = ctx->state;

		if (load_lottie_with_current_text(ctx)) {
			/*
             * Preserve playback state if the source is already visible.
             * If hidden, use the preview frame naturally during render.
             */
			ctx->state = old_state;

			if (old_frame >= 0.0f && old_frame < ctx->total_frames)
				ctx->current_lottie_frame = old_frame;
			else if (ctx->state == STATE_HIDDEN)
				ctx->current_lottie_frame = get_lottie_preview_frame(ctx);
		}
	}
}
static void test_update_old(void *data, obs_data_t *settings)
{
	struct test_source *ctx = (struct test_source *)data;

	// Free existing path if allocated
	if (ctx->lottie_path) {
		bfree(ctx->lottie_path);
		ctx->lottie_path = NULL;
	}

	// Get new path
	const char *lottie_path = obs_data_get_string(settings, "lottie_path");
	if (lottie_path && strlen(lottie_path) > 0) {
		ctx->lottie_path = bstrdup(lottie_path);
		blog(LOG_INFO, "[test_source] Lottie path updated to: %s", ctx->lottie_path);

		// Try to load the Lottie file
		if (!ctx->lottie_loaded && ctx->lottie_path) {
			blog(LOG_INFO, "[test_source] Loading Lottie: %s", ctx->lottie_path);
			if (tvg_picture_load(ctx->pic, ctx->lottie_path) == TVG_RESULT_SUCCESS) {
				// Get natural Lottie dimensions
				tvg_picture_get_size(ctx->pic, &ctx->lottie_width, &ctx->lottie_height);

				// Get total frames for proper timing
				tvg_animation_get_total_frame(ctx->anim, &ctx->total_frames);
				blog(LOG_INFO, "[test_source] Lottie loaded, total frames: %.1f", ctx->total_frames);

				tvg_canvas_add(ctx->canvas, ctx->pic);
				ctx->lottie_loaded = true;
				parse_lottie_markers(ctx);

				// Start at intro marker
				ctx->current_lottie_frame = ctx->intro_start_frame;
			} else {
				blog(LOG_ERROR, "[test_source] Failed to load Lottie file: %s", ctx->lottie_path);
			}
		}
	}
}

static void destroy_render_texture(struct test_source *ctx)
{
	if (!ctx || !ctx->texture)
		return;

	/*
     * Confirm with your OBS version whether destroy callbacks are already
     * inside the graphics context. obs_enter_graphics/obs_leave_graphics is
     * the safer pattern when destroying gs_* resources outside render.
     */
	obs_enter_graphics();
	gs_texture_destroy(ctx->texture);
	obs_leave_graphics();

	ctx->texture = NULL;
	ctx->texture_width = 0;
	ctx->texture_height = 0;
}

static void test_destroy(void *data)
{
	struct test_source *ctx = (struct test_source *)data;
	blog(LOG_INFO, "[test_source] destroy");

	signal_handler_disconnect(obs_get_signal_handler(), "source_create", global_source_create, ctx);
	obs_enum_scenes(disconnect_scene_enum, ctx);

	detach_hide_transition_owner(ctx);
	if (ctx->hide_transition)
		obs_source_release(ctx->hide_transition);

	if (ctx->lottie_path)
		bfree(ctx->lottie_path);
	if (ctx->canvas)
		tvg_canvas_destroy(ctx->canvas);
	if (ctx->anim)
		tvg_animation_del(ctx->anim);
	destroy_render_texture(ctx);
	if (ctx->buffer)
		bfree(ctx->buffer);
	if (ctx->text1_value)
		bfree(ctx->text1_value);
	if (ctx->text2_value)
		bfree(ctx->text2_value);

	bfree(data);
}

static void test_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct test_source *ctx = (struct test_source *)data;

	if (!ctx->lottie_loaded)
		return;

	// Calculate frame advancement (assuming 60fps OBS tick)
	float frame_advance = ctx->lottie_fps * seconds;

	switch (ctx->state) {
	case STATE_ENTERING:
		ctx->current_lottie_frame += frame_advance;
		ctx->current_frame++;

		// Check if we've reached the hold start
		if (ctx->current_lottie_frame >= ctx->hold_start_frame_lottie) {
			blog(LOG_INFO, "[test_source] tick: ENTERING -> HOLDING (frame %u)", ctx->current_frame);
			ctx->state = STATE_HOLDING;
			ctx->is_looping_hold = ctx->has_hold_end_marker &&
					       ctx->hold_end_frame_lottie > ctx->hold_start_frame_lottie;
			ctx->current_lottie_frame = stable_marker_render_frame(ctx, ctx->hold_start_frame_lottie);
			try_attach_hide_transition(ctx);
		}
		break;

	case STATE_HOLDING:
		if (ctx->is_looping_hold) {
			// Loop between hold_start and hold_end
			ctx->current_lottie_frame += frame_advance;
			if (ctx->current_lottie_frame > ctx->hold_end_frame_lottie) {
				ctx->current_lottie_frame =
					stable_marker_render_frame(ctx, ctx->hold_start_frame_lottie);
			}
		} else {
			ctx->current_lottie_frame = stable_marker_render_frame(ctx, ctx->hold_start_frame_lottie);
		}
		break;

	case STATE_EXITING:
		// This is now handled by the transition callback
		break;

	case STATE_HIDDEN:
		break;
	}

	if (ctx->current_frame % 30 == 0) {
		blog(LOG_INFO, "[test_source] State: %d, Lottie frame: %.2f", ctx->state, ctx->current_lottie_frame);
	}
}

static void test_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct test_source *ctx = (struct test_source *)data;
	if (!ctx)
		return;

	/*
     * Do not try to load from inside render. Loading/parsing Lottie JSON is too
     * expensive and can block the render path. test_create/test_update should
     * handle loading.
     */
	if (!ctx->lottie_loaded)
		return;

	static uint32_t render_log_counter = 0;
	render_log_counter++;

	if (render_log_counter % 60 == 0) {
		blog(LOG_INFO, "[test_source] render: state=%d loaded=%d current=%.2f preview=%.2f total=%.2f path=%s",
		     ctx->state, ctx->lottie_loaded, ctx->current_lottie_frame, get_lottie_preview_frame(ctx),
		     ctx->total_frames, ctx->lottie_path ? ctx->lottie_path : "(null)");
	}

	if (!ctx->lottie_loaded)
		return;

	if (ctx->state == STATE_HIDDEN && ctx->suppress_hidden_preview)
		return;

	float frame = ctx->current_lottie_frame;

	/*
     * Properties preview/edit window previously showed nothing while hidden.
     * Render a stable preview frame when the source state is hidden.
     *
     * Normal OBS scene output should not call video_render for a hidden
     * scene item, so this mainly helps direct source previews/properties.
     */
	if (ctx->state == STATE_HIDDEN)
		frame = get_lottie_preview_frame(ctx);

	if (!render_lottie_frame_to_buffer(ctx, frame))
		return;

	render_buffer_to_obs(ctx, (uint32_t)ctx->buffer_width, (uint32_t)ctx->buffer_height);
}

// Called from passthrough_callback each frame during the hide transition
static void test_source_tick_exit(struct test_source *ctx, uint32_t cx, uint32_t cy, gs_texture_t *a)
{
	if (ctx->state == STATE_EXITING) {
		if (ctx->lottie_loaded) {
			float frame_advance = ctx->lottie_fps / (float)get_global_framerate();
			float next_frame = ctx->current_lottie_frame + frame_advance;

			/*
             * If the hold loop has a defined end and the outro starts elsewhere,
             * jump only after naturally reaching the hold end.
             */
			if (ctx->hold_end_frame_lottie >= 0.0f && ctx->outro_start_frame >= 0.0f &&
			    ctx->outro_start_frame > ctx->hold_end_frame_lottie &&
			    ctx->current_lottie_frame < ctx->hold_end_frame_lottie &&
			    next_frame >= ctx->hold_end_frame_lottie) {
				next_frame = ctx->outro_start_frame;
			}

			if (next_frame >= ctx->total_frames - 1.0f) {
				ctx->current_lottie_frame = ctx->total_frames - 1.0f;
				ctx->state = STATE_HIDDEN;

				blog(LOG_INFO, "[test_source] tick_exit: animation complete at frame %.2f",
				     ctx->current_lottie_frame);
			} else {
				ctx->current_lottie_frame = next_frame;
			}
		}

		ctx->current_frame++;

		if (ctx->current_frame % 10 == 0) {
			blog(LOG_INFO, "[test_source] tick_exit: transition_frame=%u lottie=%.2f/%.1f",
			     ctx->current_frame, ctx->current_lottie_frame, ctx->total_frames);
		}
	}

	/*
	 * OBS has already rendered this source into texture A using test_render().
	 * Draw that captured source texture instead of rendering a second copy here;
	 * double-rendering during the item transition can brighten translucent pixels.
	 */
	if (!a)
		return;

	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!default_effect)
		return;

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
	if (!image)
		return;

	gs_effect_set_texture(image, a);

	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(a, 0, cx, cy);
}

static uint32_t test_get_width(void *data)
{
	struct test_source *ctx = (struct test_source *)data;
	return (uint32_t)ctx->lottie_width;
}

static uint32_t test_get_height(void *data)
{
	struct test_source *ctx = (struct test_source *)data;
	return (uint32_t)ctx->lottie_height;
}

static struct obs_source_info test_source_info = {
	.id = "test_lower_third",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = test_get_name,
	.create = test_create,
	.destroy = test_destroy,
	.get_width = test_get_width,
	.get_height = test_get_height,
	.show = test_show,
	.hide = test_hide,
	.video_tick = test_video_tick,
	.video_render = test_render,
	.get_properties = test_get_properties,
	.update = test_update,
	.get_defaults = test_get_defaults,
};

// ── Registration ─────────────────────────────────────────

void register_test_source(void)
{
	obs_register_source(&test_source_info);
	blog(LOG_INFO, "[test_source] after registered");
	obs_register_source(&passthrough_transition_info);
}

void unregister_test_source(void) {}
