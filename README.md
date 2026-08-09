
<h1 align="center">Instant Record</h1>
<p align="center"><b>Per-source (ISO) multi-camera recording &amp; replay for OBS Studio (Windows)</b><br/>
by <a href="https://instanrp.com">Instant Replay</a></p>

---

Instant Record records **each camera to its own file** and keeps a **per-camera replay buffer**, so you can grab a clip of every angle the instant something happens. Built for live multi-camera work by **[Instant Replay](https://instanrp.com)**.

## Install (Windows)

1. Download the latest **`instant-record-windows-x64`** from the [Actions](https://github.com/romanrafael12/instant-record/actions) tab → newest green run → **Artifacts**.
2. **Close OBS.**
3. Unzip and copy the `bin` and `data` folders into:
   ```
   C:\ProgramData\obs-studio\plugins\instant-record\
   ```
   (copy **both** folders from the same zip — replace what's there).
4. Open OBS. You'll see the **Instant Record** dock, and the **Instant Record (Source)** filter on any source's Filters.

## Configure

1. Add cameras: in the dock, click **＋ Add cameras** and pick your sources (or add the **Instant Record (Source)** filter manually).
2. Open **Global config** in the dock and set it for all cameras at once:
   - **Output folder** — where clips are saved.
   - **Trigger** — *with OBS main recording*, *manual (hotkey)*, or *always*. Or turn on **Replay buffer mode** to save clips on demand.
   - **Encoder / bitrate / container** (see below).
3. Run it from the dock: **Start all / Stop all / Save all clips**, or per-camera hotkeys (assign them in **OBS Settings → Hotkeys**, search "Instant").

## Encoders that work

- **NVIDIA NVENC H.264** — recommended. Hardware encoding, barely touches the CPU, edits cleanly everywhere.
- **x264** — good fallback (uses CPU); set it as the auto-fallback encoder.
- Avoid **SVT-AV1** for live recording — it's software AV1 and spikes the CPU.
- **Container: mkv** — survives a crash without corrupting the file.
- Backup cameras: cap at **720p** / **half** frame rate to save encoding headroom.

## About Instant Replay

Made by **[Instant Replay](https://instanrp.com)** — a live sports broadcast replay system built on OBS.

**Contact / support:** support@instanrp.com

## License

GPL-2.0 (required for libobs-linked plugins).
