<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Boundary lint fixture

A small tree with deliberate radio boundary violations (PLG-30) for the
self-test of `tools/ci/lint_boundary.py` (ctest `boundary_lint_selftest`):
`tree/` holds the sources, `targets.tsv` the link dependencies in the
format of `<build>/boundary/targets.tsv`, and `expected.txt` the findings
the lint must report, no more and no fewer. Nothing here is compiled; the
real lint skips this folder.
