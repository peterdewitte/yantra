#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest real_fixtures_selftest: the fixture checker on a yantra-gen
recording with two cells passes with a matching NAME.expected.json and
fails when the expected cells do not match (one cell missing). In a fresh
work folder that is deleted afterwards. Python, so the same test runs on
Linux and Windows.

Usage: fixtures_selftest.py YANTRA_GEN YANTRA_CHECK_FIXTURES WORK_DIR
"""

import json
import os
import shutil
import subprocess
import sys

CELL_A = {"downlink_hz": 390000000, "mcc": 901, "mnc": 4, "colour_code": 5, "location_area": 77}
CELL_B = {"downlink_hz": 390100000, "mcc": 901, "mnc": 4, "colour_code": 6, "location_area": 78}


def expected(path, cells):
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"format": "yantra-radios-fixture", "version": 1, "radio": "yantra-gen",
                   "complete": True, "cells": cells}, f)


def main():
    gen, check, work = sys.argv[1:4]
    root, folder = os.path.join(work, "root"), os.path.join(work, "root", "gen")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(folder)
    try:
        sys.stdout.flush()
        rc = subprocess.run([gen, "--cell", "id=A,carrier=3600,la=77,cc=5",
                             "--cell", "id=B,carrier=3604,la=78,cc=6,level=-75",
                             "--mcc", "901", "--mnc", "4", "--rate", "500000", "--duration", "4",
                             "--format", "cu8", "--quiet",
                             "--out", os.path.join(folder, "two.sigmf-data")]).returncode
        if rc != 0:
            return rc
        expected(os.path.join(folder, "two.expected.json"), [dict(CELL_A, air_encryption=False), CELL_B])
        sys.stdout.flush()
        rc = subprocess.run([check, root, os.path.join(work, "work")]).returncode
        if rc != 0:
            print(f"fixtures_selftest: the matching fixture failed ({rc})")
            return 1
        expected(os.path.join(folder, "two.expected.json"), [CELL_A])
        sys.stdout.flush()
        rc = subprocess.run([check, root, os.path.join(work, "work")]).returncode
        if rc == 0:
            print("fixtures_selftest: the fixture with a missing cell passed")
            return 1
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
