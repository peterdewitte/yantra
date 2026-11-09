#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test_deb.sh BUILD_DIR OUT_DIR DOCS_SITE   (ctest package_deb, NFR-09)
# Builds the .deb from the build tree with the documentation the ctest
# docs_build made (fast gzip compression) and checks it with check_deb.sh,
# including the build-machine folder scan of tests/install. Offline; no
# root; nothing is installed. Exit 77 (skipped) without dpkg-deb, or
# without pandoc/WeasyPrint for the manual PDF.
set -euo pipefail
build=${1:?} out=${2:?} site=${3:?}
here=$(cd "$(dirname "$0")" && pwd)
command -v dpkg-deb >/dev/null || { echo "SKIPPED: dpkg-deb not found"; exit 77; }
if [ ! -f "$site/docs/manual/manual.pdf" ]; then
  if ! command -v pandoc >/dev/null || ! command -v weasyprint >/dev/null; then
    echo "SKIPPED: no manual PDF (pandoc or WeasyPrint missing)"; exit 77
  fi
  echo "FAIL: no manual PDF in $site (docs_build)"; exit 1
fi
url=$(sed -n 's/^YANTRA_SOURCE_URL:[A-Z]*=\(.*\)$/\1/p' "$build/CMakeCache.txt")
rm -rf "$out"
bash "$here/build_deb.sh" "$build" "$out" --docs-site "$site" --compress gzip
deb=$(ls "$out"/yantra_*.deb)
dpkg-deb -I "$deb" | sed -n '/Package:/,$p'
status=0
YANTRA_CHECK_BUILD=$build bash "$here/check_deb.sh" "$deb" "$url" || status=$?
rm -rf "$out/debian"
exit $status
