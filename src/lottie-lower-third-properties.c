#include "lottie-lower-third-properties.h"

#include "lottie-style-library.h"

#include <plugin-support.h>
#include <util/platform.h>

#include <stdio.h>
#include <string.h>

bool lottie_lower_third_setting_has_user_value(obs_data_t *settings, const char *name)
{
	return settings && obs_data_has_user_value(settings, name);
}

const char *lottie_lower_third_get_effective_lottie_path_from_settings(obs_data_t *settings)
{
	const char *style_path = obs_data_get_string(settings, SETTING_STYLE_PATH);
	const char *custom_path = obs_data_get_string(settings, SETTING_LOTTIE_PATH);
	bool custom_file = obs_data_get_bool(settings, SETTING_CUSTOM_FILE);

	return custom_file ? custom_path : style_path;
}

void lottie_lower_third_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "text1", "Main Text");
	obs_data_set_default_string(settings, "text2", "Subtext");
	obs_data_set_default_bool(settings, SETTING_CUSTOM_FILE, false);
	obs_data_set_default_string(settings, SETTING_STYLE_PATH, "");
	obs_data_set_default_bool(settings, SETTING_PLACEMENT_INITIALIZED, false);
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

static const char *path_basename(const char *path)
{
	if (!path)
		return "";

	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	const char *base = slash > backslash ? slash : backslash;

	return base ? base + 1 : path;
}

static char *style_display_name(const char *path)
{
	const char *base = path_basename(path);
	char *name = bstrdup(base);
	char *dot = strrchr(name, '.');

	if (dot && has_json_extension(dot))
		*dot = '\0';

	return name;
}

static void add_style_path(obs_property_t *style_list, const char *path)
{
	if (!style_list || !path || !*path || !lottie_style_library_is_valid_lottie_file(path))
		return;

	char *display_name = style_display_name(path);
	obs_property_list_add_string(style_list, display_name, path);
	bfree(display_name);
}

static void add_style_files_from_text(obs_property_t *style_list, const char *paths)
{
	if (!paths || !*paths)
		return;

	char *copy = bstrdup(paths);
	char *next = copy;

	while (next && *next) {
		char *line = next;
		next = strpbrk(line, "\r\n;");

		if (next) {
			*next = '\0';
			next++;
		}

		while (*line == ' ' || *line == '\t')
			line++;

		char *end = line + strlen(line);
		while (end > line && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';

		add_style_path(style_list, line);
	}

	bfree(copy);
}

static void add_style_files_from_dir(obs_property_t *style_list, const char *dir_path)
{
	if (!dir_path || !*dir_path)
		return;

	os_dir_t *dir = os_opendir(dir_path);
	if (!dir)
		return;

	struct os_dirent *entry = NULL;
	while ((entry = os_readdir(dir)) != NULL) {
		if (entry->directory || !has_json_extension(entry->d_name))
			continue;

		size_t dir_len = strlen(dir_path);
		bool needs_sep = dir_len > 0 && dir_path[dir_len - 1] != '/' && dir_path[dir_len - 1] != '\\';
		size_t path_len = dir_len + (needs_sep ? 1 : 0) + strlen(entry->d_name) + 1;
		char *full_path = bmalloc(path_len);

		snprintf(full_path, path_len, "%s%s%s", dir_path, needs_sep ? "/" : "", entry->d_name);
		add_style_path(style_list, full_path);
		bfree(full_path);
	}

	os_closedir(dir);
}

static void populate_style_list(obs_property_t *style_list)
{
	if (!style_list)
		return;

	obs_property_list_clear(style_list);
	obs_property_list_add_string(style_list, "Select a style...", "");

	add_style_files_from_text(style_list, lottie_style_library_get_files());
	add_style_files_from_dir(style_list, lottie_style_library_get_dir());
}

static bool custom_file_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(property);

	bool custom_file = obs_data_get_bool(settings, SETTING_CUSTOM_FILE);
	obs_property_t *style = obs_properties_get(props, SETTING_STYLE_PATH);
	obs_property_t *path = obs_properties_get(props, SETTING_LOTTIE_PATH);

	if (style)
		obs_property_set_visible(style, !custom_file);
	if (path)
		obs_property_set_visible(path, custom_file);

	return true;
}

obs_properties_t *lottie_lower_third_get_properties(void *data)
{
	struct lottie_lower_third *ctx = (struct lottie_lower_third *)data;
	obs_properties_t *props = obs_properties_create();

	obs_property_t *custom = obs_properties_add_bool(props, SETTING_CUSTOM_FILE, "Custom File");
	obs_property_t *style = obs_properties_add_list(props, SETTING_STYLE_PATH, "Style", OBS_COMBO_TYPE_LIST,
							OBS_COMBO_FORMAT_STRING);
	obs_property_t *path = obs_properties_add_path(props, SETTING_LOTTIE_PATH, "Lottie File", OBS_PATH_FILE,
						       "Lottie files (*.json)", NULL);

	obs_property_set_modified_callback(custom, custom_file_modified);

	populate_style_list(style);

	if (ctx)
		obs_property_set_visible(path, ctx->custom_file);
	else
		obs_property_set_visible(path, false);

	obs_property_set_visible(style, !ctx || !ctx->custom_file);

	obs_properties_add_text(props, "text1", "Name", OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "text2", "Title", OBS_TEXT_DEFAULT);

	return props;
}
