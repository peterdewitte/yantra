#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Windows icon of Yantra (BRD-06, NFR-09) from the supplied logo PNGs.

Writes a multi-size .ico whose images are the files of data/logo/png at
16, 24, 32, 48, 64, 128 and 256 px, byte for byte (PNG-compressed icon
entries, read by Windows Vista and later): the logo as supplied, never
redrawn or recoloured (BRD-05). The .ico is embedded in yantra.exe
(app/yantra.rc.in, llvm-rc) and used by the setup .exe (NSIS).

  make_ico.py --png-dir data/logo/png --out yantra.ico
  make_ico.py --png-dir data/logo/png --check yantra.ico   (ctest windows_icon)

--check reads an .ico and fails unless it holds exactly those PNG files.
"""

import argparse
import pathlib
import struct
import sys

SIZES = (16, 24, 32, 48, 64, 128, 256)


def png_size(data: bytes) -> tuple[int, int]:
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise ValueError("not a PNG file")
    return struct.unpack(">II", data[16:24])


def build_ico(images: list[bytes]) -> bytes:
    head = struct.pack("<HHH", 0, 1, len(images))
    entries, blobs = b"", b""
    offset = 6 + 16 * len(images)
    for data in images:
        w, h = png_size(data)
        entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(data), offset + len(blobs))
        blobs += data
    return head + entries + blobs


def read_ico(data: bytes) -> list[bytes]:
    reserved, kind, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or kind != 1:
        raise ValueError("not an icon file")
    out = []
    for i in range(count):
        _, _, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        out.append(data[offset:offset + size])
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--png-dir", type=pathlib.Path, required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--out", type=pathlib.Path)
    g.add_argument("--check", type=pathlib.Path)
    a = ap.parse_args()
    images = []
    for size in SIZES:
        data = (a.png_dir / f"yantra_logo_{size}.png").read_bytes()
        if png_size(data) != (size, size):
            print(f"make_ico: yantra_logo_{size}.png is {png_size(data)}, not {size}x{size}", file=sys.stderr)
            return 1
        images.append(data)
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        ico = build_ico(images)
        if not a.out.exists() or a.out.read_bytes() != ico:
            a.out.write_bytes(ico)
        return 0
    try:
        found = read_ico(a.check.read_bytes())
    except (OSError, ValueError, struct.error) as e:
        print(f"FAIL: {a.check}: {e}")
        return 1
    if found != images:
        print(f"FAIL: {a.check} does not hold the logo PNGs {SIZES} as supplied")
        return 1
    print(f"ok: {a.check}: {len(found)} images, the logo PNGs of data/logo/png byte for byte")
    return 0


if __name__ == "__main__":
    sys.exit(main())
