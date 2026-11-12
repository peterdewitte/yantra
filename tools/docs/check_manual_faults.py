#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The PDF manual checker's own test (DOC-20, DOC-02a): a chapter with a
missing picture, broken links, text past the margin and out of a table
cell, and an index entry the text never names must fail with each of them
named (the fixture is .md.txt so the repository's link checks leave its
broken links alone). usage: check_manual_faults.py SOURCE_DIR OUT_DIR
Adapted from yantra-radios 9c6d3c9: tests/docs/check_manual_faults.py."""
import subprocess
import sys

source, out = sys.argv[1], sys.argv[2]
p = subprocess.run([sys.executable, "-B", f"{source}/tools/docs/build_manual.py", "--source", source, "--out", out,
                    "--version", "0.0.0", "--chapter", "tools/docs/manual_faults/faults.md.txt:faults",
                    "--index", f"{source}/tools/docs/manual_faults/index.txt"],
                   capture_output=True, text=True)
if p.returncode == 77:
    print(p.stdout.strip())
    sys.exit(77)
text = p.stdout + p.stderr
want = ["image no-such-picture.png is missing", "link to missing no-such-file.md",
        "#faults--no-such-heading: no such heading", "internal link to #faults--no-such-heading does not resolve",
        "runs past the page margin", "runs out of its table cell", "2 images in the chapters, 1 in the layout",
        "index: 'Waveguide' is not found"]
missing = [w for w in want if w not in text]
if p.returncode != 1 or missing:
    print(text)
    print(f"check_manual_faults: exit {p.returncode} (want 1); not reported: {missing}")
    sys.exit(1)
if "#faults--12-a-broken-link" in text or "faults--11-a-missing-picture does not resolve" in text:
    print(text)
    print("check_manual_faults: a working section reference was reported")
    sys.exit(1)
print("check_manual_faults: every fault reported")
