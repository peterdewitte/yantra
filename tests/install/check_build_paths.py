#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that no installed file names a folder of the build machine (NFR-09).

Scans every file under ROOT (the DESTDIR that install_destdir filled) for
the source folder, the build folder and the home folder of the user
running the test when it is a person's (at least two levels deep, so not
"/" or "/tmp"). Binaries are built with -ffile-prefix-map (top-level
CMakeLists.txt), so debug information and __FILE__ hold relative paths.
Prints each finding with the file and how often it occurs; exit 1 on any.
As the radio parts tools/ci/check_completeness.py (build_paths).
"""
import argparse
import os
import sys


def folders(source: str, build: str) -> list:
    """(text, label) pairs, longest first so a finding names the most
    specific folder."""
    out = {os.path.normpath(os.path.abspath(build)): "build folder",
           os.path.normpath(os.path.abspath(source)): "source folder"}
    home = os.path.normpath(os.environ.get("HOME") or "/")
    if len([c for c in home.split("/") if c]) >= 2:
        out.setdefault(home, "home folder")
    return sorted(out.items(), key=lambda kv: len(kv[0]), reverse=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, help="the installed tree (DESTDIR)")
    ap.add_argument("--source", required=True, help="the source folder")
    ap.add_argument("--build", required=True, help="the build folder")
    a = ap.parse_args()
    forbid = folders(a.source, a.build)
    problems = 0
    count = 0
    for d, _, names in os.walk(a.root):
        for n in names:
            path = os.path.join(d, n)
            if os.path.islink(path):
                continue
            count += 1
            with open(path, "rb") as f:
                data = f.read()
            for t, label in forbid:
                hits = data.count(t.encode())
                if hits:
                    problems += 1
                    print(f"check_build_paths: {os.path.relpath(path, a.root)} holds the {label} "
                          f"{hits} time(s)")
                    break
    print(f"check_build_paths: {count} files, {problems} with build machine folders")
    if count == 0:
        print("check_build_paths: nothing installed under", a.root)
        return 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
