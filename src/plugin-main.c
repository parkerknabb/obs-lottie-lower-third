/*
OBS Lottie Lower Third
Copyright (C) 2026 Parker Knabb

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <thorvg_capi.h>

extern void register_lottie_lower_third_source(void);
extern void unregister_lottie_lower_third_source(void);

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "OBS Lottie Lower Third loaded successfully (version %s)", PLUGIN_VERSION);
	tvg_engine_init(0);
	register_lottie_lower_third_source();

	return true;
}

void obs_module_unload(void)
{
	tvg_engine_term();
	obs_log(LOG_INFO, "OBS Lottie Lower Third unloaded");
}
