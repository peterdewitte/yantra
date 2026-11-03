#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release packaging (RAD-134 release.yml, RAD-137):
#   <out>/yantra-radios-<version>-<platform>.tar.gz
#       plugins/ (the .so/.dll of <build>/bin/plugins), bin/ (the tools that
#       exist: yantra-radio-host, yantra-gen, yantra-hackrf), LICENSE,
#       LICENSES/, THIRD_PARTY.md and licences/<component>/ with the licence
#       text of every bundled library under third_party/ (the test-only
#       doctest is not bundled), next to the libraries
#   <out>/yantra-radios-<version>-source.tar.gz (linux platforms only)
#       the complete corresponding source: this repository at HEAD with
#       every submodule at its pinned commit, the third_party/ sources and
#       the build scripts (without osmo-tetra's crypto/, never built)
# Usage: tools/ci/package_release.sh <build-dir> <out-dir> <version> <platform>
set -euo pipefail
build=${1:?usage: package_release.sh <build-dir> <out-dir> <version> <platform>}
out=${2:?out-dir}
version=${3:?version}
platform=${4:?platform}
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
name=yantra-radios-${version}-${platform}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT INT TERM
pkg=$stage/$name
mkdir -p "$pkg/plugins" "$pkg/bin" "$pkg/licences"

shopt -s nullglob
for f in "$build"/bin/plugins/*.so "$build"/bin/plugins/*.dll; do
    cp "$f" "$pkg/plugins/"
done
for t in yantra-radio-host yantra-gen yantra-hackrf; do
    for f in "$build/bin/$t" "$build/bin/$t.exe"; do
        [ -f "$f" ] && cp "$f" "$pkg/bin/"
    done
done
cp "$root/LICENSE" "$root/THIRD_PARTY.md" "$pkg/"
cp -r "$root/LICENSES" "$pkg/"
for dir in "$root"/third_party/*/; do
    comp=$(basename "$dir")
    [ "$comp" = doctest ] && continue   # test only, never shipped
    files=("$dir"COPYING* "$dir"LICENSE* "$dir"LICENCE* "$dir"NOTICE*)
    if [ ${#files[@]} -gt 0 ]; then
        mkdir -p "$pkg/licences/$comp"
        cp "${files[@]}" "$pkg/licences/$comp/"
    fi
done
[ -f "$build/deps/deps-manifest.tsv" ] && cp "$build/deps/deps-manifest.tsv" "$pkg/"
tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "package_release: $out/$name.tar.gz"

case $platform in
    linux*)
        src=yantra-radios-${version}-source
        srcstage=$stage/src
        mkdir -p "$srcstage"
        git -C "$root" archive --prefix="$src/" HEAD | tar -C "$srcstage" -xf -
        # shellcheck disable=SC2016
        git -C "$root" submodule foreach --quiet --recursive \
            'git archive --prefix="'"$src"'/$displaypath/" HEAD | tar -C "'"$srcstage"'" -xf -'
        # osmo-tetra's crypto/ is never built or used (scope 16.5), so it
        # is not part of the corresponding source either.
        rm -rf "$srcstage/$src/third_party/osmo-tetra/src/crypto"
        tar -C "$srcstage" -czf "$out/$src.tar.gz" "$src"
        echo "package_release: $out/$src.tar.gz"
        ;;
esac
