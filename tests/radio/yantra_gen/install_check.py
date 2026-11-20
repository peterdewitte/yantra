#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest yantra_gen_install (RAD-71): `cmake --install` of the build tree
into a scratch prefix; the installed yantra-gen lists the scenarios
(fraunces among them) and runs one by id (a recording and its ground-truth
records), and the scenario scenes are installed. The installed program
finds the scenarios relative to itself, with no folder of the build
machine compiled in (NFR-09 of Yantra, RAD-137): without
$YANTRA_SCENARIO_DIR and from another working folder it lists the
installed folder; after the prefix is moved it lists the moved folder;
with the installed scenarios gone it finds none (exit 3). The prefix is
deleted afterwards. Python, so the same test runs on Linux and Windows
(the Windows test VM: the layout of the release .zip).

Usage: install_check.py CMAKE PREFIX BUILD_DIR YANTRA_GEN_FILE_NAME
"""

import os
import shutil
import tempfile
import subprocess
import sys


def norm(path):
    return os.path.normcase(os.path.normpath(os.path.abspath(path)))


def listed_folder(gen, cwd, env):
    """The scenario folder `gen --list` names (its "# <folder>" line), or
    None; with the exit code and stderr."""
    r = subprocess.run([gen, "--list"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                       cwd=cwd, env=env)
    first = r.stdout.splitlines()[0] if r.stdout else ""
    return (first[2:] if first.startswith("# ") else None), r.returncode, r.stderr.strip()


def relocation_checks(prefix, name):
    """The installed program uses only its own folder: no build, source or
    install path compiled in."""
    env = {k: v for k, v in os.environ.items() if k != "YANTRA_SCENARIO_DIR"}
    with tempfile.TemporaryDirectory() as elsewhere:
        want = norm(os.path.join(prefix, "share", "yantra-radios", "scenarios"))
        got, rc, err = listed_folder(os.path.join(prefix, "bin", name), elsewhere, env)
        if rc != 0 or got is None or norm(got) != want:
            print(f"install_check: --list from another folder: exit {rc}, folder {got} (want {want}) {err}")
            return 1
        moved = prefix + "_moved"
        shutil.rmtree(moved, ignore_errors=True)
        os.rename(prefix, moved)
        try:
            want = norm(os.path.join(moved, "share", "yantra-radios", "scenarios"))
            gen = os.path.join(moved, "bin", name)
            got, rc, err = listed_folder(gen, elsewhere, env)
            if rc != 0 or got is None or norm(got) != want:
                print(f"install_check: --list after moving the prefix: exit {rc}, folder {got} (want {want}) {err}")
                return 1
            shutil.rmtree(os.path.join(moved, "share", "yantra-radios", "scenarios"))
            got, rc, err = listed_folder(gen, elsewhere, env)
            if rc != 3 or got is not None:
                print(f"install_check: without the installed scenarios --list found {got} (exit {rc}): "
                      "a folder is compiled in")
                return 1
        finally:
            shutil.rmtree(moved, ignore_errors=True)
    return 0


def main():
    cmake, prefix, build, name = sys.argv[1:5]
    prefix = os.path.abspath(prefix)
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
        return relocation_checks(prefix, name)
    finally:
        shutil.rmtree(prefix, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
