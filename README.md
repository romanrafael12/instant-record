<h1 align="center">Instant Record</h1>
<p align="center"><b>Per-source (ISO) multi-camera recording &amp; replay for OBS Studio (Windows)</b><br/>
by <a href="https://instanrp.com">Instant Replay</a></p>

---

Instant Record records **each camera to its own file** and keeps a **per-camera replay buffer**, so you can grab a clip of every angle the instant something happens. Built for live multi-camera work by **[Instant Replay](https://instanrp.com)**.

## Screenshots

<!-- Drag your screenshots right here while editing this file on GitHub — it uploads and links them automatically. -->
<!-- Or commit images to a screenshots/ folder and use: <p align="center"><img src="screenshots/dock.png" width="600"/></p> -->

_The dock and a source's filter — add your screenshots here._

## Install (Windows)

1. Download the latest **`instant-record-windows-x64`** from the [Actions](https://github.com/romanrafael12/instant-record/actions) tab → newest green run → **Artifacts**. (Or grab the **`InstantRecord-Setup.exe`** installer from the `instant-record-installer` artifact and skip the manual copy.)
2. **Close OBS.**
3. Unzip and copy the `bin` and `data` folders into:
   ```
   C:\ProgramData\obs-studio\plugins\instant-record\
   ```
   (copy **both** folders from the same zip — replace what's there).
4. Open OBS. You'll see the **Instant Record** dock, and the **Instant Record (Source)** filter on any source's Filters.

## Configure

1. Add cameras: **drag a source** from OBS's Sources list onto the dock, or click **＋ Add cameras** and pick them (the filter is added automatically). Use the **✕** on a card to remove it.
2. Open **Global config** in the dock and set it for all cameras at once:
   - **Output folder** — where clips are saved.
   - **Trigger** — *with OBS main recording*, *manual (hotkey)*, or *always*. Or turn on **Replay buffer mode** to save clips on demand.
   - **Encoder / bitrate / container** (see below).
3. Run it from the dock: **Start all / Stop all / Save all clips**. For hotkeys, open **OBS Settings → Hotkeys** and search "Instant" — assign *Save all clips*, *Start all*, and *Stop all*.

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
