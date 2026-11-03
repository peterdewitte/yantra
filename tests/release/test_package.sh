#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs tools/ci/package_release.sh on the build tree and checks the
# package (RAD-137): licence texts next to the plugins, the bundled
# libraries' licences, no test-only doctest, and a source archive with the
# pinned submodules but without osmo-tetra's crypto/.
# Usage: test_package.sh <source-dir> <build-dir> <out-dir>
set -eu
src=$1 build=$2 out=$3
rm -rf "$out"
"$src/tools/ci/package_release.sh" "$build" "$out" vtest linux-x86_64 >/dev/null
pkg=$(tar -tzf "$out/yantra-radios-vtest-linux-x86_64.tar.gz")
fail=0
need() { echo "$pkg" | grep -qx "$1" || { echo "missing in package: $1"; fail=1; }; }
need yantra-radios-vtest-linux-x86_64/LICENSE
need yantra-radios-vtest-linux-x86_64/THIRD_PARTY.md
need yantra-radios-vtest-linux-x86_64/LICENSES/GPL-3.0-or-later.txt
need yantra-radios-vtest-linux-x86_64/plugins/
if [ -f "$src/third_party/osmo-tetra/COPYING" ]; then
    need yantra-radios-vtest-linux-x86_64/licences/osmo-tetra/COPYING
fi
if echo "$pkg" | grep -q doctest; then echo "doctest (test only) is in the package"; fail=1; fi
srcs=$(tar -tzf "$out/yantra-radios-vtest-source.tar.gz")
echo "$srcs" | grep -qx "yantra-radios-vtest-source/CMakeLists.txt" || { echo "source archive lacks CMakeLists.txt"; fail=1; }
if [ -f "$src/third_party/osmo-tetra/COPYING" ]; then
    echo "$srcs" | grep -q "^yantra-radios-vtest-source/third_party/osmo-tetra/src/tetra_common.h$" \
        || { echo "source archive lacks the osmo-tetra submodule"; fail=1; }
fi
if echo "$srcs" | grep -q "osmo-tetra/src/crypto"; then echo "source archive contains osmo-tetra crypto/"; fail=1; fi
rm -rf "$out"
[ $fail -eq 0 ] && echo "release package: OK"
exit $fail
