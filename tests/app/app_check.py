#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs one of the app's launch and command-line tests (NFR-10, TST-04,
# SCR-01, UI-16) without a shell, on Linux and Windows (HW-06): prepares
# files, runs the command with extra environment variables, then checks
# its exit code, its output and files. Exit 77 of the command (no display
# on Linux: with_xvfb.sh) is passed on as a skip.
#
#   app_check.py [--env NAME=VALUE]... [--fresh-copy SRC DST]... [--write FILE TEXT]...
#                [--expect REGEX]... [--exit-code N] [--absent FILE]... [--exists-glob PATTERN]...
#                [--wrap PROGRAM] -- COMMAND [ARGS...]
#
# --expect: a line of the combined output (stdout and stderr) must match
# (re.search, multi-line); --exit-code: the command's exit code (0).
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys


def main():
    if "--" not in sys.argv:
        sys.exit("app_check.py: missing -- COMMAND")
    split = sys.argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--fresh-copy", nargs=2, action="append", default=[])
    ap.add_argument("--write", nargs=2, action="append", default=[])
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--exit-code", type=int, default=0)
    ap.add_argument("--absent", action="append", default=[])
    ap.add_argument("--exists-glob", action="append", default=[])
    ap.add_argument("--wrap")
    a = ap.parse_args(sys.argv[1:split])
    command = sys.argv[split + 1:]
    if not command:
        sys.exit("app_check.py: empty command")
    for src, dst in a.fresh_copy:
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst)
    for path, text in a.write:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text + "\n")
    env = dict(os.environ)
    for item in a.env:
        name, _, value = item.partition("=")
        env[name] = value
    if a.wrap and os.name != "nt":
        command = [a.wrap] + command
    proc = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
    if proc.returncode == 77 and a.exit_code != 77:
        sys.stdout.write(out)
        return 77
    problems = []
    if proc.returncode != a.exit_code:
        problems.append(f"the command exited with {proc.returncode}, expected {a.exit_code}")
    for pattern in a.expect:
        if not re.search(pattern, out, re.MULTILINE):
            problems.append(f"no line matches: {pattern}")
    for path in a.absent:
        if os.path.exists(path):
            problems.append(f"still there: {path}")
    for pattern in a.exists_glob:
        if not glob.glob(pattern):
            problems.append(f"no file matches: {pattern}")
    if problems:
        for p in problems:
            print(f"app_check.py: {p}")
        print("--- output (last 40 lines) ---")
        print("\n".join(out.splitlines()[-40:]))
        return 1
    sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
