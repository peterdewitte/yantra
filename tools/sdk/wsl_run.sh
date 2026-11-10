#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a cross-compiled Windows program on this PC's Windows from WSL2
# through Windows interop (RAD-131, RAD-139): copies the .exe (and the
# files of --with folders, e.g. DLLs) into a new scratch folder under the
# Windows user's %TEMP%, runs it there with the given arguments, passes on
# its output and exit code, and deletes the scratch folder (also on
# Ctrl-C). Windows programs on the WSL file system (\\wsl$ paths) start
# slowly and some refuse to run, hence the copy.
# Usage: tools/sdk/wsl_run.sh [--with <dir>]... <program.exe> [args...]
#        tools/sdk/wsl_run.sh --check     exit 0 when interop works, 77 if not
# Exit code: the program's; 77 when WSL interop is not available.
set -euo pipefail

interop() {
    [ -x /mnt/c/Windows/System32/cmd.exe ] || return 1
    local f
    for f in /proc/sys/fs/binfmt_misc/WSLInterop*; do
        [ -f "$f" ] && head -n 1 "$f" | grep -qx enabled && return 0
    done
    return 1
}

if [ "${1:-}" = --check ]; then
    interop && exit 0
    exit 77
fi
interop || { echo "wsl_run: no WSL interop (not WSL2, or interop is off)" >&2; exit 77; }

with=()
while [ "${1:-}" = --with ]; do
    with+=("$2")
    shift 2
done
exe=${1:?usage: wsl_run.sh [--with <dir>]... <program.exe> [args...]}
shift

# %TEMP% of the Windows user, as a WSL path (cmd.exe from a Windows
# folder, so it does not warn about a UNC working directory).
wtemp=$(cd /mnt/c && /mnt/c/Windows/System32/cmd.exe /d /c 'echo %TEMP%' | tr -d '\r')
wtemp=$(wslpath -u "$wtemp")
[ -d "$wtemp" ] || { echo "wsl_run: Windows %TEMP% ($wtemp) not found" >&2; exit 1; }
scratch=$(mktemp -d "$wtemp/yantra-wsl-run-XXXXXX")
trap 'rm -rf "$scratch"' EXIT
trap 'exit 130' INT TERM

for d in "${with[@]}"; do
    cp -r "$d"/. "$scratch"/
done
cp "$exe" "$scratch/"
cd "$scratch"
set +e
"./$(basename "$exe")" "$@"
rc=$?
set -e
exit "$rc"
