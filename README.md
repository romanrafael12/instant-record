# Instant Record

**Per-source (ISO) multi-camera recording &amp; replay for OBS Studio (Windows)**  
by [Instant Replay](https://instanrp.com)

---

Instant Record records **each camera to its own file** and keeps a **per-camera replay buffer**, so you can grab a clip of every angle the instant something happens. Built for live multi-camera work by **[Instant Replay](https://instanrp.com)**.

## Download

[![Latest release](https://img.shields.io/github/v/release/romanrafael12/instant-record?label=latest&color=e0403a)](https://github.com/romanrafael12/instant-record/releases/latest)

**➡ [Download the Windows installer](https://github.com/romanrafael12/instant-record/releases/latest/download/InstantRecord-Setup.exe)** — run it and it installs the plugin into OBS for you.

Prefer the manual zip, or looking for other versions? See the **[Releases page](https://github.com/romanrafael12/instant-record/releases/latest)**.

## Screenshots

<p align="center">
  <img src="https://github.com/user-attachments/assets/13895466-3261-464e-accd-d8e209055455" width="70%" alt="Instant Record dock — one card per camera with replay buffer"/>
  <br/><em>The dock — one card per camera, with a per-camera replay buffer.</em>
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/776bd587-a491-4130-83de-01d20daa0d7c" width="70%" alt="Global config — apply to all cameras"/>
  <br/><em>Global config — set encoder, container, resolution and buffer once, apply to every camera.</em>
</p>

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

1. **Add cameras — the fastest way:** drag and drop a source from OBS's Sources list onto the dock's box. **The filter is added automatically after you drop it** — no need to open Filters. (You can also use the **＋** button to pick sources, or add the **Instant Record (Source)** filter manually.) Use the **✕** on a card to remove it.
2. Open **Global config** in the dock and set it for all cameras at once:
   - **Output folder** — where clips are saved.
   - **Trigger** — *with OBS main recording*, *manual (hotkey)*, or *always*. Or turn on **Replay buffer mode** to save clips on demand.
   - **Encoder / bitrate / container** (see below).
3. Run it from the dock: **Start all / Stop all / Save buffer**. For hotkeys, open **OBS Settings → Hotkeys** and search "Instant" — assign *Save all clips*, *Start all*, and *Stop all*.

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
