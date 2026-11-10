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
#        tools/sdk/wsl_run.sh --in-place [--with <dir>]... <program.exe> [args...]
#        tools/sdk/wsl_run.sh --check     exit 0 when interop works, 77 if not
# --in-place (the CMAKE_CROSSCOMPILING_EMULATOR of the Windows tree, preset
# "windows"): runs the program where it is, in the current folder, instead
# of a copy. A program on the WSL file system starts in a fraction of a
# second this way, and Linux paths in its arguments, in the variables and
# compiled into the tests (/home/...) resolve to the same files: Windows
# reads a rooted path on the drive or share of the current folder, here
# \\wsl.localhost\<distribution>. The program sees a clean environment
# (RAD-133): PATH holds the --with folders and the Windows system folders
# only (no MSYS2, no other tools), plus the variables YANTRA_* and SOAPY_*
# of this shell (ctest ENVIRONMENT properties), passed through WSLENV.
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

in_place=0
if [ "${1:-}" = --in-place ]; then
    in_place=1
    shift
fi
with=()
while [ "${1:-}" = --with ]; do
    with+=("$2")
    shift 2
done
exe=${1:?usage: wsl_run.sh [--in-place] [--with <dir>]... <program.exe> [args...]}
shift

if [ "$in_place" = 1 ]; then
    path=""
    for d in "${with[@]}"; do
        path="$path$(cd "$d" && pwd):"
    done
    path="$path/mnt/c/Windows/System32:/mnt/c/Windows:/mnt/c/Windows/System32/Wbem"
    wslenv="PATH/l"
    while IFS= read -r name; do
        wslenv="$wslenv:$name"
    done < <(compgen -e | grep -E '^(YANTRA|SOAPY)_' || true)
    case "$exe" in
        */*) ;;
        *) exe="./$exe" ;;
    esac
    exec env PATH="$path" WSLENV="$wslenv" "$exe" "$@"
fi

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
