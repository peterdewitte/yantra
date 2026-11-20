#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""No folder of the build machine in a shipped file (RAD-137, RAD-138; no
personal data, Yantra NFR-09).

usage: check_shipped_paths.py [--cmake CMAKE] --build BUILD_DIR --root DIR
           [--forbid DIR]... [--forbid-binary TEXT]... [--keep]
       check_shipped_paths.py --self-test

Installs BUILD_DIR with `cmake --install --prefix DIR/stage` (what the
release packages and Yantra ship: plugins, programs, data, docs), then
reads every installed file and fails when one holds

  - a forbidden folder (--forbid: the source tree, the build tree, the
    cross SDK both as given and resolved, and the home folder of the
    user running the check, taken from HOME/USERPROFILE when it is at
    least two levels deep), in any file, or
  - a --forbid-binary text in an executable or library (ELF or PE):
    the configured install folders (CMAKE_INSTALL_FULL_DATADIR and
    LIBDIR + /yantra-radios), so no program looks in a folder that only
    exists on the machine it was built on; it must find its data relative
    to itself (yantra-gen: /proc/self/exe, GetModuleFileNameW).

Each folder is looked for as written, with / and with \\ separators.
The stage is deleted afterwards (unless --keep). Exit 0 clean, 1 found,
2 usage or install error. --self-test checks the scanner on generated
files (a fake ELF and PE holding a folder, a text file, a clean file).
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


def is_binary(data: bytes) -> bool:
    """ELF (Linux .so/programs) or PE (Windows .dll/.exe)."""
    return data[:4] == b"\x7fELF" or data[:2] == b"MZ"


def deep_enough(folder: str) -> bool:
    """At least two levels ("/home/x", "C:\\Users\\x"): "/" or "/tmp"
    would match texts that are not this machine's."""
    return len([c for c in re.split(r"[\\/]", re.sub(r"^[A-Za-z]:", "", folder)) if c]) >= 2


def variants(text: str) -> list[str]:
    out = [text, text.replace("\\", "/"), text.replace("/", "\\")]
    return list(dict.fromkeys(t.rstrip("/\\") for t in out if t))


def home_folder() -> list[str]:
    home = os.environ.get("HOME") or os.environ.get("USERPROFILE") or ""
    home = os.path.normpath(home) if home else ""
    return [home] if home and deep_enough(home) else []


def scan(root: str, forbid: list[str], forbid_binary: list[str]) -> list[str]:
    """Findings "<file>: <text>" for every installed file below root."""
    folders = []
    for f in forbid:
        if f and deep_enough(f):
            folders += variants(f)
            real = os.path.realpath(f)
            if real != f and deep_enough(real):
                folders += variants(real)
    folders = list(dict.fromkeys(folders))
    texts = list(dict.fromkeys(v for t in forbid_binary if t for v in variants(t)))
    findings = []
    for d, _, names in os.walk(root):
        for n in sorted(names):
            path = os.path.join(d, n)
            if os.path.islink(path):
                continue
            with open(path, "rb") as fh:
                data = fh.read()
            rel = os.path.relpath(path, root).replace("\\", "/")
            for t in longest([t for t in folders if t.encode() in data]):
                findings.append(f"{rel}: holds {t}, a folder of the build machine")
            if is_binary(data):
                for t in longest([t for t in texts if t.encode() in data]):
                    findings.append(f"{rel}: holds {t}, an install folder compiled into a program")
    return findings


def longest(found: list[str]) -> list[str]:
    """The found texts without those inside another found one (C:\\x\\y
    and \\x\\y are one finding)."""
    return [t for t in found if not any(t != o and t in o for o in found)]


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        home = "/home/builder"
        os.makedirs(os.path.join(tmp, "bin"))
        os.makedirs(os.path.join(tmp, "share"))
        files = {
            "bin/elf": b"\x7fELF\x02\x01" + b"\0" * 10 + b"/usr/local/share/yantra-radios/scenarios\0",
            "bin/pe.exe": b"MZ\x90\0" + b"\0" * 10 + b"C:\\src\\tree\\data\0",
            "bin/clean": b"\x7fELF\x02\x01" + b"\0" * 10 + b"share/yantra-radios/scenarios\0",
            "share/doc.md": b"install to /usr/local/share/yantra-radios/scenarios; built in /home/builder/x\n",
        }
        for name, data in files.items():
            with open(os.path.join(tmp, name), "wb") as fh:
                fh.write(data)
        # The file and the kind of each finding: on Windows the folders are
        # also looked for as resolved there (with the drive, C:\\src\\tree).
        got = sorted(f.split(" holds ")[0] + f.split(",")[-1]
                     for f in scan(tmp, [home, "/src/tree", "/"], ["/usr/local/share/yantra-radios"]))
        want = sorted([
            "bin/elf: an install folder compiled into a program",
            "bin/pe.exe: a folder of the build machine",
            "share/doc.md: a folder of the build machine",
        ])
        if got != want:
            print("check_shipped_paths: self-test failed:\n  got  " + "\n       ".join(got)
                  + "\n  want " + "\n       ".join(want))
            return 1
        if not deep_enough("/home/x") or deep_enough("/tmp") or deep_enough("C:\\x"):
            print("check_shipped_paths: self-test failed: deep_enough")
            return 1
    print("check_shipped_paths: self-test OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--cmake", default="cmake")
    ap.add_argument("--build")
    ap.add_argument("--root")
    ap.add_argument("--forbid", action="append", default=[])
    ap.add_argument("--forbid-binary", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.build or not a.root:
        ap.error("--build and --root are required")
    stage = os.path.join(os.path.abspath(a.root), "stage")
    shutil.rmtree(stage, ignore_errors=True)
    env = {k: v for k, v in os.environ.items() if k != "DESTDIR"}
    r = subprocess.run([a.cmake, "--install", a.build, "--prefix", stage], env=env,
                       stdout=subprocess.DEVNULL)
    if r.returncode != 0:
        print(f"check_shipped_paths: cmake --install failed ({r.returncode})")
        return 2
    try:
        count = sum(len(n) for _, _, n in os.walk(stage))
        binaries = 0
        for d, _, names in os.walk(stage):
            for n in names:
                p = os.path.join(d, n)
                if not os.path.islink(p):
                    with open(p, "rb") as fh:
                        binaries += is_binary(fh.read(4))
        if binaries == 0:
            print("check_shipped_paths: no program or library installed")
            return 2
        findings = scan(stage, a.forbid + home_folder(), a.forbid_binary)
        for f in findings:
            print("check_shipped_paths:", f)
        print(f"check_shipped_paths: {count} files ({binaries} programs/libraries), {len(findings)} findings")
        return 1 if findings else 0
    finally:
        if not a.keep:
            shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
