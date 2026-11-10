#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs one test command for ctest, on Linux and on Windows (RAD-131).

The command-line tests that need more than "run this program" (a fresh
folder, a server next to the command, an exit code other than 0, a byte
count of the output) use this script instead of a shell, so the same
ctest runs in the Linux trees and natively in the Windows tree (whose
ctest has no shell, BUILD.md).

Usage: test_command.py [options] -- PROGRAM [ARGS...]
  --mkdir DIR         make DIR (and its parents) first
  --remove FILE       delete FILE first if it is there
  --fresh DIR         empty DIR first (delete and make it) and delete it
                      afterwards
  --server DIR PROG   start PROG DIR/url (the loopback SoapySDRServer of
                      tests/soapy_remote), wait until it wrote its URL to
                      DIR/url, run the command with YANTRA_SOAPY_REMOTE set
                      to it, then stop the server
  --exit-code N       pass when the command exits with N (and print it);
                      default: the command's own exit code
  --quiet-stderr      drop the command's standard error
  --count-stdout      print the number of bytes the command wrote to
                      standard output instead of the bytes
  --limit N           with --count-stdout: read at most N bytes, then close
                      the pipe (the command must then end by itself, as
                      after `| head -c N`)
Exit code: the command's (or 0/1 with --exit-code); 1 on a setup error.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time


def start_server(folder, program):
    os.makedirs(folder, exist_ok=True)
    url_file = os.path.join(folder, "url")
    if os.path.exists(url_file):
        os.remove(url_file)
    server = subprocess.Popen([program, url_file])
    deadline = time.monotonic() + 60
    while not (os.path.exists(url_file) and os.path.getsize(url_file) > 0):
        if server.poll() is not None or time.monotonic() > deadline:
            if server.poll() is None:
                server.kill()
                server.wait()
            sys.exit("test_command.py: the server did not start")
        time.sleep(0.1)
    with open(url_file, encoding="utf-8") as f:
        return server, f.read().strip()


def count_stdout(cmd, limit, env):
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, env=env)
    total = 0
    while limit is None or total < limit:
        chunk = proc.stdout.read(min(1 << 16, limit - total) if limit is not None else 1 << 16)
        if not chunk:
            break
        total += len(chunk)
    proc.stdout.close()  # the writer sees a broken pipe and ends
    rc = proc.wait()
    print(total)
    return 0 if limit is not None else rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mkdir", action="append", default=[])
    ap.add_argument("--remove", action="append", default=[])
    ap.add_argument("--fresh", action="append", default=[])
    ap.add_argument("--server", nargs=2, metavar=("DIR", "PROG"))
    ap.add_argument("--exit-code", type=int)
    ap.add_argument("--quiet-stderr", action="store_true")
    ap.add_argument("--count-stdout", action="store_true")
    ap.add_argument("--limit", type=int)
    ap.add_argument("command", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    sys.stdout.reconfigure(newline="\n")  # the same output on Windows (ctest regexes)
    cmd = a.command[1:] if a.command[:1] == ["--"] else a.command
    if not cmd:
        ap.error("no command after --")

    for d in a.fresh:
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    for d in a.mkdir:
        os.makedirs(d, exist_ok=True)
    for f in a.remove:
        if os.path.exists(f):
            os.remove(f)
    env = dict(os.environ)
    server = None
    try:
        if a.server:
            server, env["YANTRA_SOAPY_REMOTE"] = start_server(*a.server)
        sys.stdout.flush()
        if a.count_stdout:
            rc = count_stdout(cmd, a.limit, env)
        else:
            rc = subprocess.run(cmd, env=env,
                                stderr=subprocess.DEVNULL if a.quiet_stderr else None).returncode
    finally:
        if server is not None:
            server.terminate()
            server.wait()
            url_file = os.path.join(a.server[0], "url")
            if os.path.exists(url_file):
                os.remove(url_file)
        for d in a.fresh:
            shutil.rmtree(d, ignore_errors=True)
    if a.exit_code is not None:
        print(f"exit code {rc} (expected {a.exit_code})")
        return 0 if rc == a.exit_code else 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
