#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Smoke test of the cross SDK's Windows part (RAD-139; ctest
# sdk_windows_smoke, label slow): configures and builds tests/sdk/win_smoke
# with tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake (C++20 with
# threads, iostream, std::filesystem and exceptions, a version resource,
# a Rust static library for x86_64-pc-windows-gnu), checks with
# llvm-objdump -p that the statically linked .exe imports only Windows
# system DLLs and the UCRT (RAD-132), and runs it on this PC's Windows
# through WSL interop (tools/sdk/wsl_run.sh).
# Exit code 77 (skipped) when there is no SDK or no WSL interop.
# Usage: tests/sdk/win_smoke.sh <work-dir>
set -euo pipefail
work=${1:?usage: win_smoke.sh <work-dir>}
root=$(cd "$(dirname "$0")/../../.." && pwd)
sdk=${YANTRA_SDK_DIR:-$root/sdk}

if [ ! -f "$sdk/manifest/msys2.lock" ]; then
    echo "no cross SDK in $sdk (tools/sdk/yantra_sdk.py make): skipped"
    exit 77
fi
if ! "$root/tools/sdk/wsl_run.sh" --check; then
    echo "no WSL interop: skipped"
    exit 77
fi

rm -rf "$work"
cmake -S "$root/tests/radio/sdk/win_smoke" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DYANTRA_SDK_DIR="$sdk" \
    -DCMAKE_TOOLCHAIN_FILE="$root/tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake" >"$work.configure.log" 2>&1 \
    || { cat "$work.configure.log"; exit 1; }
cmake --build "$work" >"$work.build.log" 2>&1 || { cat "$work.build.log"; exit 1; }
exe=$work/smoke.exe
file_type=$(head -c 2 "$exe")
[ "$file_type" = MZ ] || { echo "FAIL: $exe is not a PE file"; exit 1; }

# DLL imports (RAD-132): only Windows system DLLs and the UCRT, so no
# libstdc++-6.dll, libgcc_s_seh-1.dll or libwinpthread-1.dll.
imports=$("$sdk/llvm/bin/llvm-objdump" -p "$exe" | sed -n 's/^ *DLL Name: *//p' | tr -d '\r')
echo "imports: $(echo "$imports" | tr '\n' ' ')"
bad=$(echo "$imports" | grep -viE '^(kernel32|ntdll|ws2_32|userenv|bcrypt|bcryptprimitives|advapi32|user32|shell32|ole32)\.dll$|^api-ms-win-(crt|core)-[a-z0-9-]+\.dll$' || true)
if [ -n "$bad" ]; then
    echo "FAIL: imports that are not Windows system DLLs or the UCRT: $bad"
    exit 1
fi
echo "$imports" | grep -qi '^api-ms-win-crt-' || { echo "FAIL: no UCRT import"; exit 1; }
"$sdk/llvm/bin/llvm-objdump" -h "$exe" | grep -q '\.rsrc' || { echo "FAIL: no .rsrc section (llvm-rc)"; exit 1; }

out=$("$root/tools/sdk/wsl_run.sh" "$exe" | tr -d '\r')
echo "$out"
[ "$(echo "$out" | tail -n 1)" = "smoke ok" ] || { echo "FAIL: the program did not end with 'smoke ok'"; exit 1; }
echo "sdk_windows_smoke: passed"
