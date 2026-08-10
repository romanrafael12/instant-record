

https://github.com/user-attachments/assets/3016b7fb-eaf1-4494-9466-2c09c02506f9





<p align="center"><sub>From the makers of <a href="https://instanrp.com"><b>Instant Replay</b></a></sub></p>

<h1 align="center">Instant Record</h1>

<p align="center"><b>Per-source (ISO) multi-camera recording &amp; replay for OBS Studio (Windows)</b></p>

---

Instant Record gives **each camera its own recording** — either a continuous ISO file per camera, or a **per-camera replay buffer** so you can grab a clip of every angle the instant something happens. It can also record a **Clean Program**: the switch between your cameras in one continuous file, with **no graphics** burned in. Built for live multi-camera work (sports, worship, events).

<div align="center">

## ⬇ Download

<a href="https://github.com/romanrafael12/instant-record/releases/latest/download/InstantRecord-Setup.exe">
  <img src="https://img.shields.io/badge/Download-Windows%20Installer-e0403a?style=for-the-badge&logo=windows&logoColor=white" alt="Download Windows Installer"/>
</a>

[![Latest release](https://img.shields.io/github/v/release/romanrafael12/instant-record?label=version&color=f5c04a&style=flat-square)](https://github.com/romanrafael12/instant-record/releases/latest)

Downloads the installer directly — run it and it sets up the plugin in OBS for you.  
Other versions and the manual zip are on the **[Releases page](https://github.com/romanrafael12/instant-record/releases/latest)**.

</div>

## Screenshots

<p align="center">
  <img width="263" height="311" alt="Instant Record dock — one card per camera with replay buffer" src="https://github.com/user-attachments/assets/6ac442b0-f0d1-49cf-9753-04853889d993"/>
  <img width="263" height="311" alt="Config — apply to all cameras" src="https://github.com/user-attachments/assets/a993443d-81c0-44df-91cb-c529da3ae7f1"/>
  <br/><em>The dock and the config — one card per camera, set everything once for all cameras.</em>
</p>

<p align="center">
  <img width="276" height="55" alt="Config bar" src="https://github.com/user-attachments/assets/2ee87cf9-8077-499a-a0f0-f21c06ae1e82"/>
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
   - **Recording mode** — pick one: **Replay buffer mode** (keeps the last N seconds in RAM; save a clip on demand) **or** continuous recording (each camera records to its own file). Use one mode at a time.
   - **Trigger** (for continuous recording) — *with OBS main recording*, *manual (hotkey)*, or *always*.
   - **Encoder / bitrate / container** (see below).
   - **Clean Program folder / format** — where the clean-program file is saved and its container (defaults to Hybrid MP4).
   - **Sync `.json` files** — on by default (one per clip, with precise start/stop timestamps to align cameras in post). Uncheck it if you don't want them.
3. Run it from the dock: **Start all / Stop all / Save buffer**. For hotkeys, open **OBS Settings → Hotkeys** and search "Instant".

## Clean Program

**Clean Program** records the switch between your cameras — the "program" — into **one continuous file with no graphics** (no lower-thirds, no overlays). As you cut between scenes, it follows the camera of each scene automatically; the file never splits and never shows your on-air graphics.

- Press **Clean Program** in the dock (or bind the *Instant Record: Clean Program* hotkey). A card appears at the top showing the elapsed time and **which camera it's recording right now**, and the on-air camera is highlighted in the list. Press **Stop clean** to finish.
- It follows the camera by looking inside each scene for the source that carries the Instant Record filter, so the cameras you already added just work. On scenes with no clear camera (PiP, replay, playlists) it keeps the last camera, so the file never goes black.
- Each camera fills the whole frame (the clean camera feed), regardless of how it's scaled or placed in your scene.
- Only one recording mode runs at a time per camera; Clean Program is its own separate recording and can run alongside your per-camera setup.

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

## ☕ Support the project

Instant Record is free and open source. If it saves you time, you can [**buy me a coffee**](https://donate.stripe.com/28EaEW1Dk19c8uB5Kk7wA00) — every bit helps.

<p align="center">
  <a href="https://donate.stripe.com/28EaEW1Dk19c8uB5Kk7wA00">
    <img src="https://img.shields.io/badge/%E2%98%95%20Buy%20me%20a%20coffee-f5c04a?style=for-the-badge&logoColor=black" alt="Buy me a coffee"/>
  </a>
</p>

---

<p align="center"><sub>Brought to you by</sub></p>
<p align="center">
  <a href="https://instanrp.com">
    <img width="160" alt="Instant Replay" src="data/locale/insta.png"/>
  </a>
</p>
<p align="center"><a href="https://instanrp.com"><b>instanrp.com</b></a></p>

## License

GPL-2.0 (required for libobs-linked plugins).
