#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a command and checks that it succeeds and that its output (stdout
# and stderr) has a line matching each pattern (grep -E). Used by the app
# launch tests instead of one multi-line PASS_REGULAR_EXPRESSION: ctest's
# regex engine backtracks recursively and crashed (stack overflow) on a
# "(.|\n)*" pattern over the long driver-module output of a sanitizer build.
# Exit code 77 (skipped) is passed through.
#
#   expect_output.sh PATTERN... -- command [args...]
set -u
n=0
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    n=$((n + 1))
    eval "pattern_$n=\$1"
    shift
done
[ $# -gt 0 ] || { echo "expect_output.sh: missing -- command" >&2; exit 2; }
shift
out=$(mktemp "${TMPDIR:-/tmp}/yantra-expect.XXXXXX") || exit 1
"$@" >"$out" 2>&1
rc=$?
if [ $rc -eq 77 ]; then
    cat "$out"
    rm -f "$out"
    exit 77
fi
status=0
[ $rc -eq 0 ] || { echo "expect_output.sh: the command exited with $rc"; status=1; }
i=1
while [ $i -le $n ]; do
    eval "p=\$pattern_$i"
    if ! grep -E -q -- "$p" "$out"; then
        echo "expect_output.sh: no line matches: $p"
        status=1
    fi
    i=$((i + 1))
done
if [ $status -ne 0 ]; then
    echo "--- output (last 40 lines) ---"
    tail -n 40 "$out"
fi
rm -f "$out"
exit $status
