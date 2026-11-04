#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release packaging (RAD-98, RAD-134 release.yml, RAD-137):
#   <out>/yantra-radios-<version>-<platform>.tar.gz
#       the install tree of <build> (cmake --install, prefix "."):
#       lib/yantra-radios/plugins/ the self-contained plugins (RAD-132;
#       not the example or the test builds), bin/ yantra-radio-host,
#       yantra-gen and yantra-hackrf, share/yantra-radios/scenarios,
#       lib/udev/rules.d and lib/modprobe.d (RAD-99), the docs, and the
#       licence bundle share/licenses/yantra-radios (licence_bundle.py:
#       LICENSE, LICENSES/, THIRD_PARTY.md, every bundled library's texts,
#       BUNDLE.md), so the licences ship next to the libraries
#   <out>/yantra-radios-<version>-source.tar.gz (linux platforms only)
#       the complete corresponding source (RAD-137): this repository at
#       HEAD with every submodule at its pinned commit (without
#       osmo-tetra's crypto/, never built), and upstream/ with the release
#       archives of tools/deps/manifest.tsv that the vendored subsets come
#       from, each checked against its SHA-256 (upstream/SHA256SUMS)
# The archives come from the cache folder of tools/deps/verify_sources.py
# ($YANTRA_DEPS_CACHE, default <source>/build/deps-cache; that script
# downloads them). YANTRA_RELEASE_NO_UPSTREAM=1 leaves upstream/ out (for
# tests without the cache); a release never sets it.
# Usage: tools/ci/package_release.sh <build-dir> <out-dir> <version> <platform>
set -euo pipefail
build=${1:?usage: package_release.sh <build-dir> <out-dir> <version> <platform>}
out=${2:?out-dir}
version=${3:?version}
platform=${4:?platform}
root=$(cd "$(dirname "$0")/../.." && pwd)
build=$(cd "$build" && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
cache=${YANTRA_DEPS_CACHE:-$root/build/deps-cache}
name=yantra-radios-${version}-${platform}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT INT TERM

if [ -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]; then
    echo "package_release: warning: uncommitted changes; the source archive is HEAD without them" >&2
fi

pkg=$stage/$name
DESTDIR= cmake --install "$build" --prefix "$pkg" >/dev/null
for f in "$pkg"/lib/yantra-radios/plugins/*; do
    [ -f "$f" ] || { echo "package_release: no plugins installed" >&2; exit 1; }
done
[ -f "$pkg/share/licenses/yantra-radios/BUNDLE.md" ] \
    || { echo "package_release: no licence bundle" >&2; exit 1; }
[ -f "$build/deps/deps-manifest.tsv" ] && cp "$build/deps/deps-manifest.tsv" "$pkg/share/licenses/yantra-radios/"
tar -C "$stage" -czf "$out/$name.tar.gz" "$name"
echo "package_release: $out/$name.tar.gz"

case $platform in
    linux*)
        src=yantra-radios-${version}-source
        srcstage=$stage/src
        mkdir -p "$srcstage"
        git -C "$root" archive --prefix="$src/" HEAD | tar -C "$srcstage" -xf -
        git -C "$root" submodule foreach --quiet --recursive \
            'git archive --prefix="'"$src"'/$displaypath/" HEAD | tar -C "'"$srcstage"'" -xf -'
        # osmo-tetra's crypto/ is never built or used (scope 16.5), so it
        # is not part of the corresponding source either.
        rm -rf "${srcstage:?}/$src/third_party/osmo-tetra/src/crypto"
        if [ "${YANTRA_RELEASE_NO_UPSTREAM:-0}" != 1 ]; then
            up=$srcstage/$src/upstream
            mkdir -p "$up"
            : > "$up/SHA256SUMS"
            grep -v '^#' "$root/tools/deps/manifest.tsv" | while IFS=$'\t' read -r n _v _l _f url sha; do
                [ -n "$n" ] && [ "$url" != "-" ] || continue
                file=${url##*/}
                if [ ! -f "$cache/$file" ]; then
                    echo "package_release: $file not in $cache (run tools/deps/verify_sources.py)" >&2
                    exit 1
                fi
                echo "$sha  $file" >> "$up/SHA256SUMS"
                cp "$cache/$file" "$up/$file"
            done
            (cd "$up" && sha256sum --quiet -c SHA256SUMS) \
                || { echo "package_release: an upstream archive does not match its SHA-256" >&2; exit 1; }
        fi
        tar -C "$srcstage" -czf "$out/$src.tar.gz" "$src"
        echo "package_release: $out/$src.tar.gz"
        ;;
esac
