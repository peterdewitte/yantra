#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes tests/ctest_costs.txt from a build tree's ctest cost data.

The per-case doctest ctests (cmake/YantraTests.cmake) take their COST from
tests/ctest_costs.txt, so a fresh tree (CI) starts the longest tests first;
afterwards ctest uses its own measurements. Refresh the table after a test
got much longer or shorter:

    ctest --test-dir build -j"$(nproc)"
    YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -L opt_in -j"$(nproc)"
    tools/ci/test_costs.py build
"""

import pathlib
import sys

MIN_SECONDS = 1.0  # shorter ones need no head start

HEADER = """\
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Measured seconds of the per-case doctest ctests ("<test> <seconds>"), the
# COST of cmake/YantraTests.cmake (longest first in a fresh tree). Written by
# tools/ci/test_costs.py; tests below {min:.0f} s are left out.
"""


def main():
    build = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build")
    data = build / "Testing" / "Temporary" / "CTestCostData.txt"
    rows = []
    for line in data.read_text(encoding="utf-8").splitlines():
        if line.startswith("---"):  # the failed-tests section follows
            break
        parts = line.split()
        if len(parts) == 3 and "/" in parts[0] and float(parts[2]) >= MIN_SECONDS:
            rows.append((float(parts[2]), parts[0]))
    rows.sort(key=lambda r: (-r[0], r[1]))
    out = pathlib.Path(__file__).resolve().parents[2] / "tests" / "ctest_costs.txt"
    out.write_text(HEADER.format(min=MIN_SECONDS) + "".join(f"{n} {c:.1f}\n" for c, n in rows),
                   encoding="utf-8")
    print(f"{out}: {len(rows)} tests")


if __name__ == "__main__":
    main()
