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

/*
 * Clean Program records the switch between cameras — the "program" —
 * WITHOUT the on-air graphics burned in, into ONE continuous file.
 *
 * How it works: we keep a single private view + encoder + output. The
 * view renders exactly one source at a time: the camera of the current
 * scene. When the user cuts to another scene, we look inside that scene
 * for the source carrying the per-camera Instant Record filter and point
 * the view at it — the encoder keeps rolling to the SAME file, so the cut
 * happens inside one recording with no graphics and no file split.
 *
 * On scenes with no clear camera (PiP, replay, playlists) we keep showing
 * the last camera, so the file never goes black.
 *
 * This module is deliberately self-contained: it does not touch the
 * per-camera filter code, so a problem here can't destabilise the
 * buffer/record modes that already ship.
 */

#include "clean-program.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/threading.h>
#include <plugin-support.h>
#include <string.h>
#include <time.h>

/* The per-camera filter's registered id — a source belongs to a camera
 * when it carries this filter. Kept as a literal so this file stays
 * independent of the filter's header. */
#define IR_FILTER_ID "instant_record_filter"

struct clean_program {
	pthread_mutex_t mutex;

	bool active;

	obs_view_t *view;
	video_t *video_output;
	obs_encoder_t *venc;
	obs_encoder_t *aenc;
	obs_output_t *output;

	/* The camera the view is currently following (weak ref just for
	 * comparison/logging; the view holds the real reference). */
	obs_source_t *current_cam;
};

static struct clean_program g = {0};
static bool g_inited = false;

/* ---- finding the camera of a scene ------------------------------------ */

struct filter_probe {
	bool has_ir_filter;
};

static void probe_filter_cb(obs_source_t *parent, obs_source_t *filter, void *param)
{
	UNUSED_PARAMETER(parent);
	struct filter_probe *p = param;
	const char *id = obs_source_get_id(filter);
	if (id && strcmp(id, IR_FILTER_ID) == 0)
		p->has_ir_filter = true;
}

struct find_cam {
	obs_source_t *found; /* +1 ref when set */
};

static bool find_cam_item_cb(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	UNUSED_PARAMETER(scene);
	struct find_cam *fc = param;

	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return true; /* keep looking */

	struct filter_probe probe = {0};
	obs_source_enum_filters(src, probe_filter_cb, &probe);
	if (probe.has_ir_filter) {
		fc->found = obs_source_get_ref(src);
		return false; /* stop: first camera wins */
	}
	return true; /* keep looking */
}

/* Returns the camera source of the current program scene (+1 ref), or
 * NULL if that scene has no source carrying the filter. Caller releases. */
static obs_source_t *find_current_camera(void)
{
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	if (!scene_src)
		return NULL;

	obs_scene_t *scene = obs_scene_from_source(scene_src);
	struct find_cam fc = {0};
	if (scene)
		obs_scene_enum_items(scene, find_cam_item_cb, &fc);

	obs_source_release(scene_src);
	return fc.found;
}

/* Point the view at the current scene's camera. If the scene has no
 * camera, keep the last one (do nothing) so the file never goes black.
 * Caller must hold g.mutex. */
static void follow_current_scene_locked(void)
{
	if (!g.active || !g.view)
		return;

	obs_source_t *cam = find_current_camera();
	if (!cam)
		return; /* weird scene → keep last camera */

	if (cam == g.current_cam) {
		obs_source_release(cam);
		return; /* already following it */
	}

	obs_view_set_source(g.view, 0, cam);
	g.current_cam = cam; /* view now holds the strong ref */
	obs_log(LOG_INFO, "[instant-record] clean program → following '%s'", obs_source_get_name(cam));
	obs_source_release(cam); /* our probe ref; the view keeps its own */
}

/* ---- build / tear down the recording chain ---------------------------- */

static char *build_output_path(void)
{
	char *dir = obs_frontend_get_current_record_output_path();
	const char *base = (dir && *dir) ? dir : ".";

	char stamp[64];
	time_t t = time(NULL);
	struct tm lt;
#ifdef _WIN32
	localtime_s(&lt, &t);
#else
	localtime_r(&t, &lt);
#endif
	strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &lt);

	size_t need = strlen(base) + 64;
	char *path = bmalloc(need);
	/* .mkv survives a crash and never needs a moov-atom finalize, so a
	 * long program recording is safe even if OBS dies mid-record. */
	snprintf(path, need, "%s/CleanProgram_%s.mkv", base, stamp);

	bfree(dir);
	return path;
}

/* Try one encoder id; returns a created video encoder or NULL. */
static obs_encoder_t *make_video_encoder(const char *enc_id)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "bitrate", 8000);
	obs_encoder_t *e = obs_video_encoder_create(enc_id, "instant_record_clean_venc", s, NULL);
	obs_data_release(s);
	return e;
}

/* Caller holds g.mutex. Releases whatever is half-built. */
static void teardown_chain_locked(void)
{
	if (g.output) {
		obs_output_release(g.output); /* waits for a graceful stop */
		g.output = NULL;
	}
	if (g.venc) {
		obs_encoder_release(g.venc);
		g.venc = NULL;
	}
	if (g.aenc) {
		obs_encoder_release(g.aenc);
		g.aenc = NULL;
	}
	if (g.view) {
		obs_view_set_source(g.view, 0, NULL);
		obs_view_remove(g.view);
		obs_view_destroy(g.view);
		g.view = NULL;
		g.video_output = NULL;
	}
	g.current_cam = NULL;
	g.active = false;
}

/* ---- public API ------------------------------------------------------- */

bool clean_program_active(void)
{
	pthread_mutex_lock(&g.mutex);
	bool a = g.active;
	pthread_mutex_unlock(&g.mutex);
	return a;
}

bool clean_program_start(void)
{
	pthread_mutex_lock(&g.mutex);
	if (g.active) {
		pthread_mutex_unlock(&g.mutex);
		return true;
	}

	/* Find the camera of the current scene and size the recording to it,
	 * so the camera FILLS the whole frame (Option A: full-screen clean
	 * feed, ignoring how it's scaled/placed inside the scene). All cameras
	 * are expected to share a resolution, so this base stays correct across
	 * cuts. */
	obs_source_t *cam0 = find_current_camera(); /* +1 ref or NULL */

	struct obs_video_info ovi = {0};
	obs_get_video_info(&ovi);
	uint32_t cw = 0, ch = 0;
	if (cam0) {
		cw = obs_source_get_base_width(cam0);
		ch = obs_source_get_base_height(cam0);
	}
	if (cw == 0 || ch == 0) {
		cw = ovi.base_width; /* no camera yet → canvas size */
		ch = ovi.base_height;
	}
	ovi.base_width = cw;
	ovi.base_height = ch;
	ovi.output_width = cw;
	ovi.output_height = ch;

	g.view = obs_view_create();
	if (cam0) {
		obs_view_set_source(g.view, 0, cam0);
		g.current_cam = cam0; /* the view now holds the strong ref */
	}
	g.video_output = obs_view_add2(g.view, &ovi);
	if (!g.video_output) {
		obs_log(LOG_ERROR, "[instant-record] clean program: obs_view_add2 failed");
		if (cam0)
			obs_source_release(cam0);
		teardown_chain_locked();
		pthread_mutex_unlock(&g.mutex);
		return false;
	}

	/* Prefer NVENC (keeps the CPU free); fall back to x264. */
	g.venc = make_video_encoder("jim_nvenc");
	if (!g.venc)
		g.venc = make_video_encoder("obs_x264");
	obs_encoder_set_video(g.venc, g.video_output);

	/* Record the global OBS mix (program/commentary audio). */
	g.aenc = obs_audio_encoder_create("ffmpeg_aac", "instant_record_clean_aenc", NULL, 0, NULL);
	obs_encoder_set_audio(g.aenc, obs_get_audio());

	char *path = build_output_path();
	obs_data_t *outset = obs_data_create();
	obs_data_set_string(outset, "path", path);
	g.output = obs_output_create("ffmpeg_muxer", "instant_record_clean_output", outset, NULL);
	obs_data_release(outset);

	obs_output_set_video_encoder(g.output, g.venc);
	obs_output_set_audio_encoder(g.output, g.aenc, 0);

	if (!obs_output_start(g.output)) {
		const char *err = obs_output_get_last_error(g.output);
		obs_log(LOG_ERROR, "[instant-record] clean program: start failed: %s", err ? err : "(no detail)");
		bfree(path);
		if (cam0)
			obs_source_release(cam0);
		teardown_chain_locked();
		pthread_mutex_unlock(&g.mutex);
		return false;
	}

	g.active = true;
	obs_log(LOG_INFO, "[instant-record] clean program recording (%ux%u) → %s", cw, ch, path);
	bfree(path);

	/* Our probe ref on cam0; the view keeps its own reference. */
	if (cam0)
		obs_source_release(cam0);

	pthread_mutex_unlock(&g.mutex);
	return true;
}

void clean_program_stop(void)
{
	pthread_mutex_lock(&g.mutex);
	if (!g.active) {
		pthread_mutex_unlock(&g.mutex);
		return;
	}
	/* Ask for a graceful stop, then release (which flushes the muxer).
	 * We do the release inside teardown; obs_output_release waits for
	 * the stop to finish so the file is finalised cleanly. */
	obs_output_stop(g.output);
	teardown_chain_locked();
	obs_log(LOG_INFO, "[instant-record] clean program stopped");
	pthread_mutex_unlock(&g.mutex);
}

bool clean_program_toggle(void)
{
	if (clean_program_active()) {
		clean_program_stop();
		return false;
	}
	return clean_program_start();
}

/* ---- frontend event: follow the program across cuts ------------------- */

static void clean_frontend_event(enum obs_frontend_event event, void *priv)
{
	UNUSED_PARAMETER(priv);
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		pthread_mutex_lock(&g.mutex);
		follow_current_scene_locked();
		pthread_mutex_unlock(&g.mutex);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		clean_program_stop();
		break;
	default:
		break;
	}
}

void clean_program_init(void)
{
	if (g_inited)
		return;
	pthread_mutex_init(&g.mutex, NULL);
	obs_frontend_add_event_callback(clean_frontend_event, NULL);
	g_inited = true;
}

void clean_program_shutdown(void)
{
	if (!g_inited)
		return;
	obs_frontend_remove_event_callback(clean_frontend_event, NULL);
	clean_program_stop();
	pthread_mutex_destroy(&g.mutex);
	g_inited = false;
}
