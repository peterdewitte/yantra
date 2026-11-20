#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the Windows tree's tests natively on Windows (RAD-131, RAD-134).

The Windows tree is cross-compiled on Linux (preset "windows"); its ctest
runs natively on a Windows machine that mirrors the tree to the same paths
and has cmake and python3 there (the test VM, BUILD.md), or on WSL2 through
tools/sdk/wsl_run.sh (YANTRA_WINDOWS_TEST_RUNNER=wsl). A Windows machine
without those (the CI's Windows runner) runs the tree's test list with this
script instead (`ctest --show-only=json-v1`, written on Linux, --tests):
every test whose command is a Windows program runs (an emulator in the
command is dropped), the .exe with its arguments, its
working folder, its ENVIRONMENT, its TIMEOUT, PASS/FAIL_REGULAR_EXPRESSION,
WILL_FAIL and SKIP_RETURN_CODE, RUN_SERIAL tests after the others. Tests
that run a tool (Python or CMake scripts) ran in the cross job and are
counted as "not here". A doctest binary listed on Linux is one test for
its group (<group>/all, cmake/doctest_discover.py).

The paths in the list and inside the test programs are the Linux paths of
the cross job (/home/runner/work/...). Windows reads a rooted path on the
drive of the current folder, so the runner links that path on its drive
to the checkout (a junction, see .github/workflows/ci.yml) and this script
runs from there.

Usage: run_windows_tests.py --tests tests.json [-j N] [--emulator ARG]...
       (--emulator: a program and its arguments put before each .exe, for
       a local run on Linux: --emulator tools/sdk/wsl_run.sh
       --emulator=--in-place)
Exit code: 0 when every test that ran passed (or was skipped), 1 otherwise.
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time


def prop(test, name, default=None):
    for p in test.get("properties", []):
        if p["name"] == name:
            return p["value"]
    return default


def windows_command(test):
    """The command without the emulator, or None for a Linux tool."""
    cmd = list(test.get("command") or [])
    if cmd and cmd[0].endswith("wsl_run.sh"):
        cmd = cmd[1:]
        while cmd and cmd[0] in ("--in-place",):
            cmd = cmd[1:]
        while cmd and cmd[0] == "--with":
            cmd = cmd[2:]
    if cmd and cmd[0].lower().endswith(".exe"):
        return cmd
    return None


def run_one(test, emulator):
    cmd = emulator + windows_command(test)
    env = dict(os.environ)
    for kv in prop(test, "ENVIRONMENT", []) or []:
        k, _, v = kv.partition("=")
        env[k] = v
    timeout = float(prop(test, "TIMEOUT", 1500) or 1500)
    start = time.monotonic()
    try:
        proc = subprocess.run(cmd, cwd=prop(test, "WORKING_DIRECTORY") or None, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        # As ctest: line ends as \n; a CMake regex "." also matches \n.
        out, code = proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), proc.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b"").decode("utf-8", "replace")
        return "failed", f"timeout after {timeout:.0f} s", out, time.monotonic() - start
    except OSError as e:
        return "failed", f"cannot run: {e}", "", time.monotonic() - start
    seconds = time.monotonic() - start
    skip = prop(test, "SKIP_RETURN_CODE")
    if skip is not None and code == int(skip):
        return "skipped", f"exit code {code}", out, seconds
    passing = prop(test, "PASS_REGULAR_EXPRESSION")
    failing = prop(test, "FAIL_REGULAR_EXPRESSION")
    if passing:
        ok = any(re.search(r, out, re.DOTALL) for r in passing)
        why = "" if ok else "PASS_REGULAR_EXPRESSION not found"
    else:
        ok = code == 0
        why = "" if ok else f"exit code {code}"
    if failing and any(re.search(r, out, re.DOTALL) for r in failing):
        ok, why = False, "FAIL_REGULAR_EXPRESSION found"
    if prop(test, "WILL_FAIL"):
        ok, why = not ok, ("" if not ok else "passed but WILL_FAIL")
    return ("passed" if ok else "failed"), why, out, seconds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tests", required=True, help="ctest --show-only=json-v1 of the Windows tree")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--emulator", action="append", default=[])
    ap.add_argument("-R", default="", help="only tests whose name matches")
    a = ap.parse_args()
    if a.emulator and os.path.exists(a.emulator[0]):
        a.emulator[0] = os.path.abspath(a.emulator[0])  # the tests run in their own folders
    with open(a.tests, encoding="utf-8") as f:
        tests = json.load(f)["tests"]
    runnable = [t for t in tests if windows_command(t) and re.search(a.R, t["name"])]
    elsewhere = [t["name"] for t in tests if not windows_command(t)]
    parallel = [t for t in runnable if not prop(t, "RUN_SERIAL")]
    serial = [t for t in runnable if prop(t, "RUN_SERIAL")]
    results = {}

    def report(test, result):
        status, why, out, seconds = result
        results[test["name"]] = status
        print(f"{status:>7} {seconds:7.2f} s  {test['name']}" + (f"  ({why})" if why else ""), flush=True)
        if status == "failed":
            print(out[-6000:], flush=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.j)) as pool:
        futures = {pool.submit(run_one, t, a.emulator): t for t in parallel}
        for fut in concurrent.futures.as_completed(futures):
            report(futures[fut], fut.result())
    for t in serial:
        report(t, run_one(t, a.emulator))

    counts = {s: sum(1 for v in results.values() if v == s) for s in ("passed", "skipped", "failed")}
    print(f"run_windows_tests: {counts['passed']} passed, {counts['skipped']} skipped, "
          f"{counts['failed']} failed of {len(runnable)} Windows tests; "
          f"{len(elsewhere)} tests run a Linux tool (not here)")
    for name, status in sorted(results.items()):
        if status == "failed":
            print(f"  failed: {name}")
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
