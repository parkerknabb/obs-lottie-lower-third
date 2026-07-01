#include "lottie-style-library.h"

#include <cJSON.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <util/config-file.h>
#include <stdio.h>
#include <string.h>

static const char *STYLE_LIBRARY_SECTION = "LottieLowerThird";
static const char *STYLE_LIBRARY_FILES = "StyleFiles";
static const char *STYLE_LIBRARY_DIR = "StyleDirectory";

static config_t *style_library_config(void)
{
	return obs_frontend_get_user_config();
}

const char *lottie_style_library_get_files(void)
{
	config_t *config = style_library_config();
	if (!config)
		return "";

	const char *files = config_get_string(config, STYLE_LIBRARY_SECTION, STYLE_LIBRARY_FILES);
	return files ? files : "";
}

const char *lottie_style_library_get_dir(void)
{
	config_t *config = style_library_config();
	if (!config)
		return "";

	const char *dir = config_get_string(config, STYLE_LIBRARY_SECTION, STYLE_LIBRARY_DIR);
	return dir ? dir : "";
}

void lottie_style_library_set(const char *files, const char *dir)
{
	config_t *config = style_library_config();
	if (!config)
		return;

	config_set_string(config, STYLE_LIBRARY_SECTION, STYLE_LIBRARY_FILES, files ? files : "");
	config_set_string(config, STYLE_LIBRARY_SECTION, STYLE_LIBRARY_DIR, dir ? dir : "");
	config_save(config);
}

static bool has_json_extension(const char *path)
{
	if (!path)
		return false;

	const char *dot = strrchr(path, '.');
	if (!dot)
		return false;

	return (dot[0] == '.') && (dot[1] == 'j' || dot[1] == 'J') && (dot[2] == 's' || dot[2] == 'S') &&
	       (dot[3] == 'o' || dot[3] == 'O') && (dot[4] == 'n' || dot[4] == 'N') && dot[5] == '\0';
}

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

bool lottie_style_library_is_valid_lottie_file(const char *path)
{
	if (!path || !*path || !has_json_extension(path))
		return false;

	size_t json_size = 0;
	char *json = read_file_to_string(path, &json_size);
	if (!json)
		return false;

	cJSON *root = cJSON_ParseWithLength(json, json_size);
	bfree(json);

	if (!cJSON_IsObject(root)) {
		cJSON_Delete(root);
		return false;
	}

	cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
	cJSON *framerate = cJSON_GetObjectItemCaseSensitive(root, "fr");
	cJSON *in_point = cJSON_GetObjectItemCaseSensitive(root, "ip");
	cJSON *out_point = cJSON_GetObjectItemCaseSensitive(root, "op");
	cJSON *width = cJSON_GetObjectItemCaseSensitive(root, "w");
	cJSON *height = cJSON_GetObjectItemCaseSensitive(root, "h");
	cJSON *layers = cJSON_GetObjectItemCaseSensitive(root, "layers");

	bool valid = cJSON_IsString(version) && cJSON_IsNumber(framerate) && framerate->valuedouble > 0.0 &&
		     cJSON_IsNumber(in_point) && cJSON_IsNumber(out_point) && out_point->valuedouble > in_point->valuedouble &&
		     cJSON_IsNumber(width) && width->valueint > 0 && cJSON_IsNumber(height) && height->valueint > 0 &&
		     cJSON_IsArray(layers);

	cJSON_Delete(root);
	return valid;
}
