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

#pragma once

#include <obs-module.h>
#include <media-io/audio-io.h>
#include <pthread.h>

/* Record trigger modes for a source-record filter instance. */
enum sr_record_mode {
	SR_MODE_WITH_MAIN, /* start/stop follows OBS main recording      */
	SR_MODE_MANUAL,    /* start/stop only via the filter's hotkeys   */
	SR_MODE_ALWAYS,    /* records whenever the parent source is live */
};

/* Coarse status for the (future) status dock / registry. */
enum sr_status {
	SR_STATUS_IDLE,
	SR_STATUS_RECORDING,
	SR_STATUS_ERROR,
};

/* Lock-protected circular buffer bridging the source audio capture
 * callback (push) to the custom audio_output (pull). */
struct sr_audio_ring {
	float *data[MAX_AUDIO_CHANNELS];
	size_t capacity_frames;
	size_t write_pos;
	size_t read_pos;
	size_t available;
	uint32_t channels;
	uint32_t sample_rate;
	pthread_mutex_t lock;
	bool ready;
};

struct source_record_filter {
	obs_source_t *source; /* the filter itself */

	/* --- State below is guarded by `mutex`. --- */
	pthread_mutex_t mutex;

	/* Isolated video render path for the parent source. */
	obs_view_t *view;
	video_t *video_output;

	/* Isolated audio path for the parent source. */
	audio_t *audio_output;
	struct sr_audio_ring audio_ring;
	bool audio_cb_active;

	/* Encoding / muxing chain. */
	obs_encoder_t *video_encoder;
	obs_encoder_t *audio_encoder;
	obs_output_t *fileOutput;

	/* Current capture geometry — detects resolution changes
	 * (the root cause of upstream crash issue #166). */
	uint32_t width;
	uint32_t height;

	/* Settings (owned copies). */
	enum sr_record_mode record_mode;
	char *path;
	char *rec_format;
	char *encoder_id;
	long long bitrate;
	bool isolate_audio;
	int scale_mode;   /* 0 native, 1 =1080p, 2 =720p, 3 =480p cap */
	int fps_divisor;  /* 1 full, 2 half, 4 quarter                 */
	bool encoder_fallback;    /* try fallback encoder on start fail */
	char *fallback_encoder_id;
	bool use_buffer;   /* replay-buffer mode instead of continuous  */
	int buffer_seconds;/* how many seconds to keep buffered         */

	/* Runtime state. */
	enum sr_status status;
	bool active;         /* an output is currently running           */
	bool want_start;     /* a start is requested; gated + staggered   */
	bool need_teardown;  /* deferred cleanup requested from a signal  */
	bool restart_after;  /* after teardown, immediately restart       */
	int last_error_code; /* last obs_output stop code                 */
	int file_index;      /* monotonic per-filter, unique filenames    */

	/* Hotkeys. */
	obs_hotkey_pair_id record_hotkey_pair;
	obs_hotkey_id save_hotkey; /* save a replay clip (buffer mode)  */

	/* Roadmap: manifest.json entry for ISO multicam sync. */
	int64_t start_timestamp_ns; /* monotonic — precise inter-cam offset */
	int64_t start_unix;         /* wall clock (seconds) for reference   */
	char *current_filepath;     /* active clip, for the sidecar         */
	char *used_encoder;         /* encoder actually used (after fallback)*/
	uint32_t out_w, out_h;      /* scaled output dimensions             */
	int used_fps_div;
};

extern struct obs_source_info source_record_filter_info;

#ifdef __cplusplus
extern "C" {
#endif

/* Registry — stops every active recorder on shutdown, drives the dock. */
void sr_registry_stop_all(void);
void sr_registry_start_all(void);
void sr_registry_save_all(void); /* save a clip from every buffered camera */

/* Thread-safe snapshot of one recorder, for the status dock. */
struct sr_status_row {
	char name[256];
	char format[16];
	enum sr_status status;
	uint32_t width;
	uint32_t height;
	int64_t elapsed_ns; /* since start, if recording; else 0 */
	int last_error_code;
	bool use_buffer;    /* true if this camera is in replay-buffer mode */
};

/* Copies up to max_rows snapshots into `rows`; returns the count. */
size_t sr_registry_snapshot(struct sr_status_row *rows, size_t max_rows);

/* Save a clip from a single camera by its snapshot index. */
void sr_registry_save_index(size_t index);

/* Apply shared settings to every recorder at once. A NULL string or a
 * negative number means "leave that field unchanged" — so the dock can
 * push just the fields the operator edited. */
struct sr_global_config {
	const char *path;
	const char *rec_format;
	const char *encoder_id;
	long long bitrate;   /* <= 0 = unchanged */
	int record_mode;     /* < 0 = unchanged  */
	int isolate_audio;   /* < 0 = unchanged, else 0/1 */
	int scale_mode;      /* < 0 = unchanged  */
	int fps_divisor;     /* <= 0 = unchanged */
	int use_buffer;      /* < 0 = unchanged, else 0/1 */
	int buffer_seconds;  /* <= 0 = unchanged */
};
void sr_registry_apply_config(const struct sr_global_config *cfg);

/* Registers the Qt status dock with the OBS frontend. Implemented in
 * source-record-dock.cpp; no-op if built without Qt/frontend. */
void instant_record_register_dock(void);

#ifdef __cplusplus
}
#endif
