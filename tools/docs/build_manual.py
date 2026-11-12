#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The user manual as one PDF (DOC-02a): yantra-manual-<version>.pdf.

usage: build_manual.py [--source DIR] [--out DIR] [--version X.Y.Z] [--chapter PATH:ID]...
                       [--index FILE]

Built from the manual's Markdown (docs/manual/NN-*.md, never edited by
hand) with the build tools of spec 16.2: pandoc turns each chapter into
its document tree; this script gives every heading id the chapter's
prefix, turns section references such as "(13.2)" and links between the
chapters into links inside the PDF, links to other files of the source
into their names, and each screenshot (light theme, the dark one's
"light theme" link dropped) into a numbered figure; it adds the index
(tools/docs/manual_index.txt: main terms, windows and menus, each with the
sections that name it). pandoc writes one HTML page with the title page
(the Yantra logo BRD-05, version, build date, licence, curtat.be) and a
table of contents; WeasyPrint lays it out (A4, tools/docs/manual.css,
Inter and JetBrains Mono from data/fonts as files) and writes a tagged
PDF (PDF/UA-1) with bookmarks, internal and external links and metadata.

Checks (DOC-20; any failure exits 1):
  - pandoc and WeasyPrint report no warning;
  - every image of the chapters exists and is in the layout;
  - every internal link resolves to an anchor of the document;
  - the bookmarks are exactly the chapters' headings (levels 1-3) and the
    index, in order; every chapter is in the layout;
  - every index term is found in the chapters;
  - no text runs past the page margin or out of its table cell (TST-52);
  - every font in the PDF is embedded; the PDF has metadata and a page
    count that matches the layout.
Exit 0 ok, 1 a problem, 2 usage, 77 pandoc or WeasyPrint missing (skip).
Output: <out>/yantra-manual-<version>.pdf and the HTML it came from
(<out>/manual.html with img/ and fonts/).
Adapted from yantra-radios 9c6d3c9: tools/docs/build_manual.py (a3a6bcc).
"""
import argparse
import datetime
import glob
import html
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import zlib

TITLE = "Yantra user manual"
AUTHOR = "Curtat BV"
PAGE_SLACK_PT = 0.75  # layout rounding
FONTS = ["Inter-Regular.ttf", "Inter-SemiBold.ttf", "JetBrainsMono-Regular.ttf"]
INDEX_ID = "manual-index"


class Problems:
    def __init__(self):
        self.items = []

    def add(self, text):
        self.items.append(text)


def version_of(source):
    with open(os.path.join(source, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r"project\(Yantra\s+VERSION ([0-9.]+)", f.read())
    if not m:
        raise SystemExit("build_manual: no project version in CMakeLists.txt")
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
        elif t in ("Link", "Span", "Quoted"):
            out.append(inline_text(x["c"][1]))
        elif t == "Image":
            out.append(inline_text(x["c"][1]))
    return "".join(out)


def block_text(node):
    """All text of a block (lists, tables and quotes included)."""
    out = []

    def walk(n):
        if isinstance(n, list):
            for x in n:
                walk(x)
        elif isinstance(n, dict):
            t = n.get("t")
            if t == "Str":
                out.append(n["c"])
            elif t in ("Space", "SoftBreak", "LineBreak"):
                out.append(" ")
            elif t in ("Code", "CodeBlock"):
                out.append(" " + n["c"][1] + " ")
            elif "c" in n:
                walk(n["c"])
    walk(node)
    return "".join(out)


class Chapter:
    def __init__(self, source, path, cid, problems):
        self.source = source
        self.path = path
        self.cid = cid
        self.problems = problems
        self.headings = []  # (level, text, anchor)
        self.images = []    # absolute paths
        self.last_level = 0

    def anchor(self, ident):
        return f"{self.cid}--{ident}" if ident else self.cid


def is_image_file(url):
    return re.search(r"\.(png|jpe?g|gif|webp|svg)$", url, re.I) is not None


def print_image(path):
    """The light-theme screenshot for print when the source shows the dark one."""
    light = re.sub(r"_dark(\.[a-z]+)$", r"_light\1", path)
    return light if light != path and os.path.isfile(light) else path


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
        # No skipped levels (h1 then h3): a heading is at most one level
        # below the one before it.
        c[0] = min(c[0], chapter.last_level + 1)
        chapter.last_level = c[0]
        level, attr, inlines = c
        attr[0] = chapter.cid if level == 1 else chapter.anchor(attr[0])
        if level <= 3:
            chapter.headings.append((level, inline_text(inlines), attr[0]))
    elif t in ("Div", "Span", "CodeBlock", "Code", "Table") and isinstance(c, list) and c and \
            isinstance(c[0], list) and len(c[0]) == 3 and c[0][0]:
        c[0][0] = chapter.anchor(c[0][0])
    if t == "Image":
        url = c[2][0]
        if not re.match(r"^[a-z]+:", url):
            path = os.path.normpath(os.path.join(os.path.dirname(chapter.path), url))
            if not os.path.isfile(path):
                problems.add(f"{os.path.relpath(chapter.path, chapter.source)}: image {url} is missing")
            else:
                path = print_image(path)
            chapter.images.append(path)
            c[2][0] = "img/" + os.path.basename(path)
    if t == "Link":
        url = c[2][0]
        if url.startswith("#"):
            c[2][0] = "#" + chapter.anchor(url[1:])
        elif not re.match(r"^[a-z]+:", url) and is_image_file(url):
            # The link to the other theme's screenshot: the PDF shows one.
            node.clear()
            node.update({"t": "Str", "c": ""})
            return
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


def figures(blocks, chapter):
    """A paragraph that holds a screenshot (and the link to its other
    theme) becomes a figure with the picture's text as its caption."""
    out = []
    for b in blocks:
        if isinstance(b, dict) and b.get("t") in ("Para", "Plain"):
            inl = [x for x in b["c"] if not (x.get("t") == "Str" and x["c"] == "")]
            core = [x for x in inl if x.get("t") not in ("Space", "SoftBreak")]
            if len(core) == 1 and core[0].get("t") == "Image":
                img = core[0]
                caption = img["c"][1]
                out.append({"t": "Figure", "c": [["", [], []], [None, [{"t": "Plain", "c": caption}]],
                                                 [{"t": "Plain", "c": [img]}]]})
                continue
            b["c"] = inl
        out.append(b)
    return out


SECTION_REF = re.compile(r"^(\(?)(\d{1,2}(?:\.\d{1,2})?)(\)?[.,;:]?)$")


def section_links(node, sections, in_parens=False):
    """Section numbers in parentheses ("(13.2)", "(6.1, 7.2)") become
    links to those sections (DOC-02a cross-references)."""
    if isinstance(node, list):
        i = 0
        depth = 0
        while i < len(node):
            x = node[i]
            if isinstance(x, dict) and x.get("t") == "Str":
                s = x["c"]
                m = SECTION_REF.match(s)
                opened = s.startswith("(")
                if m and (opened or depth > 0) and m.group(2) in sections and "." in m.group(2):
                    parts = []
                    if m.group(1):
                        parts.append({"t": "Str", "c": m.group(1)})
                    parts.append({"t": "Link", "c": [["", ["section-ref"], []], [{"t": "Str", "c": m.group(2)}],
                                                     ["#" + sections[m.group(2)], ""]]})
                    if m.group(3):
                        parts.append({"t": "Str", "c": m.group(3)})
                    node[i:i + 1] = parts
                    i += len(parts)
                    depth += s.count("(") - s.count(")")
                    continue
                depth += s.count("(") - s.count(")")
                depth = max(depth, 0)
            elif isinstance(x, dict) and x.get("t") not in ("Link", "Code", "CodeBlock", "Header", "Image"):
                if "c" in x:
                    section_links(x["c"], sections)
            i += 1
    elif isinstance(node, dict) and node.get("t") not in ("Link", "Code", "CodeBlock", "Header", "Image"):
        if "c" in node:
            section_links(node["c"], sections)


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


# ---------------------------------------------------------------- index


def read_index(path, problems):
    """[terms] / [windows and menus] sections; a line is "Entry" or
    "Entry: phrase; phrase" (the phrases found in the text, the entry
    itself when none is given)."""
    groups, current = [], None
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"^\[(.+)\]$", line)
            if m:
                current = (m.group(1), [])
                groups.append(current)
                continue
            if current is None:
                problems.add(f"{path}:{n}: an entry before the first [section]")
                continue
            entry, _, phrases = line.partition(":")
            words = [p.strip() for p in phrases.split(";") if p.strip()] or [entry.strip()]
            current[1].append((entry.strip(), words))
    return groups


def index_hits(blocks, groups):
    """For every entry, the anchors (in order) of the sections whose text
    names it."""
    patterns = {}
    for _, entries in groups:
        for entry, words in entries:
            patterns[entry] = re.compile(r"(?<![\w-])(" + "|".join(re.escape(w) for w in words) + r")(?![\w-])",
                                         re.I)
    hits = {e: [] for e in patterns}
    anchor = None
    for b in blocks:
        if b.get("t") == "Header" and b["c"][0] <= 3:
            anchor = b["c"][1][0]
        if anchor is None:
            continue
        text = block_text(b)
        for entry, pat in patterns.items():
            if pat.search(text) and anchor not in hits[entry]:
                hits[entry].append(anchor)
    return hits


def index_html(groups, hits, pages):
    """The index chapter; with `pages` (anchor -> page number) the page
    numbers of an entry are listed once each."""
    out = [f'<section class="index" id="{INDEX_ID}">', '<h1 class="unnumbered">Index</h1>']
    for title, entries in groups:
        out.append(f'<h2 class="index-group">{html.escape(title)}</h2>')
        out.append('<ul class="index-list">')
        for entry, _ in sorted(entries, key=lambda e: e[0].lower()):
            refs, seen = [], set()
            for a in hits.get(entry, []):
                p = pages.get(a) if pages else None
                if pages and (p is None or p in seen):
                    continue
                seen.add(p)
                refs.append(f'<a class="index-ref" href="#{a}"></a>')
            out.append(f'<li><span class="index-entry">{html.escape(entry)}</span> {", ".join(refs)}</li>')
        out.append("</ul>")
    out.append("</section>")
    return "\n".join(out)


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
    names = set()
    for d in descriptors:
        name = re.search(rb"/FontName\s*/([^\s/]+)", d)
        names.add(name.group(1).decode().split("+")[-1] if name else "?")
        if not re.search(rb"/FontFile[23]?\s", d):
            problems.add(f"PDF: font {name.group(1).decode() if name else '?'} is not embedded")
    for b in fonts:  # base 14 fonts have no descriptor
        if re.search(rb"/Subtype\s*/Type1\b", b) and b"/FontDescriptor" not in b:
            problems.add("PDF: a base-14 font is used without embedding")
    if not any(n.startswith("Inter") for n in names):
        problems.add(f"PDF: Inter is not used (fonts: {sorted(names)})")
    everything = b"".join(objs.values())
    if b"/Outlines" not in everything:
        problems.add("PDF: no bookmarks (outline)")
    for key in (b"/Title", b"/Author", b"/Subject", b"/Lang", b"/StructTreeRoot"):
        if key not in everything:
            problems.add(f"PDF: no {key.decode()[1:]} (metadata, language, tags)")
    count = len([b for b in objs.values() if re.search(rb"/Type\s*/Page\b", b)])
    if count != pages:
        problems.add(f"PDF: {count} pages, the layout has {pages}")
    return sorted(names)


# ---------------------------------------------------------------- layout checks


def walk_boxes(box, ancestors, visit):
    visit(box, ancestors)
    children = getattr(box, "children", None)
    if children:
        ancestors.append(box)
        for child in children:
            walk_boxes(child, ancestors, visit)
        ancestors.pop()


def anchor_pages(document):
    pages = {}
    for i, page in enumerate(document.pages):
        for name in page.anchors:
            pages.setdefault(name, i + 1)
    return pages


def check_layout(document, problems, expected_headings, chapter_ids):
    from weasyprint.formatting_structure import boxes

    got = []

    def flatten(tree, level):
        for b in tree:
            label, children = b[0], b[2]  # (label, destination, children, state)
            got.append((level, label.strip()))
            flatten(children, level + 1)

    flatten(document.make_bookmark_tree(), 1)
    want = [(lvl, re.sub(r"\s+", " ", t).strip()) for lvl, t in expected_headings]
    got = [(lvl, re.sub(r"\s+", " ", t)) for lvl, t in got]
    if got != want:
        for i, (g, w) in enumerate(zip(got + [None] * len(want), want + [None] * len(got))):
            if g != w:
                problems.add(f"bookmarks differ from the headings at #{i + 1}: {g} != {w}")
                break

    anchors = anchor_pages(document)
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
            if isinstance(box, (boxes.ReplacedBox, boxes.InlineReplacedBox)):
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
    return images, anchors


# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--source", default=os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--out", default="manual")
    ap.add_argument("--version")
    ap.add_argument("--index", help="the index terms (default tools/docs/manual_index.txt)")
    ap.add_argument("--chapter", action="append", metavar="PATH:ID",
                    help="a chapter (source-relative Markdown path and id) instead of the manual's chapters; "
                         "for the checker's own test")
    args = ap.parse_args()
    source = os.path.abspath(args.source)
    out = os.path.abspath(args.out)
    here = os.path.dirname(os.path.abspath(__file__))
    if shutil.which("pandoc") is None:
        print("build_manual: SKIPPED: pandoc was not found (DOC-02a)")
        return 77
    try:
        import weasyprint
    except ImportError:
        print("build_manual: SKIPPED: WeasyPrint was not found (DOC-02a)")
        return 77
    version = args.version or version_of(source)
    date = build_date(source)
    problems = Problems()

    if args.chapter:
        plan = [tuple(c.rsplit(":", 1)) for c in args.chapter]
    else:
        files = sorted(glob.glob(os.path.join(source, "docs", "manual", "[0-9][0-9]-*.md")))
        plan = [(os.path.relpath(f, source), os.path.splitext(os.path.basename(f))[0]) for f in files]
        if not plan:
            problems.add("no chapters in docs/manual")
    chapters = [Chapter(source, os.path.join(source, p), cid, problems) for p, cid in plan]
    by_file = {ch.path: ch for ch in chapters}
    blocks = []
    api = None
    for ch in chapters:
        if not os.path.isfile(ch.path):
            problems.add(f"chapter {ch.path} is missing")
            continue
        tree = json.loads(pandoc(["-f", "gfm", "-t", "json", ch.path], problems, ch.path) or "{}")
        if not tree:
            continue
        api = tree["pandoc-api-version"]
        rewrite(tree["blocks"], ch, by_file, problems)
        tree["blocks"] = figures(tree["blocks"], ch)
        h1 = [t for lvl, t, _ in ch.headings if lvl == 1]
        if len(h1) != 1:
            problems.add(f"{ch.path}: {len(h1)} level-1 headings (one per chapter)")
        blocks += tree["blocks"]
    # Section numbers of the headings ("7.2 Manual points" -> 7.2).
    sections = {}
    for ch in chapters:
        for lvl, text, anchor in ch.headings:
            m = re.match(r"^(\d+(?:\.\d+)*)\s", text)
            if m:
                sections[m.group(1)] = anchor
    section_links(blocks, sections)
    ids, links = set(), []
    collect_ids(blocks, ids)
    collect_links(blocks, links)
    for target in sorted(set(links) - ids):
        problems.add(f"link to #{target}: no such heading or anchor")

    groups = read_index(args.index or os.path.join(here, "manual_index.txt"), problems)
    hits = index_hits(blocks, groups)
    for _, entries in groups:
        for entry, words in entries:
            if not hits.get(entry):
                problems.add(f"index: '{entry}' is not found in the chapters ({'; '.join(words)})")

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "img"))
    os.makedirs(os.path.join(out, "fonts"))
    for ch in chapters:
        for img in ch.images:
            if os.path.isfile(img):
                shutil.copy2(img, os.path.join(out, "img", os.path.basename(img)))
    for font in FONTS:
        src = os.path.join(source, "data", "fonts", font)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(out, "fonts", font))
        else:
            problems.add(f"font data/fonts/{font} is missing")
    logo = os.path.join(source, "data", "logo", "yantra_logo.svg")
    if os.path.isfile(logo):
        shutil.copy2(logo, os.path.join(out, "img", "yantra_logo.svg"))
    else:
        problems.add("the Yantra logo data/logo/yantra_logo.svg is missing (BRD-05)")

    with open(os.path.join(here, "manual.css"), encoding="utf-8") as f:
        css = f.read()
    header = os.path.join(out, ".header.html")
    with open(header, "w", encoding="utf-8") as f:
        f.write(f"<style>\n{css}\n</style>\n"
                f'<meta name="author" content="{AUTHOR}">\n'
                '<meta name="description" content="User manual of Yantra, indoor TETRA coverage survey: '
                'radios, calibration, live survey, indoor projects, walk tests, reports">\n'
                '<meta name="keywords" content="Yantra, TETRA, indoor coverage, walk test, survey">\n'
                f'<meta name="dcterms.created" content="{date}">\n')
    front = os.path.join(out, ".title.html")
    with open(front, "w", encoding="utf-8") as f:
        f.write(f'<div class="title-page" data-version="{version}">\n'
                '<img class="logo" src="img/yantra_logo.svg" alt="Yantra logo">\n'
                '<p class="name">Yantra</p>\n'
                '<p class="what">User manual</p>\n'
                f'<p>Version {version}</p>\n'
                f'<p>Built {date}</p>\n'
                '<p>Licence: GPL-3.0-or-later (the radio plugins with AGPL-3.0 parts). '
                'Copyright 2026 The Yantra contributors.</p>\n'
                '<p class="maker">Yantra is made by curtat.be (<a href="https://curtat.be">https://curtat.be</a>) '
                "as a reference on building new programs with Claude, Anthropic's AI assistant.</p>\n"
                "</div>\n")
    html_path = os.path.join(out, "manual.html")
    index_file = os.path.join(out, ".index.html")

    def make_html(pages):
        with open(index_file, "w", encoding="utf-8") as f:
            f.write(index_html(groups, hits, pages))
        pandoc(["-f", "json", "-t", "html5", "--standalone", "--template", os.path.join(here, "manual.template.html"),
                "--toc", "--toc-depth=2", "-M", f"pagetitle={TITLE} {version}", "-M", f"title-meta={TITLE} {version}",
                "-M", "lang=en", "-M", "toc-title=Contents", "-M", f"author-meta={AUTHOR}",
                "--include-in-header", header, "--include-before-body", front, "--include-after-body", index_file,
                "-o", html_path],
               problems, "the manual", stdin=json.dumps({"pandoc-api-version": api, "meta": {}, "blocks": blocks}))

    # WeasyPrint warnings are problems (missing images, bad CSS, fonts).
    class Collect(logging.Handler):
        def emit(self, record):
            problems.add(f"WeasyPrint: {record.getMessage()}")

    logger = logging.getLogger("weasyprint")
    logger.addHandler(Collect(level=logging.WARNING))
    logger.setLevel(logging.WARNING)

    # Two passes: the first finds each section's page, the second lists
    # every page of an index entry once.
    make_html(None)
    if not os.path.isfile(html_path) or api is None:
        for p in problems.items:
            print(f"build_manual: {p}", file=sys.stderr)
        return 1
    first = weasyprint.HTML(filename=html_path).render()
    make_html(anchor_pages(first))
    document = weasyprint.HTML(filename=html_path).render()
    for p in (header, front, index_file):
        os.remove(p)
    expected = [(lvl, t) for ch in chapters for lvl, t, _ in ch.headings] + [(1, "Index")]
    expected += [(2, title) for title, _ in groups]
    images, _ = check_layout(document, problems, expected, [ch.cid for ch in chapters] + [INDEX_ID])
    want_images = sum(len(ch.images) for ch in chapters) + 1  # + the logo
    if images < want_images:
        problems.add(f"{want_images} images in the chapters, {images} in the layout")
    pdf = os.path.join(out, f"yantra-manual-{version}.pdf")
    document.write_pdf(pdf, pdf_variant="pdf/ua-1")
    fonts = check_pdf(pdf, problems, len(document.pages))

    if problems.items:
        for p in problems.items:
            print(f"build_manual: {p}", file=sys.stderr)
        print(f"build_manual: {len(problems.items)} problem(s)", file=sys.stderr)
        return 1
    entries = sum(len(e) for _, e in groups)
    print(f"build_manual: {pdf}: {len(document.pages)} pages, {len(chapters)} chapters, "
          f"{len(expected)} bookmarks, {images} images, {entries} index entries, "
          f"{len(fonts)} embedded fonts ({', '.join(fonts)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
