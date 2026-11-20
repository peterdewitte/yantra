#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The manual checker's own test (RAD-97): a chapter with a missing
picture, broken links and text past the margin must fail with each of
them named (the fixture is .md.txt so the repository's link check
leaves its broken links alone). usage: check_manual_faults.py SOURCE_DIR OUT_DIR"""
import subprocess
import sys

source, out = sys.argv[1], sys.argv[2]
p = subprocess.run([sys.executable, "-B", f"{source}/tools/docs/build_radio_manual.py", "--source", source, "--out", out,
                    "--version", "0.0.0", "--chapter", "tests/radio/docs/manual_faults/faults.md.txt:faults"],
                   capture_output=True, text=True)
if p.returncode == 77:
    print(p.stdout.strip())
    sys.exit(77)
text = p.stdout + p.stderr
want = ["image no-such-picture.png is missing", "link to missing no-such-file.md",
        "#faults--no-such-heading: no such heading", "internal link to #faults--no-such-heading does not resolve",
        "runs past the page margin", "1 images in the chapters, 0 in the layout"]
missing = [w for w in want if w not in text]
if p.returncode != 1 or missing:
    print(text)
    print(f"check_manual_faults: exit {p.returncode} (want 1); not reported: {missing}")
    sys.exit(1)
print("check_manual_faults: every fault reported")
