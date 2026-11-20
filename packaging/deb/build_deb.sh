#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Linux .deb with Yantra's own script (NFR-09). Builds no code: it installs
# an existing build tree (cmake --install --strip) into a staging folder,
# adds the built documentation, the Debian parts and runs dpkg-deb
# (no root: --root-owner-group). Never installs anything on this system.
#
#   packaging/deb/build_deb.sh BUILD_DIR OUT_DIR [--revision N]
#                              [--docs-site DIR] [--compress xz|gzip]
#
# Package contents: the install layout of BUILD.md "Install" below /usr:
# yantra, yantra-probe, yantra-plugin-test, the TETRA and probe plugins,
# the shipped radio plugins of plugins/ (PLG-29,
# self-contained, RAD-132) with their licence bundle, udev rules and
# RTL-SDR DVB-T blacklist (/usr/lib/udev/rules.d, /usr/lib/modprobe.d),
# PDFium, the desktop entry and hicolor icons 16..512 (BRD-06), logo,
# branding, fonts, demo projects, manual, quick starts, notes; plus
#   /usr/share/doc/yantra/html/         the built docs (DOC-02; `ninja docs`
#                                       or --docs-site)
#   /usr/share/doc/yantra/yantra-manual-<version>.pdf  the PDF manual from
#                                       the docs (DOC-02a; Help > User manual (PDF))
#   /usr/share/doc/yantra/copyright     DEP-5, Source from YANTRA_SOURCE_URL
#   /usr/share/doc/yantra/LICENSES/     the licence texts of REUSE.toml
#   /usr/share/doc/yantra/changelog.Debian.gz
# Metadata (BUILD_PROMPT, NFR-09): Maintainer "Curtat BV <info@curtat.be>",
# Homepage https://curtat.be, Vcs-Browser/Vcs-Git and the copyright Source
# from the build's YANTRA_SOURCE_URL (the release check: empty fails).
# Depends: dpkg-shlibdeps over every ELF file (fallback: a fixed list).
# Output: OUT_DIR/yantra_<version>-<revision>_<arch>.deb.
# Exit: 0 ok, 1 error, 77 dpkg-deb missing (ctest skip).
# Copied from yantra-v1 c24e8f5: packaging/deb/build_deb.sh, adapted.
set -euo pipefail

usage="usage: build_deb.sh BUILD_DIR OUT_DIR [--revision N] [--docs-site DIR] [--compress xz|gzip]"
build=${1:?$usage}
outdir=${2:?$usage}
shift 2
rev=1 site="" compress=xz
while [ $# -gt 0 ]; do
  case $1 in
    --revision) rev=$2; shift ;;
    --docs-site) site=$2; shift ;;
    --compress) compress=$2; shift ;;
    *) echo "$usage" >&2; exit 2 ;;
  esac
  shift
done
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
build=$(cd "$build" && pwd)
site=${site:-$build/docs/site}

command -v dpkg-deb >/dev/null || { echo "build_deb: dpkg-deb not found (apt install dpkg)"; exit 77; }
cache() { sed -n "s/^$1:[A-Z]*=\(.*\)$/\1/p" "$build/CMakeCache.txt"; }
# The product version (file VERSION, PLG-29); a pre-release "0.3.0-dev" is
# the Debian upstream version "0.3.0~dev", which sorts before 0.3.0.
version=$(head -n1 "$src/VERSION" | tr -d '[:space:]')
deb_version=$(printf '%s' "$version" | tr '-' '~')
source_url=$(cache YANTRA_SOURCE_URL)
[ -n "$version" ] || { echo "build_deb: no version in VERSION" >&2; exit 1; }
[ -n "$source_url" ] || { echo "build_deb: YANTRA_SOURCE_URL is empty (NFR-09 release check)" >&2; exit 1; }
[ -z "$(cache YANTRA_SANITIZER)" ] || { echo "build_deb: a sanitizer tree is never packaged" >&2; exit 1; }
[ -x "$build/bin/yantra" ] || { echo "build_deb: $build has no app (YANTRA_BUILD_APP=OFF)" >&2; exit 1; }
manual_pdf=$site/docs/manual/yantra-manual-$version.pdf
[ -f "$manual_pdf" ] \
  || { echo "build_deb: no PDF manual in $site (run ninja -C $build docs, DOC-02a)" >&2; exit 1; }
arch=$(dpkg --print-architecture)

mkdir -p "$outdir"
outdir=$(cd "$outdir" && pwd)
# debian/yantra: the folder dpkg-shlibdeps expects a package's files in.
stage=$outdir/debian/yantra
rm -rf "$outdir/debian"
mkdir -p "$stage"
DESTDIR="$stage" cmake --install "$build" --prefix /usr --strip >"$outdir/install.log"

doc=$stage/usr/share/doc/yantra
rm -rf "$doc/html"
mkdir -p "$doc/html"
(cd "$site" && find . -type f ! -name build.log ! -name external-links.txt ! -name '.*' \
   ! -name 'yantra-manual-*.pdf' -print0 | xargs -0 cp --parents -t "$doc/html")
cp "$manual_pdf" "$doc/"
sed -e "s|@SOURCE_URL@|$source_url|" "$here/copyright.in" >"$doc/copyright"
mkdir -p "$doc/LICENSES"
cp "$src"/LICENSES/*.txt "$doc/LICENSES/"
date_r=$(date -R -u -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}")
{
  echo "yantra ($deb_version-$rev) unstable; urgency=medium"
  echo
  echo "  * Package built by packaging/deb/build_deb.sh; the changes are in"
  echo "    /usr/share/doc/yantra/CHANGELOG.md."
  echo
  echo " -- Curtat BV <info@curtat.be>  $date_r"
} | gzip -9n >"$doc/changelog.Debian.gz"

# Runtime dependencies of everything ELF in the package.
mkdir -p "$stage/DEBIAN"
mapfile -t elfs < <(cd "$outdir" && find debian/yantra/usr/bin debian/yantra/usr/lib/yantra -type f \
                       -exec sh -c 'head -c4 "$1" | grep -q ELF && echo "$1"' _ {} \; | sort)
depends=""
if command -v dpkg-shlibdeps >/dev/null; then
  printf 'Source: yantra\n\nPackage: yantra\nArchitecture: any\n' >"$outdir/debian/control"
  # libpdfium.so is the package's own library (lib/yantra, RUNPATH).
  depends=$(cd "$outdir" && dpkg-shlibdeps -O -l"debian/yantra/usr/lib/yantra" "${elfs[@]}" \
              2>"$outdir/shlibdeps.log" | sed -n 's/^shlibs:Depends=//p') || depends=""
fi
if [ -z "$depends" ]; then
  echo "build_deb: dpkg-shlibdeps unavailable or failed (see $outdir/shlibdeps.log), using the fixed list"
  depends="libc6, libstdc++6, libgcc-s1, libglfw3, libgl1, libhpdf-2.3.0, libcurl4t64 | libcurl4, libsqlite3-0, libzstd1"
fi

size=$(du -sk --exclude=DEBIAN "$stage" | cut -f1)
cat >"$stage/DEBIAN/control" <<EOF
Package: yantra
Version: $deb_version-$rev
Architecture: $arch
Maintainer: Curtat BV <info@curtat.be>
Installed-Size: $size
Depends: $depends
Section: hamradio
Priority: optional
Homepage: https://curtat.be
Vcs-Browser: $source_url
Vcs-Git: $source_url.git
Description: indoor TETRA coverage survey with a software-defined radio
 Yantra measures TETRA coverage inside buildings, floor by floor: import
 the floor plans, scale and line them up, walk the floors with a radio,
 and get interpolated coverage maps and an acceptance report. It records
 the broadcast and system information, received levels and signal quality
 only: no voice, short data, subscriber addresses or decryption.
 .
 The package holds the desktop app, the headless probe (yantra-probe) with
 its remote API, the plugin test kit, the TETRA and probe plugins, the
 self-contained radio plugins (simulator, HackRF, generic SoapySDR, TETRA
 terminal) with udev rules and the RTL-SDR DVB-T driver blacklist, the
 Fraunces demo project, and the manual (also as PDF) and quick starts.
EOF
install -m 0755 "$here/postinst" "$stage/DEBIAN/postinst"
install -m 0755 "$here/postrm" "$stage/DEBIAN/postrm"
(cd "$stage" && find . -path ./DEBIAN -prune -o -type f -printf '%P\0' | sort -z | xargs -0 md5sum) \
  >"$stage/DEBIAN/md5sums"
# Explicit modes, independent of a setgid parent folder or the umask.
bash "$here/stage_perms.sh" "$stage"

deb=$outdir/yantra_${deb_version}-${rev}_${arch}.deb
case $compress in
  xz) dpkg-deb --root-owner-group -Zxz --build "$stage" "$deb" >/dev/null ;;
  gzip) dpkg-deb --root-owner-group -Zgzip -z1 --build "$stage" "$deb" >/dev/null ;;
  *) echo "build_deb: --compress xz or gzip" >&2; exit 2 ;;
esac
echo "build_deb: $deb ($(du -h "$deb" | cut -f1))"
