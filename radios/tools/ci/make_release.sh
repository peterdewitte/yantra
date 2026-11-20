#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A local release folder (RAD-98, RAD-134, RAD-137, RAD-138), the steps of
# release.yml for one platform: the plugin package and the corresponding
# source (package_release.sh), SHA256SUMS over every file and
# RELEASE_NOTES.md (the CHANGELOG.md section of the version,
# release_notes.py), and the PDF manual yantra-radios-manual-<version>.pdf
# (RAD-97; `ninja docs-manual` first, the CMake target `release` does).
# The completeness check (RAD-133) runs on the packaged plugins first. CMake target `release` runs it into
# <build>/release/v<version>. For the Windows tree: platform
# windows-x86_64 (a .zip, BUILD.md).
# Usage: tools/ci/make_release.sh <build-dir> <out-dir> <tag> [platform]
set -euo pipefail
build=${1:?usage: make_release.sh <build-dir> <out-dir> <tag> [platform]}
out=${2:?out-dir}
tag=${3:?tag}
platform=${4:-linux-x86_64}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
python3 -B "$here/release_notes.py" "$root/CHANGELOG.md" "$tag" > /dev/null \
    || { echo "make_release: CHANGELOG.md has no section for $tag" >&2; exit 1; }
manual=$build/docs/manual/yantra-radios-manual-${tag#v}.pdf
[ -f "$manual" ] || { echo "make_release: no $manual (ninja docs-manual)" >&2; exit 1; }
rm -rf "${out:?}"
mkdir -p "$out"
"$here/package_release.sh" "$build" "$out" "$tag" "$platform"
cp "$manual" "$out/"
check=$(mktemp -d)
trap 'rm -rf "$check"' EXIT INT TERM
pkg=$check/yantra-radios-$tag-$platform
case $platform in
    windows*)
        python3 -m zipfile -e "$out/yantra-radios-$tag-$platform.zip" "$check"
        objdump=$(sed -n 's/^CMAKE_OBJDUMP:FILEPATH=//p' "$build/CMakeCache.txt")
        python3 -B "$here/check_completeness.py" --platform windows --objdump "$objdump" "$pkg/lib/yantra-radios/plugins"
        python3 -B "$here/check_completeness.py" --platform windows --programs --objdump "$objdump" "$pkg/bin"
        ;;
    *)
        tar -C "$check" -xzf "$out/yantra-radios-$tag-$platform.tar.gz"
        python3 -B "$here/check_completeness.py" "$pkg/lib/yantra-radios/plugins"
        ;;
esac
[ -f "$pkg/share/doc/yantra-radios/${manual##*/}" ] || { echo "make_release: the package has no PDF manual" >&2; exit 1; }
python3 -B "$here/release_notes.py" "$root/CHANGELOG.md" "$tag" > "$out/RELEASE_NOTES.md"
(cd "$out" && sha256sum -- yantra-radios-* RELEASE_NOTES.md > SHA256SUMS)
echo "make_release: $out"
ls -l "$out"
