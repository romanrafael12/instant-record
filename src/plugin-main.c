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
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <plugin-support.h>
#include <string.h>
#include "source-record-filter.h"
#include "clean-program.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* ============================================================
 * Minimal inline of the obs-websocket vendor API (from the official
 * obs-websocket-api.h) so this plugin needs no extra source files —
 * lets external controllers drive it via CallVendorRequest. If
 * obs-websocket isn't loaded, every call no-ops and the plugin still
 * works locally.
 * ============================================================ */
typedef void *ir_ws_vendor;
typedef void (*ir_ws_request_cb)(obs_data_t *, obs_data_t *, void *);
struct ir_ws_request_callback {
	ir_ws_request_cb callback;
	void *priv_data;
};

static proc_handler_t *ir_ws_ph;

static proc_handler_t *ir_ws_get_ph(void)
{
	proc_handler_t *global_ph = obs_get_proc_handler();
	if (!global_ph)
		return NULL;
	calldata_t cd = {0, 0, 0, 0};
	if (!proc_handler_call(global_ph, "obs_websocket_api_get_ph", &cd)) {
		calldata_free(&cd);
		return NULL;
	}
	proc_handler_t *ret = (proc_handler_t *)calldata_ptr(&cd, "ph");
	calldata_free(&cd);
	return ret;
}

static bool ir_ws_ensure_ph(void)
{
	if (!ir_ws_ph)
		ir_ws_ph = ir_ws_get_ph();
	return ir_ws_ph != NULL;
}

static ir_ws_vendor ir_ws_register_vendor(const char *vendor_name)
{
	if (!ir_ws_ensure_ph())
		return NULL;
	calldata_t cd = {0, 0, 0, 0};
	calldata_set_string(&cd, "name", vendor_name);
	proc_handler_call(ir_ws_ph, "vendor_register", &cd);
	ir_ws_vendor ret = calldata_ptr(&cd, "vendor");
	calldata_free(&cd);
	return ret;
}

static bool ir_ws_register_request(ir_ws_vendor vendor, const char *type, ir_ws_request_cb cb_fn, void *priv)
{
	if (!ir_ws_ensure_ph() || !vendor)
		return false;
	struct ir_ws_request_callback cb = {cb_fn, priv};
	calldata_t cd = {0, 0, 0, 0};
	calldata_set_string(&cd, "type", type);
	calldata_set_ptr(&cd, "callback", &cb);
	calldata_set_ptr(&cd, "vendor", vendor);
	proc_handler_call(ir_ws_ph, "vendor_request_register", &cd);
	bool ok = calldata_bool(&cd, "success");
	calldata_free(&cd);
	return ok;
}

static void ir_ws_unregister_request(ir_ws_vendor vendor, const char *type)
{
	if (!ir_ws_ensure_ph() || !vendor)
		return;
	calldata_t cd = {0, 0, 0, 0};
	calldata_set_string(&cd, "type", type);
	calldata_set_ptr(&cd, "vendor", vendor);
	proc_handler_call(ir_ws_ph, "vendor_request_unregister", &cd);
	calldata_free(&cd);
}

/* ---- Vendor "instant-record": remote control requests ---- */
static ir_ws_vendor ir_vendor = NULL;

static void ws_save_all(obs_data_t *req, obs_data_t *resp, void *priv)
{
	UNUSED_PARAMETER(req);
	UNUSED_PARAMETER(priv);
	sr_registry_save_all();
	obs_data_set_bool(resp, "ok", true);
}
static void ws_start_all(obs_data_t *req, obs_data_t *resp, void *priv)
{
	UNUSED_PARAMETER(req);
	UNUSED_PARAMETER(priv);
	sr_registry_start_all();
	obs_data_set_bool(resp, "ok", true);
}
static void ws_stop_all(obs_data_t *req, obs_data_t *resp, void *priv)
{
	UNUSED_PARAMETER(req);
	UNUSED_PARAMETER(priv);
	sr_registry_stop_all();
	obs_data_set_bool(resp, "ok", true);
}
static void ws_save_camera(obs_data_t *req, obs_data_t *resp, void *priv)
{
	UNUSED_PARAMETER(priv);
	struct sr_status_row rows[64];
	size_t n = sr_registry_snapshot(rows, 64);
	int index = -1;
	if (obs_data_has_user_value(req, "index")) {
		index = (int)obs_data_get_int(req, "index");
	} else if (obs_data_has_user_value(req, "name")) {
		const char *name = obs_data_get_string(req, "name");
		for (size_t i = 0; i < n; i++) {
			if (strcmp(rows[i].name, name) == 0) {
				index = (int)i;
				break;
			}
		}
	}
	if (index >= 0 && (size_t)index < n) {
		sr_registry_save_index((size_t)index);
		obs_data_set_bool(resp, "ok", true);
	} else {
		obs_data_set_bool(resp, "ok", false);
		obs_data_set_string(resp, "error", "camera not found");
	}
}
static void ws_get_status(obs_data_t *req, obs_data_t *resp, void *priv)
{
	UNUSED_PARAMETER(req);
	UNUSED_PARAMETER(priv);
	struct sr_status_row rows[64];
	size_t n = sr_registry_snapshot(rows, 64);
	obs_data_array_t *arr = obs_data_array_create();
	for (size_t i = 0; i < n; i++) {
		const char *st = "idle";
		if (rows[i].status == SR_STATUS_RECORDING)
			st = rows[i].use_buffer ? "buffer" : "recording";
		else if (rows[i].status == SR_STATUS_ERROR)
			st = "error";
		obs_data_t *o = obs_data_create();
		obs_data_set_int(o, "index", (int)i);
		obs_data_set_string(o, "name", rows[i].name);
		obs_data_set_string(o, "status", st);
		obs_data_set_string(o, "format", rows[i].format);
		obs_data_set_int(o, "width", rows[i].width);
		obs_data_set_int(o, "height", rows[i].height);
		obs_data_array_push_back(arr, o);
		obs_data_release(o);
	}
	obs_data_set_array(resp, "cameras", arr);
	obs_data_array_release(arr);
}

static void ir_ws_register(void)
{
	ir_vendor = ir_ws_register_vendor("instant-record");
	if (!ir_vendor) {
		obs_log(LOG_INFO, "Instant Record: obs-websocket not available; remote API disabled");
		return;
	}
	ir_ws_register_request(ir_vendor, "SaveAll", ws_save_all, NULL);
	ir_ws_register_request(ir_vendor, "StartAll", ws_start_all, NULL);
	ir_ws_register_request(ir_vendor, "StopAll", ws_stop_all, NULL);
	ir_ws_register_request(ir_vendor, "SaveCamera", ws_save_camera, NULL);
	ir_ws_register_request(ir_vendor, "GetStatus", ws_get_status, NULL);
	obs_log(LOG_INFO, "Instant Record: obs-websocket vendor 'instant-record' registered");
}

static void ir_ws_unregister(void)
{
	if (!ir_vendor)
		return;
	ir_ws_unregister_request(ir_vendor, "SaveAll");
	ir_ws_unregister_request(ir_vendor, "StartAll");
	ir_ws_unregister_request(ir_vendor, "StopAll");
	ir_ws_unregister_request(ir_vendor, "SaveCamera");
	ir_ws_unregister_request(ir_vendor, "GetStatus");
	ir_vendor = NULL;
}

/* Global (frontend) hotkeys — these always show up in OBS
 * Settings -> Hotkeys, unlike per-source filter hotkeys. */
static obs_hotkey_id hk_save_all = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hk_start_all = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hk_stop_all = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id hk_clean_toggle = OBS_INVALID_HOTKEY_ID;

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

static void hk_clean_toggle_cb(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (pressed)
		clean_program_toggle();
}

bool obs_module_load(void)
{
	obs_register_source(&source_record_filter_info);
	obs_log(LOG_INFO, "Instant Record loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

/* Called after all modules load and the frontend/UI is up — the safe
 * point to attach a dock and register frontend hotkeys. */
/* We persist the global hotkey bindings ourselves (in the plugin's
 * config dir) instead of relying on OBS's frontend persistence, which
 * doesn't reliably restore them for hotkeys registered at post_load. */
static void ir_hotkeys_save(void)
{
	obs_data_t *data = obs_data_create();
	obs_data_array_t *a;

	a = obs_hotkey_save(hk_save_all);
	obs_data_set_array(data, "InstantRecord.SaveAll", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(hk_start_all);
	obs_data_set_array(data, "InstantRecord.StartAll", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(hk_stop_all);
	obs_data_set_array(data, "InstantRecord.StopAll", a);
	obs_data_array_release(a);
	a = obs_hotkey_save(hk_clean_toggle);
	obs_data_set_array(data, "InstantRecord.CleanProgram", a);
	obs_data_array_release(a);

	char *dir = obs_module_get_config_path(obs_current_module(), "");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_get_config_path(obs_current_module(), "hotkeys.json");
	if (path) {
		obs_data_save_json(data, path);
		bfree(path);
	}
	obs_data_release(data);
}

static void ir_hotkeys_load(void)
{
	char *path = obs_module_get_config_path(obs_current_module(), "hotkeys.json");
	if (!path)
		return;
	obs_data_t *data = obs_data_create_from_json_file(path);
	bfree(path);
	if (!data)
		return;

	obs_data_array_t *a;
	a = obs_data_get_array(data, "InstantRecord.SaveAll");
	obs_hotkey_load(hk_save_all, a);
	obs_data_array_release(a);
	a = obs_data_get_array(data, "InstantRecord.StartAll");
	obs_hotkey_load(hk_start_all, a);
	obs_data_array_release(a);
	a = obs_data_get_array(data, "InstantRecord.StopAll");
	obs_hotkey_load(hk_stop_all, a);
	obs_data_array_release(a);
	a = obs_data_get_array(data, "InstantRecord.CleanProgram");
	obs_hotkey_load(hk_clean_toggle, a);
	obs_data_array_release(a);

	obs_data_release(data);
}

/* Save bindings whenever OBS is about to exit, so a normal close keeps
 * the user's keys even if unload timing varies. */
static void ir_frontend_event(enum obs_frontend_event event, void *priv)
{
	UNUSED_PARAMETER(priv);
	if (event == OBS_FRONTEND_EVENT_EXIT)
		ir_hotkeys_save();
}

void obs_module_post_load(void)
{
	/* Clean Program follows the live camera across scene cuts into one
	 * continuous, graphics-free file. Init FIRST — the dock's refresh()
	 * queries clean-program state, so its mutex must exist before the
	 * dock is constructed. */
	clean_program_init();

	instant_record_register_dock();
	ir_ws_register();

	hk_save_all = obs_hotkey_register_frontend("InstantRecord.SaveAll",
						   obs_module_text("InstantRecord.Hotkey.SaveAll"), hk_save_all_cb,
						   NULL);
	hk_start_all = obs_hotkey_register_frontend("InstantRecord.StartAll",
						    obs_module_text("InstantRecord.Hotkey.StartAll"), hk_start_all_cb,
						    NULL);
	hk_stop_all = obs_hotkey_register_frontend("InstantRecord.StopAll",
						   obs_module_text("InstantRecord.Hotkey.StopAll"), hk_stop_all_cb,
						   NULL);
	hk_clean_toggle = obs_hotkey_register_frontend("InstantRecord.CleanProgram",
						       obs_module_text("InstantRecord.Hotkey.CleanProgram"),
						       hk_clean_toggle_cb, NULL);

	/* Restore the user's saved key bindings. */
	ir_hotkeys_load();
	obs_frontend_add_event_callback(ir_frontend_event, NULL);
}

void obs_module_unload(void)
{
	ir_ws_unregister();

	/* Persist bindings before we tear the hotkeys down. */
	ir_hotkeys_save();
	obs_frontend_remove_event_callback(ir_frontend_event, NULL);

	if (hk_save_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_save_all);
	if (hk_start_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_start_all);
	if (hk_stop_all != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_stop_all);
	if (hk_clean_toggle != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(hk_clean_toggle);

	/* Stop the clean-program recording and detach its callback. */
	clean_program_shutdown();

	/* Finalize any recordings still rolling so files aren't left open. */
	sr_registry_stop_all();
	obs_log(LOG_INFO, "Instant Record unloaded");
}
