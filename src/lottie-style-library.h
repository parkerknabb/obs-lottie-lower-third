#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *lottie_style_library_get_files(void);
const char *lottie_style_library_get_dir(void);
void lottie_style_library_set(const char *files, const char *dir);
bool lottie_style_library_is_valid_lottie_file(const char *path);

#ifdef __cplusplus
}
#endif
