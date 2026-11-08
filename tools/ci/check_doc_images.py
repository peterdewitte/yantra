#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Image check of the user manual and the README (SCR-07, DOC-20).

usage: check_doc_images.py SOURCE_DIR

The manual (docs/manual/*.md) and README.md reference images from
docs/img/ only, as ![text](path) or [text](path) to a .png/.jpg/.gif/
.webp/.svg file; every referenced image must exist, and every image in
docs/img/ must be referenced by one of them (the screenshot set of
tools/screenshots, SCR-03). Exit 1 on a problem.
"""
import os
import re
import sys

REF = re.compile(r"\]\(([^)\s]+\.(?:png|jpe?g|gif|webp|svg))\)", re.IGNORECASE)
IMAGE_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg")


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    root = os.path.abspath(sys.argv[1])
    img = os.path.join(root, "docs", "img")
    manual = os.path.join(root, "docs", "manual")
    docs = [os.path.join(root, "README.md")]
    docs += sorted(os.path.join(manual, f) for f in os.listdir(manual) if f.endswith(".md"))
    problems, used = [], set()
    for md in docs:
        in_code = False
        for n, line in enumerate(open(md, encoding="utf-8"), 1):
            if line.startswith("```"):
                in_code = not in_code
            if in_code:
                continue
            for target in REF.findall(re.sub(r"`[^`]*`", "", line)):
                where = f"{os.path.relpath(md, root)}:{n}: {target}"
                if re.match(r"^[a-z]+:", target):
                    problems.append(f"{where}: images come from docs/img, not the web")
                    continue
                full = os.path.normpath(os.path.join(os.path.dirname(md), target))
                if os.path.dirname(full) != img:
                    problems.append(f"{where}: not in docs/img")
                elif not os.path.isfile(full):
                    problems.append(f"{where}: no such image")
                else:
                    used.add(os.path.basename(full))
    present = sorted(f for f in os.listdir(img) if f.lower().endswith(IMAGE_EXT)) if os.path.isdir(img) else []
    for f in present:
        if f not in used:
            problems.append(f"docs/img/{f}: referenced by neither the manual nor the README")
    for p in problems:
        print("doc_images:", p)
    print(f"doc_images: {len(used)} images referenced, {len(present)} in docs/img, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
