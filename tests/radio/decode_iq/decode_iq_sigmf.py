#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest host_cli_decode_iq_sigmf (RAD-63): yantra-gen writes a SigMF
recording, yantra-radio-host decode-iq decodes it into a record file and
reads that back; all output goes to ctest's regular expression. In a fresh
work folder that is deleted afterwards. Python, so the same test runs on
Linux and Windows.

Usage: decode_iq_sigmf.py YANTRA_GEN YANTRA_RADIO_HOST WORK_DIR
"""

import os
import shutil
import subprocess
import sys


def main():
    gen, host, work = sys.argv[1:4]
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    try:
        for cmd in ([gen, "--cell", "carrier=3600", "--duration", "3", "--format", "cu8",
                     "--full-scale-dbm", "-50", "--quiet", "--out", f"{work}/x.sigmf-data"],
                    [host, "decode-iq", "--iq", f"{work}/x.sigmf-meta", "--format", "sigmf",
                     "--out", f"{work}/x.dec.jsonl"],
                    [host, "read", f"{work}/x.dec.jsonl"]):
            sys.stdout.flush()
            rc = subprocess.run(cmd).returncode
            if rc != 0:
                return rc
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
