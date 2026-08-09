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

#include "source-record-filter.h"
#include <plugin-support.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <util/threading.h>
#include <time.h>

/* Refuse to start a recording with less than this much free space. */
#define SR_MIN_FREE_BYTES (512ULL * 1024ULL * 1024ULL)

/* These are public OBS output result codes; guard them so the build
 * never breaks if a header layout differs across OBS versions. If OBS
 * already defines them, the real values win. */
#ifndef OBS_OUTPUT_SUCCESS
#define OBS_OUTPUT_SUCCESS 0
#endif
#ifndef OBS_OUTPUT_NO_SPACE
#define OBS_OUTPUT_NO_SPACE (-7)
#endif

/* Staggered start: spin cameras up at least this far apart so a whole
 * rig starting at once doesn't hit the encoders in a single spike. */
#define SR_START_SPACING_NS (300ULL * 1000ULL * 1000ULL) /* 300 ms */
static int64_t sr_last_start_ns = 0;
static pthread_mutex_t sr_start_gate = PTHREAD_MUTEX_INITIALIZER;

/* Returns true and claims the slot if enough time has passed since the
 * last start anywhere; false means "try again next tick". */
static bool sr_start_gate_take(void)
{
	bool ok;
	pthread_mutex_lock(&sr_start_gate);
	int64_t now = os_gettime_ns();
	ok = (now - sr_last_start_ns) >= (int64_t)SR_START_SPACING_NS;
	if (ok)
		sr_last_start_ns = now;
	pthread_mutex_unlock(&sr_start_gate);
	return ok;
}

/* ================================================================== */
/* Registry — every live recorder, so we can stop them all on unload  */
/* and (later) feed a status dock.                                    */
/* ================================================================== */

static DARRAY(struct source_record_filter *) sr_registry;
static pthread_mutex_t sr_registry_lock = PTHREAD_MUTEX_INITIALIZER;
static bool sr_registry_init = false;

static void registry_add(struct source_record_filter *f)
{
	pthread_mutex_lock(&sr_registry_lock);
	if (!sr_registry_init) {
		da_init(sr_registry);
		sr_registry_init = true;
	}
	da_push_back(sr_registry, &f);
	pthread_mutex_unlock(&sr_registry_lock);
}

static void registry_remove(struct source_record_filter *f)
{
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num; i++) {
		if (sr_registry.array[i] == f) {
			da_erase(sr_registry, i);
			break;
		}
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

/* Forward decls. */
static void stop_file_output_locked(struct source_record_filter *f);
static void on_output_stopped(void *data, calldata_t *cd);

void sr_registry_stop_all(void)
{
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num; i++) {
		struct source_record_filter *f = sr_registry.array[i];
		pthread_mutex_lock(&f->mutex);
		if (f->active)
			stop_file_output_locked(f);
		pthread_mutex_unlock(&f->mutex);
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

static bool start_file_output_locked(struct source_record_filter *f);

void sr_registry_start_all(void)
{
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num; i++) {
		struct source_record_filter *f = sr_registry.array[i];
		pthread_mutex_lock(&f->mutex);
		if (!f->active)
			f->want_start = true; /* video_tick starts it, gated + staggered */
		pthread_mutex_unlock(&f->mutex);
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

/* Save a replay clip from this recorder's buffer (caller holds mutex). */
static void sr_save_locked(struct source_record_filter *f)
{
	if (!f->use_buffer || !f->fileOutput || !f->active)
		return;
	proc_handler_t *ph = obs_output_get_proc_handler(f->fileOutput);
	if (!ph)
		return;
	calldata_t cd = {0};
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
	obs_log(LOG_INFO, "[instant-record] saved replay clip");
}

void sr_registry_save_all(void)
{
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num; i++) {
		struct source_record_filter *f = sr_registry.array[i];
		pthread_mutex_lock(&f->mutex);
		sr_save_locked(f);
		pthread_mutex_unlock(&f->mutex);
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

void sr_registry_save_index(size_t index)
{
	pthread_mutex_lock(&sr_registry_lock);
	if (index < sr_registry.num) {
		struct source_record_filter *f = sr_registry.array[index];
		pthread_mutex_lock(&f->mutex);
		sr_save_locked(f);
		pthread_mutex_unlock(&f->mutex);
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

size_t sr_registry_snapshot(struct sr_status_row *rows, size_t max_rows)
{
	size_t n = 0;
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num && n < max_rows; i++) {
		struct source_record_filter *f = sr_registry.array[i];
		pthread_mutex_lock(&f->mutex);
		obs_source_t *parent = obs_filter_get_parent(f->source);
		const char *nm = parent ? obs_source_get_name(parent) : "(sin fuente)";
		snprintf(rows[n].name, sizeof(rows[n].name), "%s", nm ? nm : "(sin fuente)");
		snprintf(rows[n].format, sizeof(rows[n].format), "%s", f->rec_format ? f->rec_format : "mkv");
		rows[n].status = f->status;
		rows[n].width = f->width;
		rows[n].height = f->height;
		rows[n].elapsed_ns =
			(f->active && f->status == SR_STATUS_RECORDING) ? (os_gettime_ns() - f->start_timestamp_ns) : 0;
		rows[n].last_error_code = f->last_error_code;
		rows[n].use_buffer = f->use_buffer;
		pthread_mutex_unlock(&f->mutex);
		n++;
	}
	pthread_mutex_unlock(&sr_registry_lock);
	return n;
}

void sr_registry_apply_config(const struct sr_global_config *cfg)
{
	pthread_mutex_lock(&sr_registry_lock);
	for (size_t i = 0; i < sr_registry.num; i++) {
		struct source_record_filter *f = sr_registry.array[i];
		/* Update the persisted settings so the change survives save/
		 * reload and shows in each filter's UI; obs_source_update then
		 * calls sr_update, which refreshes runtime fields under lock. */
		obs_data_t *s = obs_source_get_settings(f->source);
		if (cfg->path)
			obs_data_set_string(s, "path", cfg->path);
		if (cfg->rec_format)
			obs_data_set_string(s, "rec_format", cfg->rec_format);
		if (cfg->encoder_id)
			obs_data_set_string(s, "encoder", cfg->encoder_id);
		if (cfg->bitrate > 0)
			obs_data_set_int(s, "bitrate", cfg->bitrate);
		if (cfg->record_mode >= 0)
			obs_data_set_int(s, "record_mode", cfg->record_mode);
		if (cfg->isolate_audio >= 0)
			obs_data_set_bool(s, "isolate_audio", cfg->isolate_audio != 0);
		if (cfg->scale_mode >= 0)
			obs_data_set_int(s, "scale_mode", cfg->scale_mode);
		if (cfg->fps_divisor > 0)
			obs_data_set_int(s, "fps_divisor", cfg->fps_divisor);
		if (cfg->use_buffer >= 0)
			obs_data_set_bool(s, "use_buffer", cfg->use_buffer != 0);
		if (cfg->buffer_seconds > 0)
			obs_data_set_int(s, "buffer_seconds", cfg->buffer_seconds);
		obs_source_update(f->source, s);
		obs_data_release(s);
	}
	pthread_mutex_unlock(&sr_registry_lock);
}

/* ================================================================== */
/* Isolated source audio: capture callback (push) -> ring -> pull     */
/* ================================================================== */

static void ring_init(struct sr_audio_ring *r, uint32_t channels, uint32_t rate)
{
	pthread_mutex_init(&r->lock, NULL);
	r->channels = channels;
	r->sample_rate = rate;
	r->capacity_frames = (size_t)rate * 2; /* 2 seconds of slack */
	for (uint32_t c = 0; c < channels; c++)
		r->data[c] = bzalloc(sizeof(float) * r->capacity_frames);
	r->write_pos = r->read_pos = r->available = 0;
	r->ready = true;
}

static void ring_free(struct sr_audio_ring *r)
{
	pthread_mutex_lock(&r->lock);
	if (!r->ready) {
		pthread_mutex_unlock(&r->lock);
		return;
	}
	r->ready = false; /* callbacks waiting on the lock will now bail */
	for (uint32_t c = 0; c < r->channels; c++) {
		bfree(r->data[c]);
		r->data[c] = NULL;
	}
	pthread_mutex_unlock(&r->lock);
	pthread_mutex_destroy(&r->lock);
}

static void ring_push(struct sr_audio_ring *r, const float **in, uint32_t frames)
{
	pthread_mutex_lock(&r->lock);
	if (!r->ready || !r->data[0]) { /* torn down; don't touch freed memory */
		pthread_mutex_unlock(&r->lock);
		return;
	}
	for (uint32_t i = 0; i < frames; i++) {
		for (uint32_t c = 0; c < r->channels; c++)
			r->data[c][r->write_pos] = in[c] ? in[c][i] : 0.0f;
		r->write_pos = (r->write_pos + 1) % r->capacity_frames;
		if (r->available < r->capacity_frames)
			r->available++;
		else /* overflow: drop oldest */
			r->read_pos = (r->read_pos + 1) % r->capacity_frames;
	}
	pthread_mutex_unlock(&r->lock);
}

/* Fill `frames` per channel; zero-fill on underrun. */
static void ring_pull(struct sr_audio_ring *r, float **out, uint32_t frames)
{
	pthread_mutex_lock(&r->lock);
	if (!r->ready || !r->data[0]) { /* torn down; emit silence */
		for (uint32_t c = 0; c < r->channels; c++)
			for (uint32_t i = 0; i < frames; i++)
				out[c][i] = 0.0f;
		pthread_mutex_unlock(&r->lock);
		return;
	}
	for (uint32_t i = 0; i < frames; i++) {
		bool have = r->available > 0;
		for (uint32_t c = 0; c < r->channels; c++)
			out[c][i] = have ? r->data[c][r->read_pos] : 0.0f;
		if (have) {
			r->read_pos = (r->read_pos + 1) % r->capacity_frames;
			r->available--;
		}
	}
	pthread_mutex_unlock(&r->lock);
}

/* Called by OBS with the parent source's audio. */
static void sr_audio_capture(void *param, obs_source_t *source, const struct audio_data *ad, bool muted)
{
	UNUSED_PARAMETER(source);
	struct source_record_filter *f = param;
	if (!f->audio_ring.ready)
		return;
	const float *planes[MAX_AUDIO_CHANNELS] = {0};
	for (uint32_t c = 0; c < f->audio_ring.channels; c++)
		planes[c] = muted ? NULL : (const float *)ad->data[c];
	ring_push(&f->audio_ring, planes, ad->frames);
}

/* Pull model for the custom audio_output. */
static bool sr_audio_input(void *param, uint64_t start_ts, uint64_t end_ts, uint64_t *out_ts, uint32_t mixers,
			   struct audio_output_data *mixes)
{
	UNUSED_PARAMETER(end_ts);
	UNUSED_PARAMETER(mixers);
	struct source_record_filter *f = param;
	if (!f->audio_ring.ready)
		return false;
	float *out[MAX_AUDIO_CHANNELS];
	for (uint32_t c = 0; c < f->audio_ring.channels; c++)
		out[c] = mixes[0].data[c];
	ring_pull(&f->audio_ring, out, AUDIO_OUTPUT_FRAMES);
	*out_ts = start_ts;
	return true;
}

static bool open_isolated_audio(struct source_record_filter *f, obs_source_t *parent)
{
	const audio_t *global = obs_get_audio();
	uint32_t rate = audio_output_get_sample_rate(global);
	uint32_t channels = audio_output_get_channels(global);
	const struct audio_output_info *goi = audio_output_get_info(global);

	ring_init(&f->audio_ring, channels, rate);

	struct audio_output_info aoi = {0};
	aoi.name = "instant_record_audio";
	aoi.samples_per_sec = rate;
	aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
	aoi.speakers = goi->speakers;
	aoi.input_param = f;
	aoi.input_callback = sr_audio_input;

	if (audio_output_open(&f->audio_output, &aoi) != 0) {
		obs_log(LOG_ERROR, "[instant-record] audio_output_open failed; falling back to global audio");
		ring_free(&f->audio_ring);
		f->audio_output = NULL;
		return false;
	}

	obs_source_add_audio_capture_callback(parent, sr_audio_capture, f);
	f->audio_cb_active = true;
	return true;
}

static void close_isolated_audio(struct source_record_filter *f, obs_source_t *parent)
{
	if (f->audio_cb_active && parent) {
		obs_source_remove_audio_capture_callback(parent, sr_audio_capture, f);
		f->audio_cb_active = false;
	}
	if (f->audio_output) {
		audio_output_close(f->audio_output);
		f->audio_output = NULL;
	}
	ring_free(&f->audio_ring);
}

/* ================================================================== */
/* Filename helpers                                                   */
/* ================================================================== */

static void sanitize(struct dstr *s)
{
	for (size_t i = 0; i < s->len; i++) {
		char c = s->array[i];
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
		    c == '>' || c == '|' || (unsigned char)c < 0x20 || c == ' ')
			s->array[i] = '_';
	}
}

static void build_output_filepath(struct source_record_filter *f, struct dstr *out)
{
	obs_source_t *parent = obs_filter_get_parent(f->source);
	const char *raw = parent ? obs_source_get_name(parent) : "source";

	struct dstr name;
	dstr_init_copy(&name, raw && *raw ? raw : "source");
	sanitize(&name);

	char stamp[64];
	time_t now = time(NULL);
	struct tm tmv;
#ifdef _WIN32
	localtime_s(&tmv, &now);
#else
	localtime_r(&now, &tmv);
#endif
	strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tmv);

	dstr_init(out);
	/* name + timestamp + per-filter index guarantees uniqueness even
	 * for identically named sources started in the same second. */
	dstr_printf(out, "%s/%s_%s_%03d.%s", f->path && *f->path ? f->path : ".", name.array, stamp,
		    f->file_index++, f->rec_format && *f->rec_format ? f->rec_format : "mkv");
	dstr_free(&name);
}

/* ================================================================== */
/* Sync sidecar — one <clip>.json per recording with precise start/    */
/* stop timestamps so cameras can be aligned in post even though they   */
/* start staggered.                                                    */
/* ================================================================== */

static void write_sidecar(struct source_record_filter *f, bool final, int64_t stop_ns)
{
	if (!f->current_filepath)
		return;
	obs_source_t *parent = obs_filter_get_parent(f->source);

	obs_data_t *m = obs_data_create();
	obs_data_set_string(m, "source", parent ? obs_source_get_name(parent) : "source");
	obs_data_set_string(m, "file", f->current_filepath);
	obs_data_set_string(m, "encoder", f->used_encoder ? f->used_encoder : "");
	obs_data_set_int(m, "width", f->out_w);
	obs_data_set_int(m, "height", f->out_h);
	obs_data_set_int(m, "fps_divisor", f->used_fps_div);
	/* Monotonic clock: shared by every camera on this machine, so the
	 * difference between two cameras' start_monotonic_ns IS their exact
	 * offset — align on this. */
	obs_data_set_int(m, "start_monotonic_ns", f->start_timestamp_ns);
	obs_data_set_int(m, "start_unix_s", f->start_unix);
	obs_data_set_bool(m, "complete", final);
	if (final) {
		obs_data_set_int(m, "stop_monotonic_ns", stop_ns);
		obs_data_set_int(m, "duration_ms", (stop_ns - f->start_timestamp_ns) / 1000000LL);
	}

	struct dstr side;
	dstr_init_copy(&side, f->current_filepath);
	dstr_cat(&side, ".json");
	obs_data_save_json_safe(m, side.array, "tmp", "bak");
	dstr_free(&side);
	obs_data_release(m);
}

static void release_chain_locked(struct source_record_filter *f)
{
	obs_source_t *parent = obs_filter_get_parent(f->source);
	if (f->fileOutput) {
		signal_handler_t *sh = obs_output_get_signal_handler(f->fileOutput);
		signal_handler_disconnect(sh, "stop", on_output_stopped, f);
		obs_output_release(f->fileOutput);
		f->fileOutput = NULL;
	}
	if (f->video_encoder) {
		obs_encoder_release(f->video_encoder);
		f->video_encoder = NULL;
	}
	if (f->audio_encoder) {
		obs_encoder_release(f->audio_encoder);
		f->audio_encoder = NULL;
	}
	close_isolated_audio(f, parent);
	if (f->view) {
		obs_view_set_source(f->view, 0, NULL);
		obs_view_remove(f->view);
		obs_view_destroy(f->view);
		f->view = NULL;
		f->video_output = NULL;
	}
}

static void on_output_stopped(void *data, calldata_t *cd)
{
	struct source_record_filter *f = data;
	long long code = calldata_int(cd, "code");
	pthread_mutex_lock(&f->mutex);
	f->last_error_code = (int)code;
	/* Never tear the output down from inside its own stop signal —
	 * defer to the graphics thread (video_tick). */
	f->need_teardown = true;
	if (code != OBS_OUTPUT_SUCCESS) {
		f->status = SR_STATUS_ERROR;
		obs_log(LOG_ERROR, "[instant-record] output stopped with code %lld", code);
		/* Disk full or transient error: try to keep rolling for
		 * ALWAYS / WITH_MAIN so we don't silently lose the event. */
		f->restart_after = (code == OBS_OUTPUT_NO_SPACE)
					   ? false
					   : (f->record_mode != SR_MODE_MANUAL);
	}
	pthread_mutex_unlock(&f->mutex);
}

/* Map scale_mode -> target max height (0 = native, never upscale). */
static uint32_t scale_cap_height(int mode)
{
	switch (mode) {
	case 1:
		return 1080;
	case 2:
		return 720;
	case 3:
		return 480;
	default:
		return 0;
	}
}

/* Build the whole chain with one specific encoder and start it. On any
 * failure it tears the chain back down and returns false, so the caller
 * can try a fallback encoder cleanly. Caller holds f->mutex. */
static bool sr_try_start(struct source_record_filter *f, obs_source_t *parent, const char *enc_id, uint32_t w,
			 uint32_t h, uint32_t sw, uint32_t sh, uint32_t fps_div, const char *filepath)
{
	f->view = obs_view_create();
	obs_view_set_source(f->view, 0, parent);

	struct obs_video_info ovi = {0};
	obs_get_video_info(&ovi);
	ovi.base_width = w;
	ovi.base_height = h;
	ovi.output_width = sw;
	ovi.output_height = sh;
	if (fps_div > 1)
		ovi.fps_den *= fps_div; /* halve/quarter the ISO frame rate */

	f->video_output = obs_view_add2(f->view, &ovi);
	if (!f->video_output) {
		obs_log(LOG_ERROR, "[instant-record] obs_view_add2 failed");
		release_chain_locked(f);
		return false;
	}

	obs_data_t *venc = obs_data_create();
	obs_data_set_int(venc, "bitrate", f->bitrate);
	f->video_encoder = obs_video_encoder_create(enc_id && *enc_id ? enc_id : "obs_x264", "instant_record_venc",
						    venc, NULL);
	obs_data_release(venc);
	obs_encoder_set_video(f->video_encoder, f->video_output);

	f->audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "instant_record_aenc", NULL, 0, NULL);
	if (f->isolate_audio && open_isolated_audio(f, parent))
		obs_encoder_set_audio(f->audio_encoder, f->audio_output);
	else
		obs_encoder_set_audio(f->audio_encoder, obs_get_audio()); /* rock-solid fallback */

	obs_data_t *outset = obs_data_create();
	if (f->use_buffer) {
		/* Replay buffer: keeps the last N seconds in memory; a clip is
		 * written on demand via the save hotkey / dock button. */
		struct dstr fmt;
		obs_source_t *par = obs_filter_get_parent(f->source);
		struct dstr nm;
		dstr_init_copy(&nm, par ? obs_source_get_name(par) : "source");
		sanitize(&nm);
		dstr_init(&fmt);
		dstr_printf(&fmt, "%s_%%CCYY-%%MM-%%DD_%%hh-%%mm-%%ss", nm.array);
		obs_data_set_string(outset, "directory", f->path && *f->path ? f->path : ".");
		obs_data_set_string(outset, "format", fmt.array);
		obs_data_set_string(outset, "extension", f->rec_format && *f->rec_format ? f->rec_format : "mkv");
		obs_data_set_int(outset, "max_time_sec", f->buffer_seconds > 0 ? f->buffer_seconds : 30);
		obs_data_set_int(outset, "max_size_mb", 0);
		f->fileOutput = obs_output_create("replay_buffer", "instant_record_buffer", outset, NULL);
		dstr_free(&fmt);
		dstr_free(&nm);
	} else {
		obs_data_set_string(outset, "path", filepath);
		f->fileOutput = obs_output_create("ffmpeg_muxer", "instant_record_output", outset, NULL);
	}
	obs_data_release(outset);

	obs_output_set_video_encoder(f->fileOutput, f->video_encoder);
	obs_output_set_audio_encoder(f->fileOutput, f->audio_encoder, 0);
	signal_handler_connect(obs_output_get_signal_handler(f->fileOutput), "stop", on_output_stopped, f);

	bool ok = obs_output_start(f->fileOutput);
	if (!ok) {
		const char *err = obs_output_get_last_error(f->fileOutput);
		obs_log(LOG_ERROR, "[instant-record] start failed with encoder '%s': %s", enc_id,
			err ? err : "(no detail)");
		release_chain_locked(f);
		return false;
	}

	f->active = true;
	f->status = SR_STATUS_RECORDING;
	f->width = w;
	f->height = h;
	f->last_error_code = OBS_OUTPUT_SUCCESS;
	f->start_timestamp_ns = os_gettime_ns();
	f->start_unix = (int64_t)time(NULL);
	f->out_w = sw;
	f->out_h = sh;
	f->used_fps_div = (int)fps_div;
	bfree(f->used_encoder);
	f->used_encoder = bstrdup(enc_id && *enc_id ? enc_id : "obs_x264");
	if (!f->use_buffer) {
		bfree(f->current_filepath);
		f->current_filepath = bstrdup(filepath);
		write_sidecar(f, false, 0); /* start entry — survives a crash */
	}
	obs_log(LOG_INFO, "[instant-record] recording %ux%u@1/%u via '%s' -> %s", sw, sh, fps_div, enc_id, filepath);
	return true;
}

static bool start_file_output_locked(struct source_record_filter *f)
{
	obs_source_t *parent = obs_filter_get_parent(f->source);
	if (!parent)
		return false;

	uint32_t w = obs_source_get_width(parent);
	uint32_t h = obs_source_get_height(parent);
	if (w == 0 || h == 0)
		return false;

	if (os_get_free_disk_space(f->path && *f->path ? f->path : ".") < SR_MIN_FREE_BYTES) {
		obs_log(LOG_ERROR, "[instant-record] refusing to start: low disk space");
		f->status = SR_STATUS_ERROR;
		return false;
	}

	/* Compute scaled output dimensions (downscale only, keep aspect). */
	uint32_t sw = w, sh = h;
	uint32_t cap = scale_cap_height(f->scale_mode);
	if (cap && h > cap) {
		sh = cap;
		sw = (uint32_t)((uint64_t)w * cap / h) & ~1u; /* keep even */
	}
	uint32_t fps_div = f->fps_divisor > 0 ? (uint32_t)f->fps_divisor : 1;

	struct dstr filepath;
	build_output_filepath(f, &filepath);

	/* Primary encoder. */
	bool ok = sr_try_start(f, parent, f->encoder_id, w, h, sw, sh, fps_div, filepath.array);

	/* Fallback: if the chosen (usually hardware) encoder couldn't get a
	 * session, drop to the fallback encoder so we still capture the ISO
	 * rather than losing it. Note this may be CPU-based (x264) — pair it
	 * with reduced resolution/fps to protect the live stream. */
	if (!ok && f->encoder_fallback) {
		const char *fb = f->fallback_encoder_id && *f->fallback_encoder_id ? f->fallback_encoder_id
										     : "obs_x264";
		obs_log(LOG_WARNING, "[instant-record] falling back to encoder '%s'", fb);
		ok = sr_try_start(f, parent, fb, w, h, sw, sh, fps_div, filepath.array);
	}

	if (!ok)
		f->status = SR_STATUS_ERROR;
	dstr_free(&filepath);
	return ok;
}

static void stop_file_output_locked(struct source_record_filter *f)
{
	if (f->fileOutput && f->active)
		obs_output_stop(f->fileOutput);
	f->active = false;
	if (f->current_filepath) {
		write_sidecar(f, true, os_gettime_ns()); /* final entry: stop + duration */
		bfree(f->current_filepath);
		f->current_filepath = NULL;
	}
	release_chain_locked(f);
	if (f->status == SR_STATUS_RECORDING)
		f->status = SR_STATUS_IDLE;
}

/* ================================================================== */
/* Frontend hook (SR_MODE_WITH_MAIN)                                  */
/* ================================================================== */

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>

static void frontend_event(enum obs_frontend_event event, void *data)
{
	struct source_record_filter *f = data;
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		pthread_mutex_lock(&f->mutex);
		if (f->active)
			stop_file_output_locked(f);
		pthread_mutex_unlock(&f->mutex);
		return;
	}
	if (f->record_mode != SR_MODE_WITH_MAIN)
		return;
	pthread_mutex_lock(&f->mutex);
	if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED && !f->active)
		f->want_start = true; /* staggered start via video_tick */
	else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
		f->want_start = false;
		if (f->active)
			stop_file_output_locked(f);
	}
	pthread_mutex_unlock(&f->mutex);
}
#endif

/* ================================================================== */
/* Hotkeys (SR_MODE_MANUAL)                                           */
/* ================================================================== */

static bool hotkey_start(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	struct source_record_filter *f = data;
	if (!pressed)
		return false;
	pthread_mutex_lock(&f->mutex);
	if (!f->active)
		f->want_start = true; /* gated start on next tick */
	pthread_mutex_unlock(&f->mutex);
	return true;
}

static bool hotkey_stop(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	struct source_record_filter *f = data;
	if (!pressed)
		return false;
	pthread_mutex_lock(&f->mutex);
	f->want_start = false;
	bool was = f->active;
	if (was)
		stop_file_output_locked(f);
	pthread_mutex_unlock(&f->mutex);
	return was;
}

/* ================================================================== */
/* Source info callbacks                                              */
/* ================================================================== */

static void sr_save_hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hk);
	if (!pressed)
		return;
	struct source_record_filter *f = data;
	pthread_mutex_lock(&f->mutex);
	sr_save_locked(f);
	pthread_mutex_unlock(&f->mutex);
}

static const char *sr_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("InstantRecord.Filter");
}

static void sr_update(void *data, obs_data_t *settings)
{
	struct source_record_filter *f = data;
	pthread_mutex_lock(&f->mutex);
	f->record_mode = (enum sr_record_mode)obs_data_get_int(settings, "record_mode");
	f->bitrate = obs_data_get_int(settings, "bitrate");
	f->isolate_audio = obs_data_get_bool(settings, "isolate_audio");
	f->scale_mode = (int)obs_data_get_int(settings, "scale_mode");
	f->fps_divisor = (int)obs_data_get_int(settings, "fps_divisor");
	f->encoder_fallback = obs_data_get_bool(settings, "encoder_fallback");
	f->use_buffer = obs_data_get_bool(settings, "use_buffer");
	f->buffer_seconds = (int)obs_data_get_int(settings, "buffer_seconds");
	bfree(f->fallback_encoder_id);
	f->fallback_encoder_id = bstrdup(obs_data_get_string(settings, "fallback_encoder"));
	bfree(f->path);
	f->path = bstrdup(obs_data_get_string(settings, "path"));
	bfree(f->rec_format);
	f->rec_format = bstrdup(obs_data_get_string(settings, "rec_format"));
	bfree(f->encoder_id);
	f->encoder_id = bstrdup(obs_data_get_string(settings, "encoder"));
	pthread_mutex_unlock(&f->mutex);
}

static void *sr_create(obs_data_t *settings, obs_source_t *source)
{
	struct source_record_filter *f = bzalloc(sizeof(struct source_record_filter));
	f->source = source;
	f->status = SR_STATUS_IDLE;
	f->record_hotkey_pair = OBS_INVALID_HOTKEY_PAIR_ID;
	f->save_hotkey = OBS_INVALID_HOTKEY_ID;
	pthread_mutex_init(&f->mutex, NULL);
	sr_update(f, settings);

	f->record_hotkey_pair = obs_hotkey_pair_register_source(
		source, "InstantRecord.Start", obs_module_text("InstantRecord.Start"), "InstantRecord.Stop",
		obs_module_text("InstantRecord.Stop"), hotkey_start, hotkey_stop, f, f);

	f->save_hotkey = obs_hotkey_register_source(source, "InstantRecord.SaveReplay",
						    obs_module_text("InstantRecord.SaveReplay"), sr_save_hotkey, f);

#ifdef ENABLE_FRONTEND_API
	obs_frontend_add_event_callback(frontend_event, f);
#endif
	registry_add(f);
	return f;
}

static void sr_destroy(void *data)
{
	struct source_record_filter *f = data;
	registry_remove(f);
#ifdef ENABLE_FRONTEND_API
	obs_frontend_remove_event_callback(frontend_event, f);
#endif
	if (f->record_hotkey_pair != OBS_INVALID_HOTKEY_PAIR_ID)
		obs_hotkey_pair_unregister(f->record_hotkey_pair);
	if (f->save_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(f->save_hotkey);

	pthread_mutex_lock(&f->mutex);
	stop_file_output_locked(f);
	pthread_mutex_unlock(&f->mutex);

	pthread_mutex_destroy(&f->mutex);
	bfree(f->path);
	bfree(f->rec_format);
	bfree(f->encoder_id);
	bfree(f->fallback_encoder_id);
	bfree(f->used_encoder);
	bfree(f->current_filepath);
	bfree(f);
}

static void sr_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct source_record_filter *f = data;
	obs_source_t *parent = obs_filter_get_parent(f->source);

	pthread_mutex_lock(&f->mutex);

	/* Deferred teardown requested by the output stop signal. */
	if (f->need_teardown) {
		f->need_teardown = false;
		bool restart = f->restart_after;
		f->restart_after = false;
		stop_file_output_locked(f);
		if (restart && parent)
			start_file_output_locked(f);
		pthread_mutex_unlock(&f->mutex);
		return;
	}

	if (!parent) {
		pthread_mutex_unlock(&f->mutex);
		return;
	}

	/* SR_MODE_ALWAYS: request a start as soon as geometry is valid. */
	if (f->record_mode == SR_MODE_ALWAYS && !f->active && !f->want_start) {
		if (obs_source_get_width(parent) && obs_source_get_height(parent))
			f->want_start = true;
	}

	/* Pending (possibly batch) start — honor the global spacing so a
	 * whole rig doesn't spin up in one spike. If the gate isn't ready,
	 * keep want_start and retry next tick (that's the stagger). */
	if (f->want_start && !f->active) {
		if (obs_source_get_width(parent) && obs_source_get_height(parent) && sr_start_gate_take()) {
			f->want_start = false;
			start_file_output_locked(f);
		}
		pthread_mutex_unlock(&f->mutex);
		return;
	}

	if (!f->active) {
		pthread_mutex_unlock(&f->mutex);
		return;
	}

	/* Resolution change → cleanly cycle instead of crashing (#166). */
	uint32_t w = obs_source_get_width(parent);
	uint32_t h = obs_source_get_height(parent);
	if ((w && h) && (w != f->width || h != f->height)) {
		obs_log(LOG_INFO, "[instant-record] resolution %ux%u -> %ux%u, cycling", f->width, f->height, w,
			h);
		enum sr_record_mode mode = f->record_mode;
		stop_file_output_locked(f);
		if (mode == SR_MODE_ALWAYS || mode == SR_MODE_WITH_MAIN)
			start_file_output_locked(f);
	}

	pthread_mutex_unlock(&f->mutex);
}

static void sr_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct source_record_filter *f = data;
	/* Transparent: capture runs through the isolated view, not here. */
	obs_source_skip_video_filter(f->source);
}

static void sr_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, "record_mode", SR_MODE_WITH_MAIN);
	obs_data_set_default_int(s, "bitrate", 6000);
	obs_data_set_default_string(s, "rec_format", "mkv");
	obs_data_set_default_string(s, "encoder", "obs_x264");
	obs_data_set_default_bool(s, "isolate_audio", true);
	obs_data_set_default_int(s, "scale_mode", 0);
	obs_data_set_default_int(s, "fps_divisor", 1);
	obs_data_set_default_bool(s, "encoder_fallback", true);
	obs_data_set_default_string(s, "fallback_encoder", "obs_x264");
	obs_data_set_default_bool(s, "use_buffer", false);
	obs_data_set_default_int(s, "buffer_seconds", 30);
}

static obs_properties_t *sr_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();

	obs_property_t *mode = obs_properties_add_list(p, "record_mode", obs_module_text("InstantRecord.Mode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("InstantRecord.Mode.WithMain"), SR_MODE_WITH_MAIN);
	obs_property_list_add_int(mode, obs_module_text("InstantRecord.Mode.Manual"), SR_MODE_MANUAL);
	obs_property_list_add_int(mode, obs_module_text("InstantRecord.Mode.Always"), SR_MODE_ALWAYS);

	obs_properties_add_path(p, "path", obs_module_text("InstantRecord.Path"), OBS_PATH_DIRECTORY, NULL, NULL);

	obs_property_t *fmt = obs_properties_add_list(p, "rec_format", obs_module_text("InstantRecord.Format"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(fmt, "mkv", "mkv");
	obs_property_list_add_string(fmt, "mp4", "mp4");
	obs_property_list_add_string(fmt, "mov", "mov");

	obs_property_t *enc = obs_properties_add_list(p, "encoder", obs_module_text("InstantRecord.Encoder"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	const char *enc_id;
	size_t i = 0;
	while (obs_enum_encoder_types(i++, &enc_id)) {
		if (obs_get_encoder_type(enc_id) != OBS_ENCODER_VIDEO)
			continue;
		const char *nm = obs_encoder_get_display_name(enc_id);
		obs_property_list_add_string(enc, nm ? nm : enc_id, enc_id);
	}

	obs_properties_add_int(p, "bitrate", obs_module_text("InstantRecord.Bitrate"), 500, 100000, 500);
	obs_properties_add_bool(p, "isolate_audio", obs_module_text("InstantRecord.IsolateAudio"));

	/* Replay buffer: keep last N seconds, save clips on demand. */
	obs_properties_add_bool(p, "use_buffer", obs_module_text("InstantRecord.Buffer"));
	obs_properties_add_int(p, "buffer_seconds", obs_module_text("InstantRecord.BufferSecs"), 5, 600, 5);

	/* Cost controls — cut encoding load on backup cameras. */
	obs_property_t *scale = obs_properties_add_list(p, "scale_mode", obs_module_text("InstantRecord.Scale"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(scale, obs_module_text("InstantRecord.Scale.Native"), 0);
	obs_property_list_add_int(scale, "1080p", 1);
	obs_property_list_add_int(scale, "720p", 2);
	obs_property_list_add_int(scale, "480p", 3);

	obs_property_t *fps = obs_properties_add_list(p, "fps_divisor", obs_module_text("InstantRecord.Fps"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fps, obs_module_text("InstantRecord.Fps.Full"), 1);
	obs_property_list_add_int(fps, obs_module_text("InstantRecord.Fps.Half"), 2);
	obs_property_list_add_int(fps, obs_module_text("InstantRecord.Fps.Quarter"), 4);

	/* Safety net — fall back to another encoder if the primary can't get
	 * a session at start (e.g. hardware encoder sessions exhausted). */
	obs_properties_add_bool(p, "encoder_fallback", obs_module_text("InstantRecord.Fallback"));
	obs_property_t *fb = obs_properties_add_list(p, "fallback_encoder",
						     obs_module_text("InstantRecord.FallbackEncoder"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	size_t fi = 0;
	const char *fb_id;
	while (obs_enum_encoder_types(fi++, &fb_id)) {
		if (obs_get_encoder_type(fb_id) != OBS_ENCODER_VIDEO)
			continue;
		const char *nm = obs_encoder_get_display_name(fb_id);
		obs_property_list_add_string(fb, nm ? nm : fb_id, fb_id);
	}
	return p;
}

struct obs_source_info source_record_filter_info = {
	.id = "instant_record_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = sr_get_name,
	.create = sr_create,
	.destroy = sr_destroy,
	.update = sr_update,
	.get_defaults = sr_defaults,
	.get_properties = sr_properties,
	.video_tick = sr_video_tick,
	.video_render = sr_video_render,
};
