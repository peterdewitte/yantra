#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerates the documentation's screenshot set in one run (SCR-03, SCR-04).

usage: screenshots.py [--build DIR] [--out DIR] [--only NAME ...]
                      [--themes dark,light] [--no-optimise]
                      [--compare DIR] [--threshold PERCENT]

Every scene `yantra --list-shots` names is rendered by the app itself
(`yantra --screenshot FILE --shot NAME`, SCR-01) on a private Xvfb display
under Mesa, in the dark and the light theme, as <scene>_<theme>.png; the
main survey view also in Dutch, French and German as
survey_<lang>_<theme>.png. The data is the demo project (the public-domain
Fraunces Tavern sheets, a fictitious network) and the simulated radios
(SCR-02): the simulator plugin, and the HackRF plugin's test build on the
fake SoapySDR device of yantra-radios' test kit. No MAK sheet (DEMO-12),
no network access, no user files: the app runs with a private HOME and
XDG folders under /tmp/yantra-screenshots (a fixed path, as the project
window shows it), removed afterwards.

The PNGs are made smaller without loss by oxipng (spec 16.2) when it is
installed (PATH or ~/.cargo/bin). --compare DIR compares the new set
with the PNGs in DIR (SCR-05) and fails when a picture differs in more
than --threshold percent of its pixels (default 2) or is new or missing.
Python's standard library only (no image package).

Exit 0 when every scene was rendered (and compared), 1 otherwise.
"""
import argparse
import json
import os
import shutil
import subprocess
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORK = "/tmp/yantra-screenshots"
THEMES = ("dark", "light")
LANGUAGES = ("nl", "fr", "de")
SIZE = "1280x800"


def oxipng():
    found = shutil.which("oxipng")
    if found:
        return found
    cargo = os.path.expanduser("~/.cargo/bin/oxipng")
    return cargo if os.access(cargo, os.X_OK) else None


def scenes(yantra):
    out = subprocess.run([yantra, "--list-shots"], check=True, capture_output=True, text=True).stdout
    result = []
    for line in out.splitlines():
        name, flags, what = line.split("\t", 2)
        result.append({"name": name, "flags": flags.split(","), "what": what})
    return result


def cache_value(build, key):
    with open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8") as f:
        for line in f:
            if line.startswith(key + ":"):
                return line.split("=", 1)[1].strip()
    return ""


def environment(build, scene):
    env = {k: v for k, v in os.environ.items() if not k.startswith(("XDG_", "YANTRA_", "WAYLAND"))}
    env.update(
        HOME=os.path.join(WORK, "home"),
        XDG_CONFIG_HOME=os.path.join(WORK, "config"),
        XDG_DATA_HOME=os.path.join(WORK, "data"),
        XDG_CACHE_HOME=os.path.join(WORK, "cache"),
        TMPDIR=os.path.join(WORK, "tmp"),
        LIBGL_ALWAYS_SOFTWARE="1",
        TZ="UTC",
        LANG="C.UTF-8",
    )
    if "radio=hackrf" in scene["flags"]:
        # The HackRF plugin's test build on the fake SoapySDR device (RAD-92):
        # an invented serial, the two fictitious cells of the test kit.
        radios = os.path.join(WORK, "plugins", "radios")
        os.makedirs(radios, exist_ok=True)
        shutil.copy(os.path.join(build, "tests", "radio-plugins", "hackrf_fake.so"), radios)
        source = cache_value(build, "YANTRA_RADIOS_DIR") or os.path.join(ROOT, "third_party", "yantra-radios")
        scenario = os.path.join(source, "tests", "fixtures", "soapy", "fake_two_cells.json")
        control = os.path.join(WORK, "fake_sdr")
        os.makedirs(control, exist_ok=True)
        env["YANTRA_PLUGIN_PATH"] = os.path.join(WORK, "plugins")
        env["YANTRA_FAKE_SOAPY"] = json.dumps(
            {"control": control,
             "devices": [{"serial": "0000000000000000a1b2c3d4e5f60009", "scenario": scenario, "paced": True}]},
            separators=(",", ":"))
    return env


def shoot(build, scene, theme, language, out_file):
    shutil.rmtree(WORK, ignore_errors=True)
    for d in ("home", "config", "data", "cache", "tmp"):
        os.makedirs(os.path.join(WORK, d))
    cmd = [os.path.join(ROOT, "tests", "app", "with_xvfb.sh"), os.path.join(build, "bin", "yantra"),
           "--window-system", "x11", "--size", SIZE, "--theme", theme, "--language", language,
           "--shot", scene["name"], "--screenshot", out_file]
    if "input" in scene["flags"]:
        # The app combines the two public-domain Fraunces sheets into one.
        cmd += ["--input", os.path.join(ROOT, "data", "demo", "fraunces")]
    r = subprocess.run(cmd, env=environment(build, scene), capture_output=True, text=True, timeout=600)
    ok = r.returncode == 0 and os.path.exists(out_file)
    if not ok:
        sys.stderr.write(f"screenshots: {scene['name']} ({theme}, {language}) failed ({r.returncode}):\n"
                         f"{r.stdout}{r.stderr}\n")
    return ok


def png_pixels(path):
    """(width, height, RGBA bytes) of a non-interlaced PNG, with the
    standard library only (zlib)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, palette, trns = 8, [], None, None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if interlace:
        raise ValueError(f"{path}: interlaced PNGs are not compared")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)
    stride = (w * channels * depth + 7) // 8
    raw = zlib.decompress(b"".join(idat))
    rows, prev = [], bytearray(stride)
    for y in range(h):
        filt = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if filt == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif filt == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif filt == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif filt == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], (prev[i - bpp] if i >= bpp else 0)
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    out = bytearray()
    for line in rows:
        if depth < 8:
            per = 8 // depth
            mask = (1 << depth) - 1
            samples = [(line[i // per] >> (8 - depth * (i % per + 1))) & mask for i in range(w)]
            scale = 255 // mask if ctype == 0 else 1
            values = [v * scale for v in samples]
        elif depth == 16:
            values = list(line[0::2])
        else:
            values = list(line)
        for x in range(w):
            if ctype == 3:
                i = values[x]
                a = trns[i] if trns and i < len(trns) else 255
                out += palette[3 * i:3 * i + 3] + bytes([a])
            elif ctype == 0:
                out += bytes([values[x]] * 3 + [255])
            elif ctype == 4:
                out += bytes([values[2 * x]] * 3 + [values[2 * x + 1]])
            elif ctype == 2:
                out += bytes(values[3 * x:3 * x + 3] + [255])
            else:
                out += bytes(values[4 * x:4 * x + 4])
    return w, h, bytes(out)


def compare(new_dir, old_dir, threshold):
    problems = []
    new = {f for f in os.listdir(new_dir) if f.endswith(".png")}
    # The Yantra logo copies (yantra_logo_*.png, BRD-06) live in docs/img for
    # the README and the manual but are not screenshots.
    old = {f for f in os.listdir(old_dir) if f.endswith(".png") and not f.startswith("yantra_logo")} \
        if os.path.isdir(old_dir) else set()
    for f in sorted(new - old):
        problems.append(f"{f}: new picture")
    for f in sorted(old - new):
        problems.append(f"{f}: no longer made")
    for f in sorted(new & old):
        a_path, b_path = os.path.join(new_dir, f), os.path.join(old_dir, f)
        with open(a_path, "rb") as a, open(b_path, "rb") as b:
            if a.read() == b.read():
                continue
        aw, ah, a = png_pixels(a_path)
        bw, bh, b = png_pixels(b_path)
        if (aw, ah) != (bw, bh):
            problems.append(f"{f}: size {aw}x{ah} instead of {bw}x{bh}")
            continue
        # A pixel changed when one of its channels differs by more than 16.
        changed = sum(1 for i in range(0, len(a), 4)
                      if max(abs(a[i + k] - b[i + k]) for k in range(4)) > 16)
        share = 100.0 * changed / (aw * ah)
        if share > threshold:
            problems.append(f"{f}: {share:.2f} % of the pixels changed (limit {threshold} %)")
    return problems


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--build", default=os.path.join(ROOT, "build"), help="build tree with bin/yantra (default build)")
    p.add_argument("--out", default=os.path.join(ROOT, "docs", "img"), help="folder of the set (default docs/img)")
    p.add_argument("--only", nargs="*", default=[], help="scene names to make (default all)")
    p.add_argument("--themes", default=",".join(THEMES), help="themes (default dark,light)")
    p.add_argument("--no-optimise", action="store_true", help="skip oxipng")
    p.add_argument("--compare", metavar="DIR", help="compare with the PNGs in DIR (SCR-05)")
    p.add_argument("--threshold", type=float, default=2.0, help="changed pixels in percent (default 2)")
    a = p.parse_args()

    build = os.path.abspath(a.build)
    yantra = os.path.join(build, "bin", "yantra")
    if not os.access(yantra, os.X_OK):
        sys.exit(f"screenshots: no {yantra}: build the app first (ninja -C {a.build})")
    if shutil.which("Xvfb") is None:
        sys.exit("screenshots: Xvfb is needed (package xvfb)")
    all_scenes = scenes(yantra)
    unknown = set(a.only) - {s["name"] for s in all_scenes}
    if unknown:
        sys.exit("screenshots: no such scene: " + ", ".join(sorted(unknown)))
    chosen = [s for s in all_scenes if not a.only or s["name"] in a.only]
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)

    failed, made = [], []
    try:
        for s in chosen:
            runs = [(t, "en", f"{s['name']}_{t}.png") for t in a.themes.split(",")]
            if s["name"] == "survey":
                runs += [(t, lang, f"survey_{lang}_{t}.png") for lang in LANGUAGES for t in a.themes.split(",")]
            for theme, language, name in runs:
                target = os.path.join(out, name)
                print(f"screenshots: {name}", flush=True)
                if shoot(build, s, theme, language, target):
                    made.append(target)
                else:
                    failed.append(name)
    finally:
        shutil.rmtree(WORK, ignore_errors=True)

    tool = None if a.no_optimise else oxipng()
    if made and tool:
        subprocess.run([tool, "-q", "-o", "4", "--strip", "safe", *made], check=True)
    elif made and not a.no_optimise:
        print("screenshots: oxipng not found: PNGs left unoptimised", file=sys.stderr)
    size = sum(os.path.getsize(f) for f in made)
    print(f"screenshots: {len(made)} pictures, {size / 1e6:.2f} MB in {out}")
    if a.compare:
        problems = compare(out, os.path.abspath(a.compare), a.threshold)
        for pr in problems:
            print("screenshots: changed:", pr)
        if problems:
            return 1
    if failed:
        print("screenshots: failed: " + ", ".join(failed), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
