#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs tools/ci/package_release.sh on the build tree and checks the
# package (RAD-98, RAD-137): the installed plugins with the licence bundle
# next to them (every row of tools/deps/manifest.tsv has its licence
# folder, BUNDLE.md lists it), no example or test plugin, no doctest; and
# a source archive with the pinned submodules, without osmo-tetra's
# crypto/, with every upstream release archive and its SHA-256 when the
# archives are in the cache (tools/deps/verify_sources.py; otherwise that
# part is left out and the test says so).
# Usage: test_package.sh <source-dir> <build-dir> <out-dir>
set -eu
src=$1 build=$2 out=$3
rm -rf "$out"
cache=${YANTRA_DEPS_CACHE:-$src/build/deps-cache}
upstream=1
for url in $(grep -v '^#' "$src/tools/deps/manifest.tsv" | cut -f5 | grep -v '^-$'); do
    [ -f "$cache/${url##*/}" ] || upstream=0
done
if [ $upstream -eq 0 ]; then
    echo "note: upstream archives not cached: the source archive is checked without upstream/"
    export YANTRA_RELEASE_NO_UPSTREAM=1
fi
"$src/tools/ci/package_release.sh" "$build" "$out" vtest linux-x86_64 >/dev/null 2>"$out.err" \
    || { cat "$out.err"; exit 1; }
rm -f "$out.err"
p=yantra-radios-vtest-linux-x86_64
pkg=$(tar -tzf "$out/$p.tar.gz")
fail=0
need() { echo "$pkg" | grep -qx "$1" || { echo "missing in package: $1"; fail=1; }; }
lic=$p/share/licenses/yantra-radios
need "$p/lib/yantra-radios/plugins/hackrf.so"
need "$p/lib/yantra-radios/plugins/soapysdr.so"
need "$p/lib/yantra-radios/plugins/simulator.so"
need "$p/lib/yantra-radios/plugins/pei.so"
need "$p/bin/yantra-radio-host"
need "$lic/LICENSE"
need "$lic/THIRD_PARTY.md"
need "$lic/BUNDLE.md"
need "$lic/LICENSES/GPL-3.0-or-later.txt"
need "$lic/rust-std/MIT.txt"
for folder in $(grep -v '^#' "$src/tools/deps/manifest.tsv" | cut -f4); do
    comp=${folder##*/}
    echo "$pkg" | grep -q "^$lic/$comp/." || { echo "no licence text for $folder"; fail=1; }
done
for bad in beacon.so hackrf_fake.so soapysdr_fake.so doctest; do
    if echo "$pkg" | grep -q "$bad"; then echo "must not be in the package: $bad"; fail=1; fi
done
tmp=$out/x
mkdir -p "$tmp"
tar -C "$tmp" -xzf "$out/$p.tar.gz" "$lic/BUNDLE.md"
for name in $(grep -v '^#' "$src/tools/deps/manifest.tsv" | cut -f1); do
    grep -q "^| $name |" "$tmp/$lic/BUNDLE.md" || { echo "BUNDLE.md lacks $name"; fail=1; }
done
s=yantra-radios-vtest-source
srcs=$(tar -tzf "$out/$s.tar.gz")
echo "$srcs" | grep -qx "$s/CMakeLists.txt" || { echo "source archive lacks CMakeLists.txt"; fail=1; }
echo "$srcs" | grep -qx "$s/tools/deps/build_deps.sh" || { echo "source archive lacks the build scripts"; fail=1; }
if [ -f "$src/third_party/osmo-tetra/COPYING" ]; then
    echo "$srcs" | grep -q "^$s/third_party/osmo-tetra/src/tetra_common.h$" \
        || { echo "source archive lacks the osmo-tetra submodule"; fail=1; }
fi
if echo "$srcs" | grep -q "osmo-tetra/src/crypto"; then echo "source archive contains osmo-tetra crypto/"; fail=1; fi
if [ $upstream -eq 1 ]; then
    tar -C "$tmp" -xzf "$out/$s.tar.gz" "$s/upstream"
    (cd "$tmp/$s/upstream" && sha256sum --quiet -c SHA256SUMS) || { echo "upstream SHA256SUMS fails"; fail=1; }
    for sha in $(grep -v '^#' "$src/tools/deps/manifest.tsv" | cut -f6 | grep -v '^-$'); do
        grep -q "^$sha " "$tmp/$s/upstream/SHA256SUMS" || { echo "upstream lacks the archive $sha"; fail=1; }
    done
fi
rm -rf "$out"
[ $fail -eq 0 ] && echo "release package: OK"
exit $fail
