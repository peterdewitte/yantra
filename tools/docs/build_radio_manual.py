#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The radio plugins manual as one PDF (RAD-97): yantra-radios-manual-<version>.pdf.

usage: build_manual.py [--source DIR] [--out DIR] [--version X.Y.Z] [--chapter PATH:ID]...

Built from the repository's Markdown (never edited by hand), with the
tools of Yantra's PDF manual (spec 16.2): pandoc turns each chapter into
its document tree; this script gives every heading id the chapter's
prefix, turns links between the chapters into links inside the PDF and
links to other files of the source into their names; pandoc writes one
HTML page with a title page (version, build date, licence), a table of
contents with page numbers and numbered sections; WeasyPrint lays it out
(A4, tools/docs/radio_manual.css) and writes the PDF with bookmarks, working
internal and external links, document metadata, embedded fonts and tags.

Checks (DOC-20 style; any failure exits 1):
  - pandoc and WeasyPrint report no warning;
  - every image of the chapters exists and is in the layout;
  - every internal link resolves to an anchor of the document;
  - the bookmarks are exactly the chapters' headings (levels 1-3) in
    order, and every chapter starts a page;
  - no text runs past the page margin or out of its table cell;
  - every font in the PDF is embedded; the PDF has metadata and a page
    count that matches the layout.
Exit 0 ok, 1 a problem, 2 usage, 77 pandoc or WeasyPrint missing (skip).
Output: <out>/yantra-radios-manual-<version>.pdf and the HTML it came
from (<out>/manual.html with img/), for the docs target and the release.
"""
import argparse
import datetime
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import zlib

# The user-facing parts (RAD-97), in order: (source, chapter id).
CHAPTERS = [
    ("docs/radio-manual/overview.md", "overview"),
    ("docs/plugins/simulator.md", "simulator"),
    ("docs/plugins/soapysdr.md", "soapysdr"),
    ("docs/plugins/hackrf.md", "hackrf"),
    ("docs/plugins/pei.md", "pei"),
    ("docs/TETRA_RECORDS.md", "records"),
    ("docs/RADIO_PLUGINS.md", "guide"),
]
TITLE = "yantra-radios manual"
AUTHOR = "The Yantra contributors"
PAGE_SLACK_PT = 0.75  # layout rounding


class Problems:
    def __init__(self):
        self.items = []

    def add(self, text):
        self.items.append(text)


def version_of(source):
    # The product version, one for the whole repository (file VERSION, PLG-29).
    with open(os.path.join(source, "VERSION"), encoding="utf-8") as f:
        m = re.match(r"([0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.]+)?)\s*$", f.readline())
    if not m:
        raise SystemExit("build_manual: no version in VERSION")
    return m.group(1)


def build_date(source):
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if not epoch:
        try:
            epoch = subprocess.run(["git", "-C", source, "log", "-1", "--format=%ct"], capture_output=True,
                                   text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            epoch = ""
    when = datetime.datetime.fromtimestamp(int(epoch), datetime.timezone.utc) if epoch else \
        datetime.datetime.now(datetime.timezone.utc)
    return when.strftime("%Y-%m-%d")


def pandoc(args, problems, what, stdin=None):
    p = subprocess.run(["pandoc", "--fail-if-warnings"] + args, input=stdin, capture_output=True, text=True)
    if p.returncode != 0 or p.stderr.strip():
        problems.add(f"pandoc on {what}: {p.stderr.strip() or 'exit ' + str(p.returncode)}")
    return p.stdout


def inline_text(inlines):
    out = []
    for x in inlines:
        t = x.get("t")
        if t == "Str":
            out.append(x["c"])
        elif t in ("Space", "SoftBreak", "LineBreak"):
            out.append(" ")
        elif t == "Code":
            out.append(x["c"][1])
        elif t in ("Emph", "Strong", "Strikeout", "Underline", "SmallCaps", "Superscript", "Subscript"):
            out.append(inline_text(x["c"]))
        elif t in ("Link", "Span"):
            out.append(inline_text(x["c"][1]))
        elif t == "Quoted":
            out.append(inline_text(x["c"][1]))
    return "".join(out)


class Chapter:
    def __init__(self, source, path, cid, problems):
        self.source = source
        self.path = path
        self.cid = cid
        self.problems = problems
        self.headings = []  # (level, text)
        self.images = []    # absolute paths
        self.last_level = 0

    def anchor(self, ident):
        return f"{self.cid}--{ident}" if ident else self.cid


def rewrite(node, chapter, by_file, problems):
    """Walks a pandoc JSON tree in place (ids, links, images)."""
    if isinstance(node, list):
        for x in node:
            rewrite(x, chapter, by_file, problems)
        return
    if not isinstance(node, dict) or "t" not in node:
        return
    t = node["t"]
    c = node.get("c")
    if t == "Header":
        # No skipped levels (h1 then h3 would number as 6.0.1): a heading
        # is at most one level below the one before it.
        c[0] = min(c[0], chapter.last_level + 1)
        chapter.last_level = c[0]
        level, attr, inlines = c
        if level == 1:
            attr[0] = chapter.cid
        else:
            attr[0] = chapter.anchor(attr[0])
        if level <= 3:
            chapter.headings.append((level, inline_text(inlines)))
    elif t in ("Div", "Span", "CodeBlock", "Code", "Table") and isinstance(c, list) and c and \
            isinstance(c[0], list) and len(c[0]) == 3 and c[0][0]:
        c[0][0] = chapter.anchor(c[0][0])
    if t == "Image":
        url = c[2][0]
        if not re.match(r"^[a-z]+:", url):
            path = os.path.normpath(os.path.join(os.path.dirname(chapter.path), url))
            if not os.path.isfile(path):
                problems.add(f"{os.path.relpath(chapter.path, chapter.source)}: image {url} is missing")
            chapter.images.append(path)
            c[2][0] = "img/" + os.path.basename(path)
    if t == "Link":
        url = c[2][0]
        if url.startswith("#"):
            c[2][0] = "#" + chapter.anchor(url[1:])
        elif not re.match(r"^[a-z]+:", url):
            target, _, frag = url.partition("#")
            path = os.path.normpath(os.path.join(os.path.dirname(chapter.path), target))
            other = by_file.get(path)
            if other is not None:
                c[2][0] = "#" + (other.anchor(frag) if frag else other.cid)
            else:
                # Another file of the source: its name, no link (the PDF
                # stands alone).
                rel = os.path.relpath(path, chapter.source).replace(os.sep, "/")
                if not os.path.exists(path):
                    problems.add(f"{os.path.relpath(chapter.path, chapter.source)}: link to missing {url}")
                label = inline_text(c[1])
                repl = {"t": "Span", "c": [["", ["source-file"], []], c[1]]}
                if label.strip() != rel and label.strip() != os.path.basename(rel):
                    repl["c"][1] = c[1] + [{"t": "Space"}, {"t": "Str", "c": "("},
                                           {"t": "Code", "c": [["", [], []], rel]}, {"t": "Str", "c": ")"}]
                node.clear()
                node.update(repl)
                return
    if c is not None:
        rewrite(c, chapter, by_file, problems)


def collect_ids(node, out):
    if isinstance(node, list):
        for x in node:
            collect_ids(x, out)
    elif isinstance(node, dict):
        c = node.get("c")
        if node.get("t") == "Header":
            out.add(c[1][0])
        elif node.get("t") in ("Div", "Span", "CodeBlock", "Code", "Table") and isinstance(c, list) and c and \
                isinstance(c[0], list) and len(c[0]) == 3 and c[0][0]:
            out.add(c[0][0])
        if c is not None:
            collect_ids(c, out)


def collect_links(node, out):
    if isinstance(node, list):
        for x in node:
            collect_links(x, out)
    elif isinstance(node, dict):
        if node.get("t") == "Link" and node["c"][2][0].startswith("#"):
            out.append(node["c"][2][0][1:])
        if node.get("c") is not None:
            collect_links(node["c"], out)


# ---------------------------------------------------------------- PDF


def pdf_objects(data):
    """Every object's dictionary text of a PDF, object streams included."""
    objs = {}
    for m in re.finditer(rb"(\d+)\s+0\s+obj\b(.*?)endobj", data, re.S):
        objs[int(m.group(1))] = m.group(2)
    for body in list(objs.values()):
        if b"/ObjStm" not in body:
            continue
        sm = re.search(rb"stream\r?\n(.*)endstream", body, re.S)
        n = re.search(rb"/N\s+(\d+)", body)
        first = re.search(rb"/First\s+(\d+)", body)
        if not (sm and n and first):
            continue
        raw = sm.group(1)
        try:
            raw = zlib.decompress(raw) if b"/FlateDecode" in body else raw
        except zlib.error:
            continue
        head = raw[:int(first.group(1))].split()
        nums = [(int(head[i]), int(head[i + 1])) for i in range(0, 2 * int(n.group(1)), 2)]
        for k, (num, off) in enumerate(nums):
            start = int(first.group(1)) + off
            end = int(first.group(1)) + nums[k + 1][1] if k + 1 < len(nums) else len(raw)
            objs[num] = raw[start:end]
    return objs


def check_pdf(path, problems, pages):
    with open(path, "rb") as f:
        data = f.read()
    objs = pdf_objects(data)
    descriptors = [b for b in objs.values() if b"/Type /FontDescriptor" in b or b"/Type/FontDescriptor" in b]
    fonts = [b for b in objs.values() if re.search(rb"/Type\s*/Font\b", b)]
    if not descriptors:
        problems.add("PDF: no font descriptor found")
    for d in descriptors:
        if not re.search(rb"/FontFile[23]?\s", d):
            name = re.search(rb"/FontName\s*/([^\s/]+)", d)
            problems.add(f"PDF: font {name.group(1).decode() if name else '?'} is not embedded")
    for b in fonts:  # Type 3 fonts have no descriptor and are drawn inline; base 14 fonts are refused
        if re.search(rb"/Subtype\s*/Type1\b", b) and b"/FontDescriptor" not in b:
            problems.add("PDF: a base-14 font is used without embedding")
    if not re.search(rb"/Outlines\s+\d+\s+0\s+R", data) and b"/Outlines" not in b"".join(objs.values()):
        problems.add("PDF: no bookmarks (outline)")
    info = b"".join(objs.values())
    for key in (b"/Title", b"/Author"):
        if key not in info:
            problems.add(f"PDF: no {key.decode()[1:]} in the metadata")
    count = len([b for b in objs.values() if re.search(rb"/Type\s*/Page\b", b)])
    if count != pages:
        problems.add(f"PDF: {count} pages, the layout has {pages}")
    return len(descriptors)


# ---------------------------------------------------------------- layout checks


def walk_boxes(box, ancestors, visit):
    visit(box, ancestors)
    children = getattr(box, "children", None)
    if children:
        ancestors.append(box)
        for child in children:
            walk_boxes(child, ancestors, visit)
        ancestors.pop()


def check_layout(document, problems, expected_headings, chapter_ids):
    from weasyprint.formatting_structure import boxes

    # bookmarks = the chapters' headings
    got = []

    def flatten(tree, level):
        for b in tree:
            label, children = b[0], b[2]  # (label, destination, children, state)
            got.append((level, re.sub(r"^[0-9.]+\s+", "", label).strip()))
            flatten(children, level + 1)

    flatten(document.make_bookmark_tree(), 1)
    want = [(lvl, re.sub(r"\s+", " ", t).strip()) for lvl, t in expected_headings]
    got = [(lvl, re.sub(r"\s+", " ", t)) for lvl, t in got]
    if got != want:
        for i, (g, w) in enumerate(zip(got + [None] * len(want), want + [None] * len(got))):
            if g != w:
                problems.add(f"bookmarks differ from the headings at #{i + 1}: {g} != {w}")
                break

    anchors = {}
    for i, page in enumerate(document.pages):
        for name in page.anchors:
            anchors[name] = i
    for i, page in enumerate(document.pages):
        for link in page.links:
            kind, target = link[0], link[1]
            if kind == "internal" and target not in anchors:
                problems.add(f"page {i + 1}: internal link to #{target} does not resolve")
    for cid in chapter_ids:
        if cid not in anchors:
            problems.add(f"chapter {cid} is not in the layout")

    images = 0
    for i, page in enumerate(document.pages):
        pb = page._page_box
        left = pb.content_box_x() - PAGE_SLACK_PT
        right = pb.content_box_x() + pb.width + PAGE_SLACK_PT
        bottom = pb.content_box_y() + pb.height + PAGE_SLACK_PT

        def visit(box, ancestors):
            nonlocal images
            if isinstance(box, boxes.ReplacedBox) or isinstance(box, boxes.InlineReplacedBox):
                images += 1
            if not isinstance(box, (boxes.TextBox, boxes.ReplacedBox)):
                return
            if any(isinstance(a, boxes.MarginBox) for a in ancestors):
                return
            x0, x1 = box.position_x, box.position_x + box.margin_width()
            text = getattr(box, "text", "image")[:40]
            if x0 < left or x1 > right or box.position_y + box.margin_height() > bottom:
                problems.add(f"page {i + 1}: '{text}' runs past the page margin")
                return
            cell = next((a for a in reversed(ancestors) if isinstance(a, boxes.TableCellBox)), None)
            if cell is not None:
                cx1 = cell.content_box_x() + cell.width + cell.padding_right + PAGE_SLACK_PT
                if x1 > cx1:
                    problems.add(f"page {i + 1}: '{text}' runs out of its table cell")

        walk_boxes(pb, [], visit)
    return images


# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--source", default=os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--out", default="manual")
    ap.add_argument("--version")
    ap.add_argument("--chapter", action="append", metavar="PATH:ID",
                    help="a chapter (source-relative Markdown path and id) instead of the manual's list; "
                         "for the checker's own test")
    args = ap.parse_args()
    source = os.path.abspath(args.source)
    out = os.path.abspath(args.out)
    if shutil.which("pandoc") is None:
        print("build_manual: SKIPPED: pandoc was not found (RAD-97)")
        return 77
    try:
        import weasyprint
    except ImportError:
        print("build_manual: SKIPPED: WeasyPrint was not found (RAD-97)")
        return 77
    version = args.version or version_of(source)
    date = build_date(source)
    problems = Problems()

    plan = [tuple(c.rsplit(":", 1)) for c in args.chapter] if args.chapter else CHAPTERS
    chapters = [Chapter(source, os.path.join(source, p), cid, problems) for p, cid in plan]
    by_file = {ch.path: ch for ch in chapters}
    blocks = []
    api = None
    for ch in chapters:
        if not os.path.isfile(ch.path):
            problems.add(f"chapter {ch.path} is missing")
            continue
        tree = json.loads(pandoc(["-f", "gfm+implicit_figures", "-t", "json", ch.path], problems, ch.path) or "{}")
        if not tree:
            continue
        api = tree["pandoc-api-version"]
        rewrite(tree["blocks"], ch, by_file, problems)
        h1 = [t for lvl, t in ch.headings if lvl == 1]
        if len(h1) != 1:
            problems.add(f"{ch.path}: {len(h1)} level-1 headings (one per chapter)")
        blocks += tree["blocks"]
    doc = {"pandoc-api-version": api, "meta": {}, "blocks": blocks}
    ids, links = set(), []
    collect_ids(blocks, ids)
    collect_links(blocks, links)
    for target in sorted(set(links) - ids):
        problems.add(f"link to #{target}: no such heading or anchor")

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "img"))
    for ch in chapters:
        for img in ch.images:
            if os.path.isfile(img):
                shutil.copy2(img, os.path.join(out, "img", os.path.basename(img)))

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "radio_manual.css"), encoding="utf-8") as f:
        css = f.read()
    header = os.path.join(out, ".header.html")
    with open(header, "w", encoding="utf-8") as f:
        f.write(f"<style>\n{css}\n</style>\n"
                f'<meta name="author" content="{AUTHOR}">\n'
                '<meta name="description" content="Radio plugins of Yantra: plugins, record schemas, '
                'radio API guide and tutorial">\n'
                '<meta name="keywords" content="TETRA, radio plugins, Yantra, SDR, PEI">\n'
                f'<meta name="dcterms.created" content="{date}">\n')
    front = os.path.join(out, ".title.html")
    with open(front, "w", encoding="utf-8") as f:
        f.write(f'<div class="title-page" data-version="{version}">\n'
                '<p class="name">yantra-radios</p>\n'
                '<p class="what">Radio plugins for Yantra: manual</p>\n'
                f"<p>Version {version}</p>\n"
                f"<p>Built {date}</p>\n"
                "<p>Licence: GPL-3.0-or-later (the TETRA plugins with AGPL-3.0 parts). "
                "Copyright 2026 The Yantra contributors.</p>\n"
                "</div>\n")
    html = os.path.join(out, "manual.html")
    pandoc(["-f", "json", "-t", "html5", "--standalone", "--template", os.path.join(here, "radio_manual.template.html"), "--toc", "--toc-depth=2", "--number-sections",
            "-M", f"pagetitle={TITLE} {version}", "-M", "title-meta=" + f"{TITLE} {version}",
            "-M", "lang=en", "-M", "toc-title=Contents", "-M", f"author-meta={AUTHOR}",
            "--include-in-header", header, "--include-before-body", front, "-o", html],
           problems, "the manual", stdin=json.dumps(doc))
    os.remove(header)
    os.remove(front)
    if not os.path.isfile(html):
        for p in problems.items:
            print(f"build_manual: {p}", file=sys.stderr)
        return 1

    # WeasyPrint warnings are problems (missing images, bad CSS, fonts).
    class Collect(logging.Handler):
        def emit(self, record):
            problems.add(f"WeasyPrint: {record.getMessage()}")

    logger = logging.getLogger("weasyprint")
    logger.addHandler(Collect(level=logging.WARNING))
    logger.setLevel(logging.WARNING)
    document = weasyprint.HTML(filename=html).render()
    expected = [h for ch in chapters for h in ch.headings]
    images = check_layout(document, problems, expected, [ch.cid for ch in chapters])
    want_images = sum(len(ch.images) for ch in chapters)
    if images < want_images:
        problems.add(f"{want_images} images in the chapters, {images} in the layout")
    pdf = os.path.join(out, f"yantra-radios-manual-{version}.pdf")
    document.write_pdf(pdf, pdf_tags=True)
    fonts = check_pdf(pdf, problems, len(document.pages))

    if problems.items:
        for p in problems.items:
            print(f"build_manual: {p}", file=sys.stderr)
        print(f"build_manual: {len(problems.items)} problem(s)", file=sys.stderr)
        return 1
    print(f"build_manual: {pdf}: {len(document.pages)} pages, {len(chapters)} chapters, "
          f"{len(expected)} bookmarks, {images} images, {fonts} embedded fonts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
