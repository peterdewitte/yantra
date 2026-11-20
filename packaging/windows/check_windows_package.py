#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks a Windows package of Yantra without running it (NFR-09, TST-02).

  check_windows_package.py PACKAGE [--source DIR] [--newer-than FILE]...

PACKAGE is the portable zip (yantra-<version>-windows-x86_64.zip) or its
unpacked folder. Checks:

- DLL imports: every .exe and .dll imports only DLLs that are in the
  package (next to it, or next to the programs in bin/, where Windows and
  Yantra's plugin loader look) or that are Windows system DLLs or the
  UCRT (api-ms-win-*); msvcrt.dll, a second C runtime, is refused.
- No test-only file: no DLL named like a Windows system DLL (Mesa's
  opengl32.dll, a 16.2 test tool: the app uses the system's OpenGL), no
  test program or test/example plugin, no DLL that nothing imports.
- Contents: the programs, the plugins of Yantra and the radio plugins
  (PLG-26, each radio plugin exporting yantra_radio_entry), data (fonts
  with their OFL texts, logo, demo project), the licence texts (LICENSE,
  THIRD_PARTY.md, the radios' bundle RAD-137, PDFium's, one folder per
  MSYS2 package listed in share/licenses/msys2/PACKAGES.txt for every
  bundled DLL), the corresponding-source note with the source URL, and
  yantra.exe with its icon and version information (BRD-06).
- No MAK plan (DEMO-12): no picture under demo/mak and, with --source, no
  file with a MAK sheet's SHA-256 from data/demo/mak/*.json.
- No personal data: no file holds the home folder, the source or build
  folder, or (with --source) the git name and e-mail of the builder.
- --newer-than: the package must be newer than each FILE (a package made
  before the last build of the tree is stale).

--self-test: after the check, the package is changed in memory in ways the
check must catch (a missing DLL, Mesa's opengl32.dll, an unused DLL, a
test program, a MAK plan picture, the home folder in a file, a DLL without
its licence entry, yantra.exe without its icon) and each must be found.

Exit code 0 when every check passes, 1 otherwise (findings on stdout).
The PE reader is copied from yantra-radios 2b79bd7:
tools/ci/check_completeness.py (read_pe), with resources added.
"""

import argparse
import hashlib
import os
import pathlib
import re
import struct
import subprocess
import sys
import zipfile

# Windows system DLLs Yantra's programs, the bundled MSYS2 DLLs and the
# radio plugins import (compared case-insensitively); with the UCRT and
# the API sets (api-ms-win-*, ext-ms-win-*). opengl32.dll is the system's
# OpenGL (the graphics driver's ICD), so the package must not hold one.
SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "bcryptprimitives.dll", "cfgmgr32.dll", "comctl32.dll",
    "comdlg32.dll", "crypt32.dll", "dbghelp.dll", "dnsapi.dll", "dwmapi.dll", "gdi32.dll",
    "imm32.dll", "iphlpapi.dll", "kernel32.dll", "kernelbase.dll", "mpr.dll", "msimg32.dll",
    "normaliz.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll", "opengl32.dll", "psapi.dll",
    "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shell32.dll", "shlwapi.dll", "ucrtbase.dll",
    "user32.dll", "userenv.dll", "uxtheme.dll", "version.dll", "winhttp.dll", "winmm.dll",
    "winusb.dll", "wldap32.dll", "ws2_32.dll", "wsock32.dll",
}
SYSTEM_PATTERNS = [re.compile(r"api-ms-win-[a-z0-9-]+\.dll"), re.compile(r"ext-ms-win-[a-z0-9-]+\.dll")]
REFUSED = {"msvcrt.dll": "msvcrt.dll is a second C runtime next to the UCRT"}

PROGRAMS = ["yantra.exe", "yantra-probe.exe", "yantra-plugin-test.exe",
            "yantra-radio-host.exe", "yantra-hackrf.exe", "yantra-gen.exe"]
PLUGINS = ["lib/yantra/plugins/probe.dll", "lib/yantra/plugins/tetra.dll"]
RADIO_PLUGINS = ["hackrf", "pei", "simulator", "soapysdr"]
REQUIRED = [
    "README.txt", "LICENSE.txt", "SOURCE.txt",
    "share/yantra/THIRD_PARTY.md", "share/yantra/LEGAL.md",
    "share/yantra/fonts/Inter-Regular.ttf", "share/yantra/fonts/Inter-OFL.txt",
    "share/yantra/fonts/JetBrainsMono-Regular.ttf", "share/yantra/fonts/JetBrainsMono-OFL.txt",
    "share/yantra/logo/png/yantra_logo_256.png", "share/yantra/demo/fraunces/README.md",
    "share/yantra/demo/fraunces/fraunces.yantra", "share/yantra/demo/scenes/fraunces-1.json",
    "share/licenses/msys2/PACKAGES.txt", "share/doc/yantra/LICENSES/GPL-3.0-or-later.txt",
]
REQUIRED_DIRS = ["share/licenses/yantra-radios", "share/licenses/pdfium", "share/yantra/manual",
                 "share/yantra-radios/scenarios"]
# The PDF manual (DOC-02a) of the package's own version.
MANUAL_PDF = re.compile(r"^share/doc/yantra/yantra-manual-[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?\.pdf$")  # a pre-release has a suffix (PLG-29)
PICTURE = re.compile(r"\.(png|jpe?g|pdf|tiff?|gif|webp|bmp)$", re.I)


def read_pe(data: bytes):
    """Imports (import and delay-import tables), exported names, the
    subsystem and the resource type ids of a PE32+ file. Raises ValueError
    on a file that is not PE."""
    def u16(o): return struct.unpack_from("<H", data, o)[0]
    def u32(o): return struct.unpack_from("<I", data, o)[0]
    try:
        if data[:2] != b"MZ":
            raise ValueError("no MZ header")
        pe = u32(0x3C)
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        nsec, opt_size = u16(pe + 6), u16(pe + 20)
        opt = pe + 24
        magic = u16(opt)
        if magic == 0x20B:
            ndirs, dirs = u32(opt + 108), opt + 112
        elif magic == 0x10B:
            ndirs, dirs = u32(opt + 92), opt + 96
        else:
            raise ValueError(f"unknown optional header magic {magic:#x}")
        subsystem = u16(opt + 68)
        sections = []
        for i in range(nsec):
            h = opt + opt_size + 40 * i
            vsize, va, rsize, raw = struct.unpack_from("<IIII", data, h + 8)
            sections.append((va, max(vsize, rsize), raw))

        def off(rva):
            for va, size, raw in sections:
                if va <= rva < va + size:
                    return raw + rva - va
            raise ValueError(f"RVA {rva:#x} is in no section")

        def cstr(rva):
            o = off(rva)
            return data[o:data.index(b"\0", o)].decode("ascii", "replace")

        def directory(i):
            return struct.unpack_from("<II", data, dirs + 8 * i) if i < ndirs else (0, 0)

        imports, exports, resources = [], [], []
        rva, size = directory(1)
        if rva and size:
            o = off(rva)
            while True:
                name = u32(o + 12)
                if not any(struct.unpack_from("<5I", data, o)):
                    break
                imports.append(cstr(name))
                o += 20
        rva, size = directory(13)  # delay-load imports
        if rva and size:
            o = off(rva)
            while any(struct.unpack_from("<8I", data, o)):
                imports.append(cstr(u32(o + 4)))
                o += 32
        rva, size = directory(0)
        if rva and size:
            o = off(rva)
            base, nfun, nnames, afun, anames, aords = struct.unpack_from("<6I", data, o + 16)
            named = {}
            for i in range(nnames):
                named[u16(off(aords) + 2 * i)] = cstr(u32(off(anames) + 4 * i))
            for i in range(nfun):
                if u32(off(afun) + 4 * i):
                    exports.append(named.get(i, f"#{base + i}"))
        rva, size = directory(2)  # resources: the root directory's type ids
        if rva and size:
            o = off(rva)
            named_n, id_n = u16(o + 12), u16(o + 14)
            for i in range(named_n, named_n + id_n):
                resources.append(u32(o + 16 + 8 * i) & 0xFFFF)
        return imports, sorted(set(exports)), subsystem, resources
    except (struct.error, IndexError) as e:
        raise ValueError(f"truncated or malformed PE file ({e})") from None


def is_system(name: str) -> bool:
    n = name.lower()
    return n in SYSTEM_DLLS or any(p.fullmatch(n) for p in SYSTEM_PATTERNS)


def load(package: pathlib.Path) -> dict[str, bytes]:
    """The package's files by path below its top folder."""
    files = {}
    if package.is_dir():
        for p in sorted(package.rglob("*")):
            if p.is_file():
                files[p.relative_to(package).as_posix()] = p.read_bytes()
        return files
    with zipfile.ZipFile(package) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        tops = {n.split("/", 1)[0] for n in names}
        if len(tops) != 1 or any("/" not in n for n in names):
            raise ValueError(f"the zip must hold one top folder, not {sorted(tops)[:5]}")
        for n in names:
            files[n.split("/", 1)[1]] = z.read(n)
    return files


def check(files: dict[str, bytes], source: pathlib.Path | None) -> list[str]:
    bad = []
    lower = {p.lower(): p for p in files}
    pe_files = {p: d for p, d in files.items() if p.lower().endswith((".exe", ".dll"))}
    dlls_in = {}
    for p in pe_files:
        if p.lower().endswith(".dll"):
            folder, _, name = p.rpartition("/")
            dlls_in.setdefault(folder, set()).add(name.lower())
    imported = set()
    infos = {}
    for p, data in sorted(pe_files.items()):
        try:
            imports, exports, subsystem, resources = read_pe(data)
        except ValueError as e:
            bad.append(f"{p}: {e}")
            continue
        infos[p] = (imports, exports, subsystem, resources)
        folder = p.rpartition("/")[0]
        for name in imports:
            n = name.lower()
            if n in REFUSED:
                bad.append(f"{p}: imports {name}: {REFUSED[n]}")
            elif n in dlls_in.get(folder, set()) or n in dlls_in.get("bin", set()):
                imported.add(n)
            elif not is_system(n):
                bad.append(f"{p}: imports {name}, which is neither in the package nor a Windows system DLL")
    # No test-only or unused DLL next to the programs.
    for name in sorted(dlls_in.get("bin", set())):
        if is_system(name):
            bad.append(f"bin/{name}: a Windows system DLL in the package (Mesa is a test tool, never shipped)")
        elif name not in imported:
            bad.append(f"bin/{name}: no program or plugin imports it")
    for p in files:
        base = p.rsplit("/", 1)[-1].lower()
        if base.startswith(("test_", "bench_", "fake")) or "example_beacon" in base or "/examples/" in p \
                or base in ("yantra-demo.exe", "yantra-fuzz-seeds.exe", "yantra-pei-sim.exe", "yantra-dlls.txt"):
            bad.append(f"{p}: a test or build-tree file in the package")
    # Contents.
    for prog in PROGRAMS:
        if f"bin/{prog}" not in files:
            bad.append(f"bin/{prog} missing")
    for plugin in PLUGINS + [f"lib/yantra/plugins/radios/{r}.dll" for r in RADIO_PLUGINS]:
        if plugin not in files:
            bad.append(f"{plugin} missing (PLG-26)")
    for r in RADIO_PLUGINS:
        info = infos.get(f"lib/yantra/plugins/radios/{r}.dll")
        if info and "yantra_radio_entry" not in info[1]:
            bad.append(f"lib/yantra/plugins/radios/{r}.dll does not export yantra_radio_entry")
    for req in REQUIRED:
        if req not in files:
            bad.append(f"{req} missing")
    if not any(MANUAL_PDF.match(p) and files[p][:5] == b"%PDF-" for p in files):
        bad.append("share/doc/yantra/yantra-manual-<version>.pdf missing (DOC-02a)")
    for d in REQUIRED_DIRS:
        if not any(p.startswith(d + "/") for p in files):
            bad.append(f"{d}/ missing or empty")
    info = infos.get("bin/yantra.exe")
    if info:
        if info[2] != 2:
            bad.append("bin/yantra.exe is not a windowed (GUI) program")
        for rid, what in ((14, "icon"), (16, "version information")):
            if rid not in info[3]:
                bad.append(f"bin/yantra.exe has no {what} resource (BRD-06)")
    # The licences of every bundled MSYS2 DLL (PACKAGES.txt: dll package version licence).
    listed = {}
    for line in files.get("share/licenses/msys2/PACKAGES.txt", b"").decode("utf-8", "replace").splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0].lower().endswith(".dll"):
            listed[parts[0].lower()] = parts[1]
    for name in sorted(dlls_in.get("bin", set())):
        if name == "pdfium.dll":
            continue
        pkg = listed.get(name)
        if pkg is None:
            bad.append(f"bin/{name}: not listed in share/licenses/msys2/PACKAGES.txt")
        elif not any(p.startswith(f"share/licenses/msys2/{pkg}/") for p in files):
            bad.append(f"bin/{name}: no licence texts in share/licenses/msys2/{pkg}/")
    source_note = files.get("SOURCE.txt", b"").decode("utf-8", "replace")
    if not re.search(r"https://\S+", source_note):
        bad.append("SOURCE.txt names no source URL (NFR-01, NFR-09)")
    # MAK plans never in a package (DEMO-12).
    for p in files:
        if "/demo/mak/" in p and PICTURE.search(p):
            bad.append(f"{p}: a MAK plan picture in the package (DEMO-12)")
    if source:
        sums = set()
        for j in sorted((source / "data/demo/mak").glob("*.json")):
            sums.update(re.findall(r'"sha256":\s*"([0-9a-f]{64})"', j.read_text()))
        if not sums:
            bad.append("no MAK sheet SHA-256 found in data/demo/mak/*.json")
        for p, data in files.items():
            if hashlib.sha256(data).hexdigest() in sums:
                bad.append(f"{p}: a MAK sheet (by SHA-256) in the package (DEMO-12)")
    # Personal data and build-machine folders.
    needles = set()
    home = os.environ.get("HOME", "")
    if home not in ("", "/"):
        needles.add(home)
    if source:
        needles.add(str(source.resolve()))
        for key in ("user.name", "user.email"):
            v = subprocess.run(["git", "-C", str(source), "config", key], capture_output=True,
                               text=True).stdout.strip()
            if len(v) >= 4:
                needles.add(v)
    for needle in sorted(needles):
        raw = needle.encode()
        wide = needle.encode("utf-16-le")
        hits = [p for p, d in files.items() if raw in d or wide in d]
        if hits:
            bad.append(f"a build-machine folder or the builder's identity in: {', '.join(sorted(hits)[:5])}")
    return bad


def self_test(files: dict[str, bytes], source: pathlib.Path | None) -> list[str]:
    """Faults the check must find, each in its own copy of the package."""
    some_dll = files["bin/zlib1.dll"]
    exe = bytearray(files["bin/yantra.exe"])
    try:
        imports, _, _, _ = read_pe(bytes(exe))
    except ValueError:
        return ["bin/yantra.exe is not a PE file"]
    no_icon = bytes(exe).replace(b"\x0e\x00\x00\x00", b"\x0f\x00\x00\x00")  # GROUP_ICON id gone
    home = os.environ.get("HOME", "") if os.environ.get("HOME", "") not in ("", "/") else str(source or "/nowhere")
    faults = {
        "a DLL missing": ({"bin/zlib1.dll": None}, "imports zlib1.dll, which is neither"),
        "Mesa in the package": ({"bin/opengl32.dll": some_dll}, "a Windows system DLL in the package"),
        "an unused DLL": ({"bin/libunused-1.dll": some_dll}, "no program or plugin imports it"),
        "a test program": ({"bin/test_core.exe": files["bin/yantra-probe.exe"]}, "a test or build-tree file"),
        "a MAK plan": ({"share/yantra/demo/mak/mak_villa_bunzl/plan.png": b"x"}, "a MAK plan picture"),
        "the home folder": ({"share/yantra/notes.txt": home.encode()}, "a build-machine folder"),
        "no licence entry": ({"share/licenses/msys2/PACKAGES.txt": b""}, "not listed in share/licenses/msys2"),
        "no icon": ({"bin/yantra.exe": no_icon}, "has no icon resource"),
        "no source URL": ({"SOURCE.txt": b"none"}, "SOURCE.txt names no source URL"),
        "no yantra-gen": ({"bin/yantra-gen.exe": None}, "bin/yantra-gen.exe missing"),
        "no PDF manual": ({p: None for p in files if MANUAL_PDF.match(p)}, "yantra-manual-<version>.pdf missing"),
    }
    missed = []
    for what, (change, finding) in faults.items():
        copy = dict(files)
        for k, v in change.items():
            if v is None:
                copy.pop(k, None)
            else:
                copy[k] = v
        found = check(copy, source)
        if not any(finding in f for f in found):
            missed.append(f"self-test: {what} not found (findings: {found[:3]})")
        else:
            print(f"ok: self-test: {what} is found")
    return missed


def main() -> int:
    ap = argparse.ArgumentParser(description="Check a Windows package of Yantra (NFR-09)")
    ap.add_argument("package", type=pathlib.Path)
    ap.add_argument("--source", type=pathlib.Path, help="the source tree (MAK sums, git identity)")
    ap.add_argument("--newer-than", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if not a.package.exists():
        print(f"FAIL: {a.package} not found: make it first (ninja -C <windows tree> package-windows)")
        return 1
    bad = []
    for f in a.newer_than:
        if f.exists() and f.stat().st_mtime > a.package.stat().st_mtime:
            bad.append(f"the package is older than {f.name}: make it again (ninja -C <windows tree> package-windows)")
    try:
        files = load(a.package)
    except (OSError, ValueError, zipfile.BadZipFile) as e:
        print(f"FAIL: {a.package}: {e}")
        return 1
    bad += check(files, a.source)
    if a.self_test and not bad:
        bad += self_test(files, a.source)
    pe = sum(1 for p in files if p.lower().endswith((".exe", ".dll")))
    if bad:
        for b in bad:
            print(f"FAIL: {b}")
        return 1
    print(f"ok: {a.package.name}: {len(files)} files, {pe} programs and DLLs import only "
          "packaged DLLs and Windows system DLLs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
