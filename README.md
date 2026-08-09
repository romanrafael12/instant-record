# Instant Record

A clean-room OBS Studio plugin for per-source (ISO) recording, built from
scratch on the official `obs-plugintemplate`. Each source that carries the
**Instant Record (Source)** filter records to its own file, in parallel.

## Status — v0.5.0 (sync sidecars)

API-checked against real OBS 31.1.1 headers and the working upstream
source-record plugin: view creation, isolated audio_output, source audio
capture callback, output stop signal, frontend dock, and disk-space query
all match. Still needs a local compile (no libobs toolchain in the build
environment used to author it).

Implemented:

- Isolated per-source render + encode + mux to file (`ffmpeg_muxer`)
- Three trigger modes: with OBS main recording, manual (hotkey), or always
- Selectable container (mkv/mp4/mov), video encoder, bitrate
- **Resolution-change handling** — cleanly cycles the output instead of
  crashing (fixes the class of bug behind upstream source-record #166)

Stability layer (this pass):

1. **Thread safety** — all shared state guarded by a per-filter mutex;
   graphics thread, frontend callback, hotkeys and UI no longer race.
2. **Output failure detection** — connects the output `stop` signal, reads
   the error code, and defers teardown to the graphics thread (never tears
   down from inside the signal). Auto-restarts on transient errors for
   ALWAYS / WITH_MAIN; stops on `NO_SPACE`.
3. **Clean shutdown** — a registry of live recorders is finalized on
   `OBS_FRONTEND_EVENT_EXIT` and in `obs_module_unload`, so files close.
4. **Collision-proof filenames** — source names are sanitized (Windows
   reserved chars, spaces, control chars) and get a per-filter index, so
   two cameras or two starts in the same second never overwrite.
5. **Parent lifetime safety** — every use of the parent source is
   NULL-guarded; teardown is locked.
6. **Disk-space pre-check** — refuses to start below 512 MB free.

Isolated per-source audio (`isolate_audio`, on by default):
- Captures the source's own audio via a capture callback into a ring
  buffer, fed to a dedicated `audio_output`.
- ⚠️ **Verify first.** This build bridges the capture callback to the
  audio_output with a ring buffer and stamps output with start_ts — which
  can drift over long recordings. The proven fix (per upstream
  source-record) is to pull audio inside the input callback using the
  source's *real* timestamps (obs_source_enum_active_tree / mix). Planned.
  Meanwhile, if you see A/V drift, turn the option off — it falls back to
  the global audio mix, which is rock-solid.

Status dock (Qt) — attaches as a normal OBS panel:

- One table of **every** Instant Record source: name, resolution/container,
  live status (REC / idle / error), and elapsed time. Refreshes ~2x/sec
  from the thread-safe registry snapshot.
- **Start all / Stop all** buttons — run the whole rig from one place.
- **Global config** dialog — set output folder, container, encoder,
  bitrate, trigger mode and per-source audio once, then *Apply to all
  cameras*. Writes each filter's persisted settings, so it survives
  save/reload and shows in every filter's own UI. This is the main guard
  against per-camera human error when configuring a multi-cam rig.

Load / resilience controls (this pass):

- **Per-camera resolution cap** (native / 1080p / 720p / 480p) and
  **frame-rate divisor** (full / half / quarter). Downscale-only, aspect
  preserved. Cuts encoding cost on backup cams so the live stream keeps
  its encoder headroom. Settable per camera or pushed to all from the
  dock's global config.
- **Encoder auto-fallback**: if the primary (usually hardware) encoder
  can't get a session at start, the ISO falls back to a chosen encoder
  instead of failing outright. Start-time only — no mid-recording swap
  (that would split the file). If the fallback is x264 it uses CPU, so
  pair it with the resolution/fps caps above.

- **Staggered start**: when a whole rig starts at once (Start-all, or
  Record-with-main), cameras spin up ~300 ms apart via a global gate
  instead of all in the same instant — smooths the encoder load spike at
  go-live. Single manual starts are effectively immediate.

Note: nothing here touches the main stream. The stream is never
throttled or swapped by this plugin — watch OBS's dropped-frames /
encoding-lag stats and reduce ISO quality or count if they climb.

Sync sidecars (this pass) — the multicam alignment mechanism:

- Every clip gets a `<clip>.json` written next to it, with the source
  name, encoder, output dimensions, and — critically — a precise
  **start_monotonic_ns** and **stop_monotonic_ns / duration_ms**.
- Cameras do NOT start at the same instant (staggered by design), but all
  share one monotonic clock, so the *difference* between two clips'
  start_monotonic_ns is their exact offset — align on that in post,
  sub-millisecond. The second-resolution filename is not the sync source;
  the sidecar is.
- Written at start (survives a crash) and rewritten at stop with duration.
- Video-to-video drift over a long recording is negligible: every ISO is
  driven by the same OBS video clock.
- **Single-machine only** for now. Across multiple PCs the monotonic
  clocks differ — you'd need shared timecode or NTP-disciplined stamps.
- Audio isolation drift (separate issue) still to verify; see above.

## Roadmap

- `manifest.json` with synchronized start/stop timestamps for multicam ISO
- Per-source replay buffer
- Tighten isolated-audio timestamps to eliminate long-run drift

## Build

```bash
# macOS (with your existing Developer ID signing env)
cmake --preset macos && cmake --build --preset macos
# Windows
cmake --preset windows-x64 && cmake --build --preset windows-x64
```

> Not yet compiled against libobs here — expect a compile pass with small
> fixes on first local build. Run a full-length stress test (all cameras,
> whole-event duration) before trusting it on a real broadcast.

## License

GPL-2.0 (required for libobs-linked plugins).

## Post: align_multicam.py

`tools/align_multicam.py` reads the `.json` sidecars from a shoot, prints
the exact per-camera offsets (ms + frames), and writes an FCPXML timeline
with each camera on its own lane at its offset — import into DaVinci
Resolve / Premiere / Final Cut. The offsets are exact; FCPXML import may
need a small nudge (fps/path). Single machine only.

```
python3 tools/align_multicam.py /path/to/recordings --fps 60 -o synced.fcpxml
```
