/*
Instant Record — Clean Program recorder
Copyright (C) 2025

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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Lifecycle — call once from the plugin's post_load / unload. Init
 * registers the scene-change follower; shutdown stops any recording
 * in progress and detaches the callback. */
void clean_program_init(void);
void clean_program_shutdown(void);

/* Start/stop the clean-program recording. Safe to call from the UI or
 * a hotkey thread. clean_program_toggle() starts if idle, stops if
 * recording. Returns true if a recording is running after the call. */
bool clean_program_start(void);
void clean_program_stop(void);
bool clean_program_toggle(void);

/* True while a clean-program recording is active. */
bool clean_program_active(void);

/* Seconds since the recording started (0 if idle) — for the dock timer. */
long long clean_program_elapsed_s(void);

/* Copies the name of the camera being followed right now into buf
 * (empty string if idle / none). For the dock's "recording now" label. */
void clean_program_current_cam(char *buf, unsigned long buflen);

#ifdef __cplusplus
}
#endif
