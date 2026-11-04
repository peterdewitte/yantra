#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A local release folder (RAD-98, RAD-134, RAD-137, RAD-138), the steps of
# release.yml for one platform: the plugin package and the corresponding
# source (package_release.sh), SHA256SUMS over every file and
# RELEASE_NOTES.md (the CHANGELOG.md section of the version,
# release_notes.py). The completeness check (RAD-133) runs on the packaged
# plugins first. CMake target `release` runs it into
# <build>/release/v<version>. The Windows .dll package is DEFERRED
# (Windows): release.yml has the job.
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
rm -rf "${out:?}"
mkdir -p "$out"
"$here/package_release.sh" "$build" "$out" "$tag" "$platform"
check=$(mktemp -d)
trap 'rm -rf "$check"' EXIT INT TERM
tar -C "$check" -xzf "$out/yantra-radios-$tag-$platform.tar.gz"
python3 -B "$here/check_completeness.py" "$check/yantra-radios-$tag-$platform/lib/yantra-radios/plugins"
python3 -B "$here/release_notes.py" "$root/CHANGELOG.md" "$tag" > "$out/RELEASE_NOTES.md"
(cd "$out" && sha256sum -- *.tar.gz RELEASE_NOTES.md > SHA256SUMS)
echo "make_release: $out"
ls -l "$out"
