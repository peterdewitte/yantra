#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes one ctest per doctest test case (TST-01).

Copied from yantra-radios 6fac3ee: cmake/doctest_discover.py.

Run after a doctest binary is linked (cmake/YantraTests.cmake,
yantra_doctest_tests()). It lists the test cases that pass the group's
doctest filters (the same filters the single ctest of the group used) and
writes a CMake file that ctest includes: one add_test() per case, which
runs the binary with the group's filters plus --test-case=<that case>.
The union of these ctests is exactly the group: the same cases and the
same assertions, only in parallel.

With --emulator (repeatable: the program and its arguments, the build's
CMAKE_CROSSCOMPILING_EMULATOR, e.g. tools/sdk/wsl_run.sh --in-place for
the Windows tree) the binary is listed and every ctest is run through it.
When the emulator cannot run the binary here (exit code 77: a Linux CI
runner without Windows), the group becomes one ctest that runs the whole
binary with the group's filters: the same cases, not split.

A cross-compiled tree without an emulator (the Windows tree, whose ctest
runs on Windows) writes that one group ctest at build time (--group-only)
and lists the cases when ctest reads the tests on the target
(--at-test-time, run from the group's ctest include file): where the
binary cannot run (not Windows) it writes nothing and exits 77, so ctest
keeps the group ctest.

Usage: doctest_discover.py --exe EXE --group NAME --out FILE
           [--labels L1,L2] [--timeout S] [--run-serial] [--workdir DIR]
           [--costs FILE] [--emulator ARG]... [--group-only | --at-test-time]
           [-- doctest filter arguments...]
"""

import argparse
import fnmatch
import os
import re
import subprocess
import sys


def list_cases(exe, filters, emulator):
    """Test case names passing the filters, in doctest's order; None when
    the emulator cannot run the binary (exit code 77)."""
    proc = subprocess.run([*emulator, exe, *filters, "--list-test-cases", "--no-colors=true"],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if emulator and proc.returncode == 77:
        return None
    if proc.returncode != 0:
        sys.exit(f"doctest_discover: {exe} --list-test-cases failed ({proc.returncode}):\n{proc.stderr}")
    lines = proc.stdout.splitlines()
    rules = [i for i, line in enumerate(lines) if re.fullmatch(r"=+", line)]
    if len(rules) < 2:
        sys.exit(f"doctest_discover: unexpected --list-test-cases output of {exe}:\n{proc.stdout}")
    names = lines[rules[0] + 1:rules[1]]
    m = re.search(r"test cases passing the current filters: (\d+)", proc.stdout)
    if not m or int(m.group(1)) != len(names):
        sys.exit(f"doctest_discover: case count of {exe} does not match its list:\n{proc.stdout}")
    return names


def doctest_filter(name):
    """--test-case value matching exactly this name (commas and
    backslashes escaped; doctest's * and ? are checked by the caller)."""
    return name.replace("\\", "\\\\").replace(",", "\\,")


def slug(name):
    s = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").lower()
    return s[:72].rstrip("_") or "case"


def bracket(text):
    """CMake bracket argument that holds any text literally."""
    eq = "="
    while f"]{eq}]" in text:
        eq += "="
    return f"[{eq}[{text}]{eq}]"


def read_costs(path):
    costs = {}
    if path:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    name, cost = line.rsplit(None, 1)
                    costs[name] = cost
    return costs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--group", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--labels", default="")
    ap.add_argument("--timeout", default="")
    ap.add_argument("--run-serial", action="store_true")
    ap.add_argument("--processors", default="")
    ap.add_argument("--workdir", default="")
    ap.add_argument("--costs", default="")
    ap.add_argument("--emulator", action="append", default=[])
    ap.add_argument("--group-only", action="store_true")
    ap.add_argument("--at-test-time", action="store_true")
    ap.add_argument("filters", nargs="*")
    a = ap.parse_args()

    if a.group_only:
        write_group(a, "Listed when ctest runs on the target (--at-test-time)")
        return
    if a.at_test_time and a.exe.lower().endswith(".exe") and os.name != "nt":
        sys.exit(77)  # a Windows binary, not runnable here: keep the group ctest
    listed = list_cases(a.exe, a.filters, a.emulator)
    if listed is None:
        write_group(a)
        return
    names = list(dict.fromkeys(listed))  # a duplicate name runs once, all copies
    if not names:
        sys.exit(f"doctest_discover: group {a.group} of {a.exe} has no test case")
    # doctest filters are wildcards (* and ?) and case-insensitive: every
    # per-case filter must match its own case only, or a case would run twice.
    for n in names:
        pat = n.lower().replace("[", "[[]")
        hits = [o for o in names if fnmatch.fnmatchcase(o.lower(), pat)]
        if hits != [n]:
            sys.exit(f"doctest_discover: the filter of test case \"{n}\" also matches {hits[1:] or hits}; "
                     "rename one of them")
    costs = read_costs(a.costs)

    out = [f"# Generated by cmake/doctest_discover.py from {a.exe}: do not edit.\n"]
    used = set()
    for n in names:
        base = f"{a.group}/{slug(n)}"
        test, k = base, 2
        while test in used:
            test, k = f"{base}_{k}", k + 1
        used.add(test)
        args = " ".join(bracket(x) for x in [*a.emulator, a.exe, *a.filters, "--test-case=" + doctest_filter(n)])
        out.append(f"add_test({bracket(test)} {args})\n")
        props = properties(a)
        if test in costs:
            props.append(f"COST {costs[test]}")
        if props:
            out.append(f"set_tests_properties({bracket(test)} PROPERTIES {' '.join(props)})\n")
    write_if_changed(a.out, "".join(out))


def properties(a):
    """The ctest properties every test of the group gets."""
    props = []
    if a.labels:
        props.append(f"LABELS {bracket(a.labels.replace(',', ';'))}")
    if a.timeout:
        props.append(f"TIMEOUT {a.timeout}")
    if a.run_serial:
        props.append("RUN_SERIAL TRUE")
    if a.processors:
        props.append(f"PROCESSORS {a.processors}")
    if a.workdir:
        props.append(f"WORKING_DIRECTORY {bracket(a.workdir)}")
    return props


def write_group(a, why="Not listed (the emulator could not run it here)"):
    """One ctest for the whole group (the binary could not be listed)."""
    test = f"{a.group}/all"
    args = " ".join(bracket(x) for x in [*a.emulator, a.exe, *a.filters])
    out = [f"# Generated by cmake/doctest_discover.py from {a.exe}: do not edit.\n",
           f"# {why}: one test for the group.\n",
           f"add_test({bracket(test)} {args})\n"]
    props = properties(a)
    if props:
        out.append(f"set_tests_properties({bracket(test)} PROPERTIES {' '.join(props)})\n")
    write_if_changed(a.out, "".join(out))


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


if __name__ == "__main__":
    main()
