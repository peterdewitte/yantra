#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs a build-machine check of a cross-compiled tree (RAD-131).

The Windows tree is built on Linux and its ctest runs on Windows (the test
VM, BUILD.md). A few of its tests check the build machine or run its tools
(the cross SDK's compilers and lock check, cargo, Doxygen, reuse, the
dependency and release scripts): they test nothing of the Windows
programs and cannot run on Windows. Their command goes through this
script (yantra_build_host_tests() in the top-level CMakeLists.txt, label
build_host): on Windows it reports the test as skipped (exit code 77,
SKIP_RETURN_CODE); on the build machine it runs the command, so
`ctest --test-dir build-win -L build_host` runs them there.

Usage: build_host_test.py PROGRAM [ARGS...]
"""

import os
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: build_host_test.py PROGRAM [ARGS...]")
    if os.name == "nt":
        print("skipped on Windows: a check of the build machine and its tools "
              "(run it there: ctest --test-dir <tree> -L build_host)")
        sys.exit(77)
    sys.stdout.flush()
    os.execvp(sys.argv[1], sys.argv[1:])


if __name__ == "__main__":
    main()
