#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest yantra_gen_install (RAD-71): `cmake --install` of the build tree
into a scratch prefix; the installed yantra-gen lists the scenarios
(fraunces among them) and runs one by id (a recording and its ground-truth
records), and the scenario scenes are installed. The prefix is deleted
afterwards. Python, so the same test runs on Linux and Windows.

Usage: install_check.py CMAKE PREFIX BUILD_DIR YANTRA_GEN_FILE_NAME
"""

import os
import shutil
import subprocess
import sys


def main():
    cmake, prefix, build, name = sys.argv[1:5]
    shutil.rmtree(prefix, ignore_errors=True)
    try:
        rc = subprocess.run([cmake, "--install", build, "--prefix", prefix],
                            stdout=subprocess.DEVNULL).returncode
        if rc != 0:
            print(f"install_check: cmake --install failed ({rc})")
            return 1
        gen = os.path.join(prefix, "bin", name)
        listed = subprocess.run([gen, "--list"], stdout=subprocess.PIPE, text=True)
        if listed.returncode != 0 or not any(l.startswith("fraunces") for l in listed.stdout.splitlines()):
            print(f"install_check: {gen} --list does not list fraunces ({listed.returncode})")
            return 1
        rc = subprocess.run([gen, "--scenario", "fraunces", "--duration", "0.05", "--quiet",
                             "--out", os.path.join(prefix, "run.cs8"), "--format", "cs8"]).returncode
        if rc != 0:
            print(f"install_check: the installed yantra-gen failed ({rc})")
            return 1
        records = os.path.join(prefix, "run.records.jsonl")
        if not (os.path.isfile(records) and os.path.getsize(records) > 0):
            print("install_check: no ground-truth records")
            return 1
        if not os.path.isfile(os.path.join(prefix, "share", "yantra-radios", "scenarios", "scenes",
                                           "demo_office_f0.json")):
            print("install_check: the scenario scenes are not installed")
            return 1
        return 0
    finally:
        shutil.rmtree(prefix, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
