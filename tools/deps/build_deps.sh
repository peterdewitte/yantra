#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static dependency builds (RAD-136): builds the bundled libraries of
# tools/deps/deps.cmake (talloc, the libosmocore subset osmo-tetra needs;
# SoapySDR, its modules, their vendor libraries and libusb, sdr.cmake)
# from the pinned sources under third_party/ and installs them, with their
# headers, licence texts and deps-manifest.tsv (name, version, licence),
# into <prefix>. The build folder is <prefix>/build. Configure the main
# project with -DYANTRA_DEPS_PREFIX=<prefix> to use the result; without
# it the main project builds the same libraries in its own build tree.
# Runs the same on Linux and in MSYS2 UCRT64 (cmake, ninja, a C compiler).
# The result is reused when this folder and the vendored sources have not
# changed (stamp file), so CI can cache <prefix> per script version.
# Usage: tools/deps/build_deps.sh <prefix>
set -euo pipefail
prefix=${1:?usage: build_deps.sh <prefix>}
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$prefix"
prefix=$(cd "$prefix" && pwd)

# Stamp: hash of tools/deps and the vendored sources.
stamp=$(cd "$root" && find tools/deps third_party/talloc third_party/libosmocore third_party/libusb \
    third_party/soapysdr third_party/hackrf third_party/soapyhackrf third_party/rtl-sdr \
    third_party/soapyrtlsdr third_party/airspy third_party/soapyairspy -type f -print0 \
    | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)
if [ -f "$prefix/deps.stamp" ] && [ "$(cat "$prefix/deps.stamp")" = "$stamp" ]; then
    echo "build_deps: $prefix is up to date"
    exit 0
fi

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S "$root/tools/deps" -B "$prefix/build" "${generator[@]}" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release
cmake --build "$prefix/build" -j "$(nproc 2>/dev/null || echo 4)"
cmake --install "$prefix/build"
echo "$stamp" > "$prefix/deps.stamp"
echo "build_deps: bundled libraries in $prefix:"
cat "$prefix/deps-manifest.tsv"
