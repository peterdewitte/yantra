#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lint job of ci.yml (TST-02), also run by ctest (repo_lint):
#   - shell scripts: sh -n / bash -n (by shebang)
#   - Python: py_compile (into a temporary folder, the tree stays clean)
#   - YAML (CI workflows): parsed with Python's yaml module
# The FFI width check of TST-02 joins once Yantra has a Rust<->C++ FFI
# (none yet). reuse lint runs as its own ctest and CI step. third_party/
# and radios/third_party/ (vendored code) are skipped; the radio parts in
# radios/ (PLG-29) are linted with the rest.
# Usage: tools/ci/lint.sh [source-dir]   (default: the repository root)
# Environment: YANTRA_LINT_REQUIRE_TOOLS=1 makes a missing tool (Python
# yaml) an error (CI).
# Copied from yantra-radios fb194f5: tools/ci/lint.sh, adapted (no Rust
# crate yet).
set -eu
root=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$root"
fail=0
require=${YANTRA_LINT_REQUIRE_TOOLS:-0}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

missing() {
    echo "lint: $1 missing, $2 not checked"
    if [ "$require" = 1 ]; then fail=1; fi
}

list() {
    find . \( -path ./.git -o -path './build*' -o -path ./third_party -o -path ./radios/third_party \
              -o -path './radios/build*' -o -path ./radios/sdk -o -path ./radios/sdk-cache \
              -o -name target -o -path ./out \) -prune -o \
           -type f \( "$@" \) -print | sort
}

for f in $(list -name '*.sh'); do
    case $(head -n 1 "$f") in
        *bash*) bash -n "$f" || { echo "lint: shell syntax error in $f"; fail=1; } ;;
        *)      sh -n "$f"   || { echo "lint: shell syntax error in $f"; fail=1; } ;;
    esac
done

for f in $(list -name '*.py'); do
    python3 -c 'import py_compile, sys; py_compile.compile(sys.argv[1], cfile=sys.argv[2], doraise=True)' \
        "$f" "$tmp/x.pyc" || { echo "lint: Python syntax error in $f"; fail=1; }
done

yaml_files=$(list -name '*.yml' -o -name '*.yaml')
if [ -n "$yaml_files" ]; then
    if python3 -c 'import yaml' 2>/dev/null; then
        for f in $yaml_files; do
            python3 -c 'import sys, yaml; yaml.safe_load(open(sys.argv[1], encoding="utf-8"))' "$f" \
                || { echo "lint: YAML error in $f"; fail=1; }
        done
    else
        missing "Python yaml module" "YAML files"
    fi
fi

[ $fail -eq 0 ] && echo "lint: OK"
exit $fail
