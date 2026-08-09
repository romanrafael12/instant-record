#!/usr/bin/env python3
"""
align_multicam.py — align Instant Record ISO clips in post.

Reads the <clip>.json sidecars that Instant Record writes next to every
recording, computes each camera's exact offset from the earliest start
(using the shared monotonic clock), and emits:

  1. A sync report (offset in ms and frames) to stdout.
  2. An FCPXML timeline (DaVinci Resolve / Premiere / Final Cut) with each
     camera placed on its own lane at its computed offset.

The offsets are the guaranteed-correct part. FCPXML import can occasionally
need a small nudge (frame rate / path), but the placement math is exact.

Single machine only: monotonic clocks aren't comparable across PCs.

Usage:
  python3 align_multicam.py /path/to/recordings --fps 60 -o synced.fcpxml
"""

import argparse
import glob
import json
import os
import sys
from xml.sax.saxutils import escape


def load_sidecars(folder):
    clips = []
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        try:
            with open(path) as fh:
                d = json.load(fh)
        except (OSError, json.JSONDecodeError) as e:
            print(f"  skip {os.path.basename(path)}: {e}", file=sys.stderr)
            continue
        if "start_monotonic_ns" not in d:
            continue  # not one of ours
        clips.append(d)
    return clips


def report(clips, fps):
    t0 = min(c["start_monotonic_ns"] for c in clips)
    print(f"\nReference (t0) = earliest start; timeline fps = {fps}\n")
    print(f"{'source':<16}{'offset (ms)':>12}{'offset (fr)':>12}"
          f"{'duration (s)':>14}{'  status'}")
    print("-" * 68)
    rows = []
    for c in sorted(clips, key=lambda x: x["start_monotonic_ns"]):
        off_ns = c["start_monotonic_ns"] - t0
        off_ms = off_ns / 1e6
        off_fr = round(off_ns / 1e9 * fps)
        dur_s = c.get("duration_ms", 0) / 1000.0
        status = "ok" if c.get("complete") else "INCOMPLETE (crash?)"
        print(f"{c.get('source','?'):<16}{off_ms:>12.2f}{off_fr:>12}"
              f"{dur_s:>14.2f}  {status}")
        rows.append((c, off_ns, off_fr, dur_s))
    print()
    return t0, rows


def fcpxml(rows, fps, out_path):
    # FCPXML rational times need integer denominators.
    if fps == int(fps):
        f = str(int(fps))
    else:
        # 59.94 / 29.97 style: express as ntsc rational
        print("  note: non-integer fps — using approximate rational; "
              "verify in your NLE", file=sys.stderr)
        f = str(int(round(fps)))
    fd = f"1/{f}s"
    parts = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<!DOCTYPE fcpxml>',
             '<fcpxml version="1.10">', '  <resources>']
    # one shared format; per-asset overrides not needed for placement
    w = rows[0][0].get("width", 1920)
    h = rows[0][0].get("height", 1080)
    parts.append(f'    <format id="r1" name="FFVideoFormat" '
                 f'frameDuration="{fd}" width="{w}" height="{h}"/>')
    for i, (c, off_ns, off_fr, dur_s) in enumerate(rows, 1):
        dur_fr = max(1, round(dur_s * fps))
        src = "file://" + escape(os.path.abspath(c.get("file", "")))
        parts.append(
            f'    <asset id="a{i}" name="{escape(c.get("source","cam"))}" '
            f'src="{src}" hasVideo="1" hasAudio="1" format="r1" '
            f'duration="{dur_fr}/{f}s"/>')
    parts += ['  </resources>', '  <library>',
              '    <event name="Instant Record Sync">',
              '      <project name="Multicam Synced">',
              f'        <sequence format="r1">', '          <spine>']
    # anchor = first clip on the spine; others connected at their offset/lane
    for i, (c, off_ns, off_fr, dur_s) in enumerate(rows, 1):
        dur_fr = max(1, round(dur_s * fps))
        name = escape(c.get("source", "cam"))
        if i == 1:
            parts.append(
                f'            <asset-clip ref="a{i}" name="{name}" '
                f'offset="0/{f}s" duration="{dur_fr}/{f}s">')
            anchor_close = True
        else:
            parts.append(
                f'              <asset-clip ref="a{i}" name="{name}" '
                f'lane="{i-1}" offset="{off_fr}/{f}s" '
                f'duration="{dur_fr}/{f}s"/>')
    parts.append('            </asset-clip>')
    parts += ['          </spine>', '        </sequence>', '      </project>',
              '    </event>', '  </library>', '</fcpxml>']
    with open(out_path, "w") as fh:
        fh.write("\n".join(parts))
    print(f"Wrote FCPXML -> {out_path}")


def main():
    ap = argparse.ArgumentParser(description="Align Instant Record ISO clips.")
    ap.add_argument("folder", help="folder containing the recordings + .json sidecars")
    ap.add_argument("--fps", type=float, default=60.0, help="timeline fps (default 60)")
    ap.add_argument("-o", "--output", default=None, help="FCPXML output path")
    args = ap.parse_args()

    clips = load_sidecars(args.folder)
    if not clips:
        print("No Instant Record sidecars found.", file=sys.stderr)
        sys.exit(1)

    _, rows = report(clips, args.fps)
    out = args.output or os.path.join(args.folder, "synced.fcpxml")
    fcpxml(rows, args.fps, out)


if __name__ == "__main__":
    main()
