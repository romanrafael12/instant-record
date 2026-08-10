# Instant Record

**Per-source (ISO) multi-camera recording & replay for OBS Studio (Windows)**

---

Instant Record records **each camera to its own file** and keeps a **per-camera replay buffer**, so you can grab a clip of every angle the instant something happens. Built for live multi-camera work (sports, worship, events).

<div align="center">

## ⬇ Download

<a href="https://github.com/romanrafael12/instant-record/releases/latest">
  <img src="https://img.shields.io/badge/Download-on%20GitHub-e0403a?style=for-the-badge&logo=github&logoColor=white" alt="Download on GitHub"/>
</a>

[![Latest release](https://img.shields.io/github/v/release/romanrafael12/instant-record?label=version&color=f5c04a&style=flat-square)](https://github.com/romanrafael12/instant-record/releases/latest)

Get the installer or the manual zip from GitHub Releases.  
Other versions are on the **[Releases page](https://github.com/romanrafael12/instant-record/releases/latest)**.

### ☕ Support the project

If Instant Record saves you time, you can [**buy me a coffee**](https://donate.stripe.com/28EaEW1Dk19c8uB5Kk7wA00) — it's free and open source, and every bit helps.

<a href="https://donate.stripe.com/28EaEW1Dk19c8uB5Kk7wA00">
  <img src="https://img.shields.io/badge/%E2%98%95%20Buy%20me%20a%20coffee-f5c04a?style=for-the-badge&logoColor=black" alt="Buy me a coffee"/>
</a>

</div>

## Screenshots

<p align="center">
  <img src="https://github.com/user-attachments/assets/13895466-3261-464e-accd-d8e209055455" width="70%" alt="Instant Record dock — one card per camera with replay buffer"/>
  <br/><em>The dock — one card per camera, with a per-camera replay buffer.</em>
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/776bd587-a491-4130-83de-01d20daa0d7c" width="70%" alt="Config — apply to all cameras"/>
  <br/><em>Config — set encoder, container, resolution and buffer once, apply to every camera.</em>
</p>

## Install (Windows)

1. Download the latest **`InstantRecord-Setup.exe`** from the [Releases page](https://github.com/romanrafael12/instant-record/releases/latest), or build it yourself from the [Actions](https://github.com/romanrafael12/instant-record/actions) tab.
2. **Close OBS.**
3. Run the installer (or, for the manual zip, copy the `bin` and `data` folders into `C:\ProgramData\obs-studio\plugins\instant-record\`).
4. Open OBS. You'll see the **Instant Record** dock, and the **Instant Record (Source)** filter on any source's Filters.

## Configure

1. **Add cameras — the fastest way:** drag and drop a source from OBS's Sources list onto the dock. **The filter is added automatically after you drop it** — no need to open Filters. (You can also use the **＋** button, or add the **Instant Record (Source)** filter manually.) Use the **✕** on a card to remove it.
2. Open **Config** in the dock and set it for all cameras at once:
   - **Output folder** — where clips are saved.
   - **Trigger** — *with OBS main recording*, *manual (hotkey)*, or *always*. Or turn on **Replay buffer mode** to save clips on demand.
   - **Encoder / bitrate / container** (see below).
3. Run it from the dock: **Start all / Stop all / Save buffer**. For hotkeys, open **OBS Settings → Hotkeys** and search "Instant".

## Encoders that work

- **Auto (recommended)** — picks the best hardware encoder on your PC (NVENC → AMF → QuickSync → x264) and avoids the one your stream is using when possible.
- **NVIDIA NVENC H.264** — hardware encoding, barely touches the CPU, edits cleanly everywhere.
- **x264** — good fallback (uses CPU).
- Avoid **SVT-AV1** for live recording — it's software AV1 and spikes the CPU.
- **Container:** mkv (crash-safe) or **Hybrid MP4** (crash-safe *and* editable without remux). Backup cameras: cap at **720p** / **half** frame rate.

## Remote control (optional)

Instant Record registers an **obs-websocket** vendor (`instant-record`) so an app or Stream Deck can trigger **SaveAll / StartAll / StopAll / SaveCamera / GetStatus** via `CallVendorRequest`. Requires obs-websocket (built into OBS 28+).

## Support

Questions, bugs, or feature requests → open an [issue on GitHub](https://github.com/romanrafael12/instant-record/issues).

## License

GPL-2.0 (required for libobs-linked plugins).
