/*
Instant Record
Copyright (C) 2026 Rafael Roman <support@instanrp.com>

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

/*
 * Exposes Instant Record over obs-websocket as a "vendor" so external
 * controllers (the Instant Replay app, a Stream Deck, etc.) can drive
 * it remotely via CallVendorRequest.
 *
 * Vendor name: "instant-record"
 * Requests:
 *   SaveAll     -> save a replay clip from every buffered camera
 *   StartAll    -> start all cameras
 *   StopAll     -> stop all cameras
 *   SaveCamera  -> save one camera; requestData { "index": N } or { "name": "cam1" }
 *   GetStatus   -> responseData { "cameras": [ {name,status,format,width,height,index} ] }
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <string.h>
#include "obs-websocket-api.h"
#include "source-record-filter.h"

static obs_websocket_vendor vendor = NULL;

static void ws_save_all(obs_data_t *request_data, obs_data_t *response_data, void *priv)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv);
	sr_registry_save_all();
	obs_data_set_bool(response_data, "ok", true);
}

static void ws_start_all(obs_data_t *request_data, obs_data_t *response_data, void *priv)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv);
	sr_registry_start_all();
	obs_data_set_bool(response_data, "ok", true);
}

static void ws_stop_all(obs_data_t *request_data, obs_data_t *response_data, void *priv)
{
	UNUSED_PARAMETER(request_data);
	UNUSED_PARAMETER(priv);
	sr_registry_stop_all();
	obs_data_set_bool(response_data, "ok", true);
}

static void ws_save_camera(obs_data_t *request_data, obs_data_t *response_data, void *priv)
{
	UNUSED_PARAMETER(priv);
	struct sr_status_row rows[64];
	size_t n = sr_registry_snapshot(rows, 64);

	int index = -1;
	if (obs_data_has_user_value(request_data, "index")) {
		index = (int)obs_data_get_int(request_data, "index");
	} else if (obs_data_has_user_value(request_data, "name")) {
		const char *name = obs_data_get_string(request_data, "name");
		for (size_t i = 0; i < n; i++) {
			if (strcmp(rows[i].name, name) == 0) {
				index = (int)i;
				break;
			}
		}
	}

	if (index >= 0 && (size_t)index < n) {
		sr_registry_save_index((size_t)index);
		obs_data_set_bool(response_data, "ok", true);
	} else {
		obs_data_set_bool(response_data, "ok", false);
		obs_data_set_string(response_data, "error", "camera not found");
	}
}

static void ws_get_status(obs_data_t *request_data, obs_data_t *response_data, void *priv)
{
	UNUSED_PARAMETER(request_data);
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
	obs_data_set_array(response_data, "cameras", arr);
	obs_data_array_release(arr);
}

void instant_record_register_websocket(void)
{
	/* Returns NULL if obs-websocket isn't loaded — then we simply do
	 * nothing and the dock/hotkeys still work locally. */
	vendor = obs_websocket_register_vendor("instant-record");
	if (!vendor) {
		obs_log(LOG_INFO, "Instant Record: obs-websocket not available; remote API disabled");
		return;
	}
	obs_websocket_vendor_register_request(vendor, "SaveAll", ws_save_all, NULL);
	obs_websocket_vendor_register_request(vendor, "StartAll", ws_start_all, NULL);
	obs_websocket_vendor_register_request(vendor, "StopAll", ws_stop_all, NULL);
	obs_websocket_vendor_register_request(vendor, "SaveCamera", ws_save_camera, NULL);
	obs_websocket_vendor_register_request(vendor, "GetStatus", ws_get_status, NULL);
	obs_log(LOG_INFO, "Instant Record: obs-websocket vendor 'instant-record' registered");
}

void instant_record_unregister_websocket(void)
{
	if (!vendor)
		return;
	obs_websocket_vendor_unregister_request(vendor, "SaveAll");
	obs_websocket_vendor_unregister_request(vendor, "StartAll");
	obs_websocket_vendor_unregister_request(vendor, "StopAll");
	obs_websocket_vendor_unregister_request(vendor, "SaveCamera");
	obs_websocket_vendor_unregister_request(vendor, "GetStatus");
	vendor = NULL;
}
