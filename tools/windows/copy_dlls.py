#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The DLLs Yantra's Windows programs need (OA-02, NFR-09): reads the
# import tables of the given .exe/.dll files (and of every DLL found for
# them, recursively), finds each imported DLL in the search folders (the
# SDK sysroot's bin/ with the MSYS2 UCRT64 packages, PDFium's bin/) and
# copies it next to the programs. Windows' own DLLs (not in a search
# folder) are left to the system. Also lists what it copied, so the
# packaging step (13.9) and its DLL import check can reuse it.
#
#   copy_dlls.py --dest <folder> --search <folder>... [--extra <dll name>...] <pe file|folder>...
#
# A folder argument stands for every .exe and .dll in it. --extra copies
# named DLLs (and what they import) even when no program imports them (the
# test tree's Mesa opengl32.dll for off-screen rendering in the test VM).
import argparse
import pathlib
import shutil
import struct
import sys

# Windows system DLLs that a package in the sysroot also provides (Mesa's
# opengl32.dll): a program's import of them stays the system's unless
# named with --extra.
SYSTEM_NAMES = {"opengl32.dll"}


def pe_imports(path):
    """Names of the DLLs a PE file imports (normal and delay-load)."""
    data = pathlib.Path(path).read_bytes()
    if data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return []
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dirs = opt + (112 if magic == 0x20B else 96)
    sections = []
    sec = opt + opt_size
    for i in range(nsec):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
        sections.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        return None

    def name_at(rva):
        o = off(rva)
        if o is None:
            return None
        end = data.index(b"\0", o)
        return data[o:end].decode("ascii", "replace")

    names = []
    for index, entry_size, name_field in ((1, 20, 12), (13, 32, 4)):
        rva, size = struct.unpack_from("<II", data, dirs + 8 * index)
        o = off(rva) if rva else None
        while o is not None and o + entry_size <= len(data):
            name_rva = struct.unpack_from("<I", data, o + name_field)[0]
            if name_rva == 0:
                break
            n = name_at(name_rva)
            if n:
                names.append(n)
            o += entry_size
    return names


def main():
    ap = argparse.ArgumentParser(description="Copy the DLLs Yantra's Windows programs import (OA-02)")
    ap.add_argument("--dest", type=pathlib.Path, required=True)
    ap.add_argument("--search", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--extra", action="append", default=[])
    ap.add_argument("files", type=pathlib.Path, nargs="*")
    a = ap.parse_args()
    available = {}
    for folder in a.search:
        if folder.is_dir():
            for f in folder.glob("*.dll"):
                available.setdefault(f.name.lower(), f)
    todo = []
    for f in a.files:
        if f.is_dir():
            todo += sorted(p for p in f.iterdir() if p.suffix.lower() in (".exe", ".dll"))
        elif f.exists():
            todo.append(f)
    for name in a.extra:
        if name.lower() not in available:
            print(f"copy_dlls: {name} not found in the search folders", file=sys.stderr)
            return 1
        todo.append(available[name.lower()])
    extras = {n.lower() for n in a.extra}
    seen, copied = set(), []
    while todo:
        f = todo.pop()
        for name in pe_imports(f):
            key = name.lower()
            if key in seen or key not in available or (key in SYSTEM_NAMES and key not in extras):
                continue
            seen.add(key)
            src = available[key]
            dst = a.dest / src.name
            if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime or dst.stat().st_size != src.stat().st_size:
                a.dest.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
            copied.append(src.name)
            todo.append(src)
    for name in a.extra:
        src = available[name.lower()]
        dst = a.dest / src.name
        if not dst.exists() or dst.stat().st_size != src.stat().st_size:
            shutil.copy2(src, dst)
        copied.append(src.name)
    (a.dest / "yantra-dlls.txt").write_text("".join(sorted(f"{n}\n" for n in set(copied))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
