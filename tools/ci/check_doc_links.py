#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Link check of the Markdown documentation (DOC-17, TST-02 docs job;
copied from yantra-radios fb194f5 tools/ci/check_doc_links.py).

usage: check_doc_links.py SOURCE_DIR

Every relative link [text](path) and [text](path#anchor) in the project's
own *.md files (not third_party/ or build trees) must name an existing
file or folder, and an anchor must be a heading of the target (GitHub's
heading slugs). Web links (http, https, mailto) are not fetched: the
check runs offline. Exit 1 on a broken link.
"""
import os
import re
import sys

LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)\)")
SKIP_DIRS = {"third_party", ".git", "target"}


def slug(heading):
    s = heading.strip().lower()
    s = re.sub(r"[`*]", "", s)
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors(path, cache={}):
    if path not in cache:
        found = set()
        in_code = False
        for line in open(path, encoding="utf-8"):
            if line.startswith("```"):
                in_code = not in_code
            m = re.match(r"^#{1,6} (.*)$", line)
            if m and not in_code:
                base, n = slug(m.group(1)), 0
                while (base if n == 0 else f"{base}-{n}") in found:
                    n += 1
                found.add(base if n == 0 else f"{base}-{n}")
        cache[path] = found
    return cache[path]


def markdown_files(root):
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith("build")]
        for f in files:
            if f.endswith(".md"):
                yield os.path.join(d, f)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = os.path.abspath(sys.argv[1])
    problems, links = [], 0
    for md in sorted(markdown_files(root)):
        in_code = False
        for n, line in enumerate(open(md, encoding="utf-8"), 1):
            if line.startswith("```"):
                in_code = not in_code
            if in_code:
                continue
            for target in LINK.findall(re.sub(r"`[^`]*`", "", line)):
                if re.match(r"^[a-z]+:", target):
                    continue
                links += 1
                path, _, anchor = target.partition("#")
                full = os.path.normpath(os.path.join(os.path.dirname(md), path)) if path else md
                where = f"{os.path.relpath(md, root)}:{n}: {target}"
                if not os.path.exists(full):
                    problems.append(f"{where}: no such file")
                elif anchor and full.endswith(".md") and anchor not in anchors(full):
                    problems.append(f"{where}: no heading #{anchor}")
    for p in problems:
        print("doc_links:", p)
    print(f"doc_links: {links} relative links, {len(problems)} broken")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
