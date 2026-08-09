/*
Instant Record
Copyright (C) 2026 Rafael Roman <rafael@instanrp.com>

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
#include "source-record-filter.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_register_source(&source_record_filter_info);
	obs_log(LOG_INFO, "Instant Record loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/* Called after all modules load and the frontend/UI is up — the safe
 * point to attach a dock. */
void obs_module_post_load(void)
{
	instant_record_register_dock();
}

void obs_module_unload(void)
{
	/* Finalize any recordings still rolling so files aren't left open. */
	sr_registry_stop_all();
	obs_log(LOG_INFO, "Instant Record unloaded");
}
