#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the Linux steps of ci.yml on a developer machine (RAD-134): the
# static dependency build, configure with warnings as errors, build, fast
# tests (timing tests retried once), the completeness check and the lint.
# Usage: tools/ci/run_linux.sh [build-dir] [extra cmake args...]
# Copied from yantra-v1 c24e8f5: tools/ci/run_linux.sh, adapted for RAD-134.
set -euo pipefail
cd "$(dirname "$0")/../.."
build=${1:-build-ci}
shift || true
jobs=$(nproc)
tools/deps/build_deps.sh "$build/deps"
cmake -S . -B "$build" -G Ninja -DYANTRA_WERROR=ON "$@"
cmake --build "$build" -j "$jobs"
ctest --test-dir "$build" -LE "slow|timing" --timeout 600 --output-on-failure -j "$jobs"
ctest --test-dir "$build" -L timing -LE slow --timeout 600 --repeat until-pass:2 --output-on-failure
python3 tools/ci/check_completeness.py "$build/bin/plugins"
tools/ci/lint.sh
reuse lint
