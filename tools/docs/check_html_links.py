#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link check of the built documentation pages (DOC-20, SCR-07), offline.

usage: check_html_links.py OUT [--source ROOT] [--external FILE]

In the pages tools/docs/build_docs.sh made below OUT, every relative
href/src (links, pictures, style sheets) resolves to a file below OUT
and every "#fragment" to an id of the target page. With --source ROOT,
a link to a file that is not built (LICENSE, a source file, a folder)
may resolve in the source tree instead. The Doxygen pages of the plugin
API reference (OUT/docs/plugin-api) are Doxygen's own and not checked.
External links (http, https, mailto) are never fetched: --external FILE
lists them, one "page: url" per line.
Exit: 0 ok, 1 problems.
Copied from yantra-v1 c24e8f5: tools/docs/check_links.py (HTML mode).
"""
import argparse
import html.parser
import pathlib
import sys
import urllib.parse

EXTERNAL = ("http://", "https://", "mailto:", "ftp://")
SKIP = ("docs/plugin-api/",)


class _Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids, self.refs = set(), []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        for k in ("id", "name"):
            if a.get(k):
                self.ids.add(a[k])
        if tag == "a" and a.get("href"):
            self.refs.append(a["href"])
        if tag in ("img", "link", "script", "source", "video") and (a.get("src") or a.get("href")):
            self.refs.append(a.get("src") or a.get("href"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--source")
    ap.add_argument("--external")
    a = ap.parse_args()
    out = pathlib.Path(a.out).resolve()
    source = pathlib.Path(a.source).resolve() if a.source else None
    pages = {}
    for p in sorted(out.rglob("*.html")):
        page = _Page()
        page.feed(p.read_text(encoding="utf-8"))
        pages[p.resolve()] = page
    problems, external, checked = [], [], 0
    for p, page in pages.items():
        rel = p.relative_to(out)
        if str(rel).startswith(SKIP):
            continue
        checked += 1
        for ref in page.refs:
            if ref.startswith(EXTERNAL):
                external.append(f"{rel}: {ref}")
                continue
            if ref.startswith("data:"):
                continue
            path, _, frag = ref.partition("#")
            dest = (p.parent / urllib.parse.unquote(path)).resolve() if path else p
            if not dest.exists() and source is not None and dest.is_relative_to(out):
                # A link to a file that is not a page (LICENSE, a header):
                # fine when it exists at the same place in the source tree.
                dest = source / dest.relative_to(out)
            if not dest.exists():
                problems.append(f"{rel}: link to missing {ref}")
            elif frag and dest in pages and urllib.parse.unquote(frag) not in pages[dest].ids:
                problems.append(f"{rel}: no id \"#{frag}\" in {dest.relative_to(out)}")
    for pr in problems:
        print(pr)
    if a.external:
        pathlib.Path(a.external).write_text("".join(e + "\n" for e in external), encoding="utf-8")
    print(f"check_html_links: {len(problems)} problem(s), {checked} page(s), {len(external)} external link(s) "
          "listed, not fetched")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
