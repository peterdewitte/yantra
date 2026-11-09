#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# check_deb.sh DEB SOURCE_URL [SOURCE_DIR]   (NFR-09)
# Checks a package made by build_deb.sh without installing it: the control
# fields (dpkg-deb -I; the owner data exactly as given: Maintainer,
# Homepage, the source link), the contents (dpkg-deb -c: binaries, the
# shipped radio plugins and no test plugin, PLG-26; desktop entry and
# hicolor icons 16..512, BRD-06; data, manual PDF, quick starts in four
# languages, licences, udev rules and blacklist), files owned by root,
# the maintainer scripts, md5sums, the DEP-5 copyright file; on the
# unpacked tree: the app finds its own PDFium (RUNPATH), no file names a
# build-machine folder (tests/install/check_build_paths.py) or holds the
# builder's home folder or git identity (personal data), no MAK plan
# image (DEMO-12: no picture under demo/mak, no file with a MAK sheet's
# SHA-256). lintian when installed.
# Copied from yantra-v1 c24e8f5: packaging/deb/check_deb.sh, adapted.
set -u
deb=${1:?usage: check_deb.sh DEB SOURCE_URL [SOURCE_DIR]}
source_url=${2:?usage: check_deb.sh DEB SOURCE_URL [SOURCE_DIR]}
here=$(cd "$(dirname "$0")" && pwd)
src=${3:-$(cd "$here/../.." && pwd)}
fail=0
bad() { echo "FAIL: $*"; fail=1; }

field() { dpkg-deb -f "$deb" "$1"; }
[ "$(field Package)" = yantra ] || bad "Package field"
[ "$(field Maintainer)" = "Curtat BV <info@curtat.be>" ] || bad "Maintainer: $(field Maintainer)"
[ "$(field Homepage)" = "https://curtat.be" ] || bad "Homepage: $(field Homepage)"
[ "$(field Vcs-Browser)" = "$source_url" ] || bad "Vcs-Browser: $(field Vcs-Browser)"
[ "$(field Vcs-Git)" = "$source_url.git" ] || bad "Vcs-Git: $(field Vcs-Git)"
[ "$(field Section)" = hamradio ] || bad "Section field"
deps=$(field Depends)
echo "Depends: $deps"
for d in libc6 libstdc++6 libglfw3 libhpdf libcurl libsqlite3 libzstd; do
  grep -q "$d" <<<"$deps" || bad "Depends lacks $d"
done
# The radio plugins are self-contained (RAD-132): no SoapySDR or vendor package.
grep -qi "soapysdr\|libhackrf\|libusb\|libosmo\|pdfium" <<<"$deps" && bad "Depends names a bundled library: $deps"

list=$(dpkg-deb -c "$deb" | awk '{print $6}')
want=(./usr/bin/yantra ./usr/bin/yantra-probe ./usr/bin/yantra-plugin-test
      ./usr/lib/yantra/libpdfium.so ./usr/lib/yantra/plugins/tetra.so ./usr/lib/yantra/plugins/probe.so
      ./usr/lib/yantra/plugins/radios/simulator.so ./usr/lib/yantra/plugins/radios/hackrf.so
      ./usr/lib/yantra/plugins/radios/soapysdr.so ./usr/lib/yantra/plugins/radios/pei.so
      ./usr/lib/udev/rules.d/60-yantra-sdr.rules ./usr/lib/modprobe.d/yantra-rtl-sdr-blacklist.conf
      ./usr/share/licenses/yantra-radios/BUNDLE.md ./usr/share/licenses/pdfium/
      ./usr/share/applications/yantra.desktop
      ./usr/share/doc/yantra/copyright ./usr/share/doc/yantra/changelog.Debian.gz ./usr/share/doc/yantra/CHANGELOG.md
      ./usr/share/doc/yantra/LICENSES/GPL-3.0-or-later.txt ./usr/share/doc/yantra/LICENSES/OFL-1.1.txt
      ./usr/share/doc/yantra/html/docs/manual/manual.pdf ./usr/share/doc/yantra/html/docs/manual/manual.html
      ./usr/share/doc/yantra/html/docs/METHOD.pdf ./usr/share/doc/yantra/html/docs/LEGAL.pdf
      ./usr/share/doc/yantra/html/docs/img/yantra_logo_128.png
      ./usr/share/yantra/THIRD_PARTY.md ./usr/share/yantra/METHOD.md ./usr/share/yantra/LEGAL.md
      ./usr/share/yantra/logo/png/yantra_logo_512.png ./usr/share/yantra/branding/curtat_logo.svg
      ./usr/share/yantra/fonts/Inter-Regular.ttf ./usr/share/yantra/fonts/Inter-OFL.txt
      ./usr/share/yantra/manual/13-indoor-projects.md ./usr/share/yantra/demo/fraunces/fraunces.yantra)
for l in "" nl/ fr/ de/; do
  want+=("./usr/share/yantra/quickstart/${l}01-first-indoor-project.md"
         "./usr/share/doc/yantra/html/docs/quickstart/${l}01-first-indoor-project.pdf")
done
for s in 16 24 32 48 64 128 256 512; do want+=("./usr/share/icons/hicolor/${s}x${s}/apps/yantra.png"); done
for w in "${want[@]}"; do grep -qx -- "$w" <<<"$list" || bad "missing $w"; done
for t in beacon.so soapysdr_fake.so hackrf_fake.so example_beacon.so; do
  grep -q "/$t\$" <<<"$list" && bad "test plugin $t in the package"
done
grep -q "^./etc/\|^./home/\|^./tmp/\|/build/\|\.a\$" <<<"$list" && bad "files outside /usr or build-tree files"
grep -qi "/demo/mak/.*\.\(png\|jpe\?g\|pdf\|tiff\?\|gif\|webp\)\$" <<<"$list" && bad "a MAK plan image in the package (DEMO-12)"
dpkg-deb -c "$deb" | awk '$2 != "root/root" {print}' | grep -q . && bad "files not owned by root"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
dpkg-deb -e "$deb" "$work/DEBIAN"
for s in postinst postrm; do
  [ -x "$work/DEBIAN/$s" ] || bad "$s not executable"
  sh -n "$work/DEBIAN/$s" || bad "$s syntax"
done
dpkg-deb -x "$deb" "$work/root"
root=$work/root
head -1 "$root/usr/share/doc/yantra/copyright" \
  | grep -qx "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/" || bad "copyright is not DEP-5"
grep -qx "Source: $source_url" "$root/usr/share/doc/yantra/copyright" || bad "copyright Source is not $source_url"
(cd "$root" && md5sum --quiet -c "$work/DEBIAN/md5sums") || bad "md5sums"
grep -q "^Icon=yantra\$" "$root/usr/share/applications/yantra.desktop" || bad "desktop entry icon"
for s in 16 512; do
  cmp -s "$root/usr/share/icons/hicolor/${s}x${s}/apps/yantra.png" "$src/data/logo/png/yantra_logo_${s}.png" \
    || bad "hicolor ${s}px icon is not data/logo/png/yantra_logo_${s}.png"
done
# The unpacked app finds the PDFium it ships ($ORIGIN RUNPATH).
readelf -d "$root/usr/bin/yantra" | grep -q 'RUNPATH.*\[\$ORIGIN/../lib/yantra\]' || bad "yantra RUNPATH"
pdfium=$(ldd "$root/usr/bin/yantra" 2>/dev/null | sed -n 's/.*libpdfium.so => \(.*\) (0x.*/\1/p')
[ -n "$pdfium" ] && [ "$(realpath "$pdfium")" = "$(realpath "$root/usr/lib/yantra/libpdfium.so")" ] \
  || bad "the unpacked yantra does not find its libpdfium.so (${pdfium:-not found})"
# No build-machine folder in any file (NFR-09; the same scan as the ctest
# install_build_paths), no personal data, no MAK sheet.
if [ -n "${YANTRA_CHECK_BUILD:-}" ]; then
  python3 -B "$src/tests/install/check_build_paths.py" --root "$root" --source "$src" --build "$YANTRA_CHECK_BUILD" \
    || bad "build-machine folders in the package"
fi
if [ -n "${HOME:-}" ] && [ "$HOME" != / ] && grep -rlaF "$HOME" "$root" | grep -q .; then
  bad "the builder's home folder in: $(grep -rlaF "$HOME" "$root" | head -3 | sed "s|$root||")"
fi
for key in user.name user.email; do
  v=$(git -C "$src" config "$key" 2>/dev/null || true)
  if [ -n "$v" ] && grep -rlaF "$v" "$root" | grep -q .; then
    bad "the git $key of the builder in: $(grep -rlaF "$v" "$root" | head -3 | sed "s|$root||")"
  fi
done
mak_sums=$(sed -n 's/.*"sha256": *"\([0-9a-f]\{64\}\)".*/\1/p' "$src"/data/demo/mak/*.json | sort -u)
if [ -n "$mak_sums" ]; then
  find "$root" -type f -size +100k -print0 | xargs -0 sha256sum | awk '{print $1}' | sort -u \
    | grep -qxF "$mak_sums" && bad "a MAK sheet (by SHA-256) in the package (DEMO-12)"
else
  bad "no MAK sheet SHA-256 found in data/demo/mak/*.json"
fi

if command -v lintian >/dev/null; then
  lintian --no-tag-display-limit "$deb" || bad "lintian errors"
else
  echo "note: lintian not installed, skipped"
fi
[ $fail = 0 ] && echo "check_deb: OK ($(wc -l <<<"$list") entries, $(du -h "$deb" | cut -f1))"
exit $fail
