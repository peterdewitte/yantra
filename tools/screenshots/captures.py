#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes the short animated capture of the indoor workflow (SCR-06).

usage: captures.py [--build DIR] [--out DIR] [--seconds S] [--keep-frames DIR]

The app renders the frames itself in screenshot mode, exactly as
screenshots.py does (SCR-01, SCR-02: private Xvfb display, private HOME and
XDG folders under /tmp/yantra-screenshots, the demo project with the
public-domain Fraunces Tavern sheets and its replayed walk-test
recordings, the simulated radio; never a MAK sheet, no user folder): a new
indoor project's way from the start screen through the plan import, the
walk test and the interpolated coverage map to the report dialog. ffmpeg
(the program only, spec 16.2) joins them with short cross-fades into

  docs/img/indoor_workflow.gif    for the README and the manual
  docs/img/indoor_workflow.webm   VP9, smaller, for the web

each under 10 seconds (default 8). The sizes are printed. Python's
standard library only.

Exit 0 when both files were made, 1 otherwise.
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screenshots  # noqa: E402  (same folder: the app runner and its private environment)

# The indoor workflow, in order: (scene, what it shows).
FRAMES = [
    ("start_menu", "start screen: New indoor project"),
    ("plan_import", "plan import: floors trimmed from one sheet"),
    ("walk_test", "walk test: the measured points on the floor"),
    ("interpolation", "coverage map interpolated from the points"),
    ("report", "File > Report: the coverage and acceptance report"),
]
NAME = "indoor_workflow"
WIDTH = 800
FADE = 0.4


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--build", default=os.path.join(screenshots.ROOT, "build"),
                   help="build tree with bin/yantra (default build)")
    p.add_argument("--out", default=os.path.join(screenshots.ROOT, "docs", "img"),
                   help="folder of the captures (default docs/img)")
    p.add_argument("--seconds", type=float, default=8.0, help="length of a capture, below 10 (default 8)")
    p.add_argument("--keep-frames", metavar="DIR", help="also keep the rendered frames in DIR")
    a = p.parse_args()
    if not 2.0 <= a.seconds < 10.0:
        sys.exit("captures: --seconds must be at least 2 and below 10 (SCR-06)")
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        sys.exit("captures: ffmpeg is needed (package ffmpeg; the program only)")
    build = os.path.abspath(a.build)
    yantra = os.path.join(build, "bin", "yantra")
    if not os.access(yantra, os.X_OK):
        sys.exit(f"captures: no {yantra}: build the app first (ninja -C {a.build})")
    if shutil.which("Xvfb") is None:
        sys.exit("captures: Xvfb is needed (package xvfb)")
    known = {s["name"]: s for s in screenshots.scenes(yantra)}
    missing = [n for n, _ in FRAMES if n not in known]
    if missing:
        sys.exit("captures: the app has no scene " + ", ".join(missing))

    frames_dir = os.path.join(build, "captures", "frames")
    shutil.rmtree(frames_dir, ignore_errors=True)
    os.makedirs(frames_dir)
    frames = []
    try:
        for i, (name, what) in enumerate(FRAMES):
            target = os.path.join(frames_dir, f"{i:02d}_{name}.png")
            print(f"captures: frame {i + 1}/{len(FRAMES)}: {what}", flush=True)
            if not screenshots.shoot(build, known[name], "dark", "en", target):
                return 1
            frames.append(target)
    finally:
        shutil.rmtree(screenshots.WORK, ignore_errors=True)
    if a.keep_frames:
        os.makedirs(a.keep_frames, exist_ok=True)
        for f in frames:
            shutil.copy(f, a.keep_frames)

    # Every frame is shown for `hold` seconds; neighbours cross-fade over
    # FADE seconds, so the whole capture lasts a.seconds.
    n = len(frames)
    hold = (a.seconds + (n - 1) * FADE) / n
    inputs = []
    for f in frames:
        inputs += ["-loop", "1", "-t", f"{hold:.3f}", "-framerate", "25", "-i", f]
    chain, last = [], "[0:v]"
    for i in range(1, n):
        offset = i * (hold - FADE)
        chain.append(f"{last}[{i}:v]xfade=transition=fade:duration={FADE}:offset={offset:.3f}[x{i}]")
        last = f"[x{i}]"
    joined = ";".join(chain)
    os.makedirs(a.out, exist_ok=True)
    gif = os.path.join(os.path.abspath(a.out), NAME + ".gif")
    webm = os.path.join(os.path.abspath(a.out), NAME + ".webm")
    # No metadata (encoder dates, file names) in either file.
    common = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", *inputs]
    gif_filter = (f"{joined};{last}fps=5,scale={WIDTH}:-1:flags=lanczos,split[a][b];"
                  "[a]palettegen=max_colors=64:stats_mode=diff[p];"
                  "[b][p]paletteuse=dither=none:diff_mode=rectangle")
    webm_filter = f"{joined};{last}fps=25,scale={WIDTH}:-2:flags=lanczos,format=yuv420p"
    runs = [
        common + ["-filter_complex", gif_filter, "-map_metadata", "-1", "-loop", "0", gif],
        common + ["-filter_complex", webm_filter, "-map_metadata", "-1", "-c:v", "libvpx-vp9", "-b:v", "0",
                  "-crf", "42", "-row-mt", "1", "-an", "-flags", "+bitexact", webm],
    ]
    for cmd in runs:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(f"captures: ffmpeg failed:\n{r.stderr}\n")
            return 1
    for f in (gif, webm):
        print(f"captures: {os.path.relpath(f, screenshots.ROOT)}: {os.path.getsize(f) / 1e3:.0f} kB, "
              f"{a.seconds:g} s, {n} frames")
    shutil.rmtree(frames_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
