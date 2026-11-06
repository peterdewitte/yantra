#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a command on a private Xvfb display (launch tests NFR-10, TST-04;
# screenshots SCR-01). Xvfb picks a free display itself (-displayfd), so
# parallel ctest runs do not collide, and it is stopped afterwards. It
# listens on an abstract socket only ("local"): under WSL /tmp/.X11-unix
# is a read-only mount of WSLg, where Xvfb cannot create its socket file.
# Exit code 77 (skipped) when Xvfb is not installed.
#
#   with_xvfb.sh [--size WxHxD] command [args...]
set -u
size=1280x800x24
if [ "${1:-}" = "--size" ]; then
    size=$2
    shift 2
fi
if ! command -v Xvfb >/dev/null 2>&1; then
    echo "SKIPPED: Xvfb not found"
    exit 77
fi
dir=$(mktemp -d "${TMPDIR:-/tmp}/yantra-xvfb.XXXXXX") || exit 1
mkfifo "$dir/displayfd" || { rm -rf "$dir"; exit 1; }
Xvfb -displayfd 3 -screen 0 "$size" -nolisten tcp -nolisten unix -listen local -noreset 3>"$dir/displayfd" 2>"$dir/xvfb.log" &
xvfb=$!
display=""
read -r display <"$dir/displayfd"
if [ -z "$display" ]; then
    echo "with_xvfb.sh: Xvfb did not start:" >&2
    cat "$dir/xvfb.log" >&2
    wait "$xvfb" 2>/dev/null
    rm -rf "$dir"
    exit 1
fi
DISPLAY=":$display"
export DISPLAY
# The private display only: no Wayland compositor of the session.
unset WAYLAND_DISPLAY
"$@"
rc=$?
kill "$xvfb" 2>/dev/null
wait "$xvfb" 2>/dev/null
rm -rf "$dir"
exit $rc
