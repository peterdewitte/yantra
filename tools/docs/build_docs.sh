#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the documentation from Markdown (DOC-20, DOC-02, DOC-03, DOC-06):
#   OUT/docs/manual/manual.html          the user manual on one page, with
#                                        the Yantra logo on its title page (BRD-06)
#   OUT/docs/manual/yantra-manual-<version>.pdf  the PDF manual (DOC-02a,
#                                        tools/docs/build_manual.py with its checks)
#   OUT/docs/quickstart/*.html + .pdf    the quick starts (+ nl/, fr/, de/)
#   OUT/docs/METHOD.*, OUT/docs/LEGAL.*  method and legal notes
#   OUT/docs/plugin-api/                 the plugin API reference (Doxygen,
#                                        DOC-17), when --plugin-api DIR is given
#   OUT/**.html                          every other page (README, BUILD,
#                                        formats, plugins, ...) for links
# with the pictures (docs/img) and samples (docs/samples) copied, then
# checks the links: relative links and anchors of the Markdown sources
# (tools/ci/check_doc_links.py) and of the built pages, images included
# (tools/docs/check_html_links.py). External links are listed in
# OUT/external-links.txt and never fetched. Fully offline.
#
# Usage: build_docs.sh OUT [--root DIR] [--plugin-api DIR] [--html-only] [--require-pdf]
# PDF: pandoc writes HTML, WeasyPrint (spec 16.2 build tool) turns it into
# PDF (pandoc's --pdf-engine=weasyprint resolves relative pictures from the
# wrong folder). Without WeasyPrint the HTML is built and the PDF skipped
# (--require-pdf: fail instead).
# Exit: 0 ok, 1 build or link problems, 77 pandoc missing (ctest skip).
# Copied from yantra-v1 c24e8f5: tools/docs/build_docs.sh, adapted.
set -euo pipefail

out="" root="" api="" html_only=0 require_pdf=0
while [ $# -gt 0 ]; do
  case $1 in
    --root) root=$2; shift ;;
    --plugin-api) api=$2; shift ;;
    --html-only) html_only=1 ;;
    --require-pdf) require_pdf=1 ;;
    -h|--help) sed -n '4,26p' "$0"; exit 0 ;;
    *) out=$1 ;;
  esac
  shift
done
[ -n "$out" ] || { echo "usage: build_docs.sh OUT [--root DIR] [--plugin-api DIR] [--html-only] [--require-pdf]" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${root:-$here/../..}" && pwd)

if ! command -v pandoc >/dev/null; then
  echo "build_docs: pandoc not found, skipped (apt install pandoc)"; exit 77
fi
pdf=1
if [ $html_only = 1 ]; then
  pdf=0
elif ! command -v weasyprint >/dev/null; then
  if [ $require_pdf = 1 ]; then echo "build_docs: weasyprint not found (--require-pdf)" >&2; exit 1; fi
  echo "build_docs: weasyprint not found, HTML only (apt install weasyprint for the PDFs)"
  pdf=0
fi

rm -rf "$out"
mkdir -p "$out/docs"
out=$(cd "$out" && pwd)
log=$out/build.log
: >"$log"
cp -r "$root/docs/img" "$out/docs/img"
[ -d "$root/docs/samples" ] && cp -r "$root/docs/samples" "$out/docs/samples"
if [ -n "$api" ]; then
  [ -f "$api/index.html" ] || { echo "build_docs: no plugin API reference in $api (ninja docs-plugin-api)" >&2; exit 1; }
  cp -r "$api" "$out/docs/plugin-api"
fi
style=$out/.style.html
{ echo "<style>"; cat "$here/docs.css"; echo "</style>"; } >"$style"
version=$(sed -n '/^project(Yantra/,/)/s/.*VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)
# Reproducible output: no build date in the pages or PDFs. The PDF manual
# names its build date: the caller's SOURCE_DATE_EPOCH, else the date of
# the last commit (build_manual.py).
manual_epoch=${SOURCE_DATE_EPOCH:-}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1794096000}

from="markdown+gfm_auto_identifiers"
common=(-f "$from" -t html5 --standalone --lua-filter "$here/pandoc_links.lua"
        --include-in-header "$style" -M "subtitle=Yantra $version")

title_of() { sed -n 's/^# \(.*\)$/\1/p' "$1" | head -1; }
lang_of() {
  case $1 in */nl/*) echo nl ;; */fr/*) echo fr ;; */de/*) echo de ;; *) echo en ;; esac
}

pages=0
build_page() {  # build_page SRC.md  -> OUT/<same path>.html
  local src=$1 rel dst t
  rel=${src#"$root"/}
  dst=$out/${rel%.md}.html
  mkdir -p "$(dirname "$dst")"
  t=$(title_of "$src"); t=${t:-$(basename "$src" .md)}
  pandoc "${common[@]}" -M "pagetitle=$t" -M "lang=$(lang_of "$rel")" -o "$dst" "$src" >>"$log" 2>&1 \
    || { echo "build_docs: pandoc failed on $rel (see $log)" >&2; exit 1; }
  pages=$((pages + 1))
}

for f in README.md BUILD.md ARCHITECTURE.md CONTRIBUTING.md CONTRIBUTORS.md CHANGELOG.md THIRD_PARTY.md; do
  if [ -f "$root/$f" ]; then build_page "$root/$f"; fi
done
while IFS= read -r f; do build_page "$f"; done < <(find "$root/docs" -name '*.md' | sort)
# Pages outside docs/ that the docs link to: the demo plan's source and
# licence, and the Markdown pages next to the code (the README of each
# plugin, of the radio API and of the radio parts' common code, tools,
# data; PLG-29), without third_party/ and Rust build output.
if [ -f "$root/data/demo/fraunces/README.md" ]; then build_page "$root/data/demo/fraunces/README.md"; fi
for d in radio-api plugins radio-common gen tools udev tests/radio; do
  [ -d "$root/$d" ] || continue
  while IFS= read -r f; do build_page "$f"; done < <(find "$root/$d" -name target -prune -o -name '*.md' -print | sort)
done

# The manual on one page: its README (Yantra logo, title, introduction,
# chapter list; BRD-06 title page) before the table of contents, then the
# chapters in order; links between chapters become in-page anchors
# (pandoc_links.lua); table of contents down to N.M.
chapters=("$root"/docs/manual/[0-9][0-9]-*.md)
meta=()
for c in "${chapters[@]}"; do meta+=(-M "chapters=$(basename "$c") $(title_of "$c")"); done
front=$out/.manual-front.html
pandoc -f "$from" -t html5 --lua-filter "$here/pandoc_links.lua" -M combined=true "${meta[@]}" \
  -o "$front" "$root/docs/manual/README.md" >>"$log" 2>&1 \
  || { echo "build_docs: pandoc failed on the manual's title page (see $log)" >&2; exit 1; }
pandoc "${common[@]}" -M "pagetitle=Yantra user manual" -M lang=en -M combined=true "${meta[@]}" \
  --include-before-body "$front" --toc --toc-depth=2 -o "$out/docs/manual/manual.html" "${chapters[@]}" >>"$log" 2>&1 \
  || { echo "build_docs: pandoc failed on the manual (see $log)" >&2; exit 1; }
rm -f "$front"
pages=$((pages + 1))

pdfs=0
if [ $pdf = 1 ]; then
  to_pdf() {  # weasyprint resolves the ../img pictures from the page's own folder
    weasyprint -q "$1" "${1%.html}.pdf" >>"$log" 2>&1 \
      || { echo "build_docs: weasyprint failed on ${1#"$out"/} (see $log)" >&2; exit 1; }
    pdfs=$((pdfs + 1))
  }
  # The manual: one PDF with title page, contents, bookmarks, index and
  # its DOC-20 checks (DOC-02a).
  SOURCE_DATE_EPOCH=$manual_epoch python3 -B "$here/build_manual.py" --source "$root" --out "$out/.manual-pdf" \
      --version "$version" >>"$log" 2>&1 \
    || { echo "build_docs: the PDF manual failed its build or checks:" >&2; grep '^build_manual' "$log" >&2; exit 1; }
  mv "$out/.manual-pdf/yantra-manual-$version.pdf" "$out/docs/manual/"
  rm -rf "$out/.manual-pdf"
  pdfs=$((pdfs + 1))
  for f in "$out"/docs/quickstart/*.html "$out"/docs/quickstart/*/*.html "$out/docs/METHOD.html" \
           "$out/docs/LEGAL.html"; do
    if [ -f "$f" ]; then to_pdf "$f"; fi
  done
fi
rm -f "$style"

status=0
python3 -B "$root/tools/ci/check_doc_links.py" "$root" || status=1
python3 -B "$here/check_html_links.py" "$out" --source "$root" --external "$out/external-links.txt" || status=1
echo "build_docs: $pages HTML page(s), $pdfs PDF(s) in $out$([ $pdf = 0 ] && echo ' (no PDF)')"
exit $status
