#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static dependency builds (RAD-136), placeholder: creates the install
# prefix and an empty dependency manifest (name, version, licence per
# line). Runs the same on Linux and in MSYS2 UCRT64.
# Usage: tools/deps/build_deps.sh <prefix>
set -euo pipefail
prefix=${1:?usage: build_deps.sh <prefix>}
mkdir -p "$prefix/lib" "$prefix/include"
: > "$prefix/deps-manifest.tsv"
echo "build_deps: no bundled libraries yet; prefix $prefix ready"
