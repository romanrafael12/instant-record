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

/* Global (frontend) hotkeys — these always show up in OBS
 * Settings -> Hotkeys, unlike per-source filter hotkeys. */
static obs_hotkey_id hk_save_all = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hk_start_all = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hk_stop_all = OBS_INVALID_HOTKEY_ID;

static void hk_save_all_cb(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (pressed)
		sr_registry_save_all();
}

static void hk_start_all_cb(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (pressed)
		sr_registry_start_all();
}

static void hk_stop_all_cb(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (pressed)
		sr_registry_stop_all();
}

bool obs_module_load(void)
{
	obs_register_source(&source_record_filter_info);
	obs_log(LOG_INFO, "Instant Record loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/* Called after all modules load and the frontend/UI is up — the safe
 * point to attach a dock and register frontend hotkeys. */
void obs_module_post_load(void)
{
	instant_record_register_dock();

	hk_save_all = obs_hotkey_register_frontend("InstantRecord.SaveAll",
						   obs_module_text("InstantRecord.Hotkey.SaveAll"), hk_save_all_cb,
						   NULL);
	hk_start_all = obs_hotkey_register_frontend("InstantRecord.StartAll",
						    obs_module_text("InstantRecord.Hotkey.StartAll"), hk_start_all_cb,
						    NULL);
	hk_stop_all = obs_hotkey_register_frontend("InstantRecord.StopAll",
						   obs_module_text("InstantRecord.Hotkey.StopAll"), hk_stop_all_cb,
						   NULL);
}

void obs_module_unload(void)
{
	if (hk_save_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_save_all);
	if (hk_start_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_start_all);
	if (hk_stop_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_stop_all);

	/* Finalize any recordings still rolling so files aren't left open. */
	sr_registry_stop_all();
	obs_log(LOG_INFO, "Instant Record unloaded");
}
