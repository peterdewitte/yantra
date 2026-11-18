#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test_stage_perms.sh   (ctest package_deb_perms, NFR-09)
# Regression: a staging tree below a setgid folder with an odd umask gets
# the modes dpkg-deb accepts from stage_perms.sh, and a minimal package
# builds from it. Exit 77 (skipped) without dpkg-deb.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
command -v dpkg-deb >/dev/null || { echo "SKIPPED: dpkg-deb not found"; exit 77; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
chmod 2775 "$tmp"
umask 0002
stage=$tmp/debian/yantra
mkdir -p "$stage/DEBIAN" "$stage/usr/bin" "$stage/usr/share/doc/yantra"
printf 'Package: yantra-perms-test\nVersion: 0-1\nArchitecture: all\nMaintainer: test <test@example.invalid>\nDescription: test\n' \
  >"$stage/DEBIAN/control"
printf '#!/bin/sh\n' >"$stage/usr/bin/tool" && chmod 0775 "$stage/usr/bin/tool"
echo doc >"$stage/usr/share/doc/yantra/README"
[ "$(stat -c %a "$stage/DEBIAN")" = 2775 ] || fail "setup: DEBIAN did not inherit setgid"
bash "$here/stage_perms.sh" "$stage"
check() { [ "$(stat -c %a "$1")" = "$2" ] || fail "$1 has mode $(stat -c %a "$1"), expected $2"; }
while IFS= read -r -d '' d; do check "$d" 755; done < <(find "$stage" -type d -print0)
check "$stage/DEBIAN/control" 644
check "$stage/usr/bin/tool" 755
check "$stage/usr/share/doc/yantra/README" 644
dpkg-deb --root-owner-group --build "$stage" "$tmp/test.deb" >/dev/null || fail "dpkg-deb refused the stage"
echo "OK: stage modes 0755/0644/0755, dpkg-deb accepts it below a setgid folder"
