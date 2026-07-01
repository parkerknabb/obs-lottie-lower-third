#include "lottie-lower-third-lottie.h"

#include "lottie-lower-third-markers.h"

#include <cJSON.h>
#include <plugin-support.h>

#include <stdio.h>
#include <string.h>

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

static bool patch_text_document_value(cJSON *layer, const char *new_text)
{
	cJSON *t = cJSON_GetObjectItemCaseSensitive(layer, "t");
	cJSON *d = t ? cJSON_GetObjectItemCaseSensitive(t, "d") : NULL;
	cJSON *k = d ? cJSON_GetObjectItemCaseSensitive(d, "k") : NULL;

	if (!k)
		return false;

	bool patched = false;

	if (cJSON_IsObject(k)) {
		cJSON *s = cJSON_GetObjectItemCaseSensitive(k, "s");
		cJSON *text = s ? cJSON_GetObjectItemCaseSensitive(s, "t") : NULL;

		if (cJSON_IsString(text)) {
			cJSON_SetValuestring(text, new_text ? new_text : "");
			patched = true;
		}

		return patched;
	}

	if (!cJSON_IsArray(k))
		return false;

	cJSON *keyframe = NULL;
	cJSON_ArrayForEach(keyframe, k) {
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
		blog(LOG_WARNING,
		     "[lottie_lower_third] %s patched text contains glyphs not embedded in Lottie chars: \"%s\"",
		     layer_name, missing);
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

static void patch_lottie_text_layers(cJSON *node, struct lottie_lower_third *ctx)
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
					}
				}

				if (is_text_layer(layer, "SUBTITLE", "SUBTITLE", "#text2", "text2")) {
					if (patch_text_document_value(layer, ctx->text2_value)) {
						ctx->has_text2 = true;
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

void lottie_lower_third_unload_lottie(struct lottie_lower_third *ctx)
{
	if (!ctx)
		return;

	ctx->lottie_loaded = false;

	if (ctx->canvas && ctx->pic)
		tvg_canvas_remove(ctx->canvas, ctx->pic);

	if (ctx->anim) {
		tvg_animation_del(ctx->anim);
		ctx->anim = NULL;
		ctx->pic = NULL;
	}

	ctx->anim = tvg_lottie_animation_new();
	if (!ctx->anim) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to create ThorVG animation");
		return;
	}

	ctx->pic = tvg_animation_get_picture(ctx->anim);
	if (!ctx->pic) {
		blog(LOG_ERROR, "[lottie_lower_third] failed to get ThorVG animation picture");
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
	ctx->warned_canvas_update_failure = false;
	ctx->warned_canvas_draw_failure = false;
	ctx->warned_canvas_sync_failure = false;
}

bool lottie_lower_third_load_with_current_text(struct lottie_lower_third *ctx)
{
	if (!ctx || !ctx->lottie_path || !*ctx->lottie_path)
		return false;

	size_t json_size = 0;
	char *json = read_file_to_string(ctx->lottie_path, &json_size);

	if (!json) {
		blog(LOG_ERROR, "[lottie_lower_third] Failed to read Lottie file: %s", ctx->lottie_path);
		return false;
	}

	cJSON *root = cJSON_ParseWithLength(json, json_size);
	bfree(json);

	if (!root) {
		blog(LOG_ERROR, "[lottie_lower_third] Failed to parse Lottie JSON: %s", ctx->lottie_path);
		return false;
	}

	cJSON *fr = cJSON_GetObjectItemCaseSensitive(root, "fr");
	if (cJSON_IsNumber(fr) && fr->valuedouble > 0.0) {
		ctx->lottie_fps = (float)fr->valuedouble;
	} else {
		blog(LOG_WARNING, "[lottie_lower_third] Lottie JSON has no valid 'fr'; using 30fps fallback");
	}

	ctx->has_text1 = false;
	ctx->has_text2 = false;

	patch_lottie_text_layers(root, ctx);

	if (!ctx->has_text1)
		blog(LOG_WARNING, "[lottie_lower_third] NAME text layer not found");

	if (!ctx->has_text2)
		blog(LOG_WARNING, "[lottie_lower_third] SUBTITLE text layer not found");

	warn_missing_text_glyphs(root, "NAME", ctx->text1_value);
	warn_missing_text_glyphs(root, "SUBTITLE", ctx->text2_value);

	char *patched_json = cJSON_PrintUnformatted(root);

	if (!patched_json) {
		cJSON_Delete(root);
		blog(LOG_ERROR, "[lottie_lower_third] Failed to serialize patched Lottie JSON");
		return false;
	}

	lottie_lower_third_unload_lottie(ctx);

	bool parsed_json_markers = lottie_lower_third_parse_markers_from_json(root, ctx);
	cJSON_Delete(root);

	if (!ctx->anim || !ctx->pic) {
		cJSON_free(patched_json);
		return false;
	}

	Tvg_Result result = tvg_picture_load_data(ctx->pic, patched_json, (uint32_t)strlen(patched_json),
						  "application/json", NULL, true);

	cJSON_free(patched_json);

	if (result != TVG_RESULT_SUCCESS) {
		blog(LOG_ERROR, "[lottie_lower_third] tvg_picture_load_data failed");
		return false;
	}

	tvg_picture_get_size(ctx->pic, &ctx->lottie_width, &ctx->lottie_height);

	if (ctx->lottie_width <= 0.0f || ctx->lottie_height <= 0.0f) {
		blog(LOG_WARNING, "[lottie_lower_third] Lottie reported invalid size %.2fx%.2f; using buffer size",
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
		blog(LOG_ERROR, "[lottie_lower_third] Lottie has invalid total frame count: %.2f", ctx->total_frames);
		lottie_lower_third_unload_lottie(ctx);
		return false;
	}

	ctx->exit_start_frame = (uint32_t)(ctx->total_frames * 0.75f);
	ctx->end_frame = (uint32_t)ctx->total_frames;

	tvg_picture_set_size(ctx->pic, ctx->lottie_width, ctx->lottie_height);
	tvg_canvas_add(ctx->canvas, ctx->pic);

	ctx->lottie_loaded = true;

	if (parsed_json_markers) {
		lottie_lower_third_apply_marker_defaults(ctx);
	} else {
		lottie_lower_third_parse_markers_from_animation(ctx);
	}

	ctx->current_lottie_frame = ctx->intro_start_frame;

	return true;
}
