#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print the CHANGELOG.md section of a version as release notes (RAD-134).

Usage: release_notes.py CHANGELOG.md v1.2.0
A section starts with a level-2 heading that names the version, with or
without the leading "v" (e.g. "## [1.2.0] - 2026-11-03" or "## v1.2.0"),
and ends at the next level-2 heading. Exit code 1 if there is none.
"""
import re
import sys


def notes(text: str, tag: str) -> str | None:
    version = tag[1:] if tag.startswith("v") else tag
    head = re.compile(r"^##\s+\[?v?" + re.escape(version) + r"\]?(\s|$)")
    out, inside = [], False
    for line in text.splitlines():
        if line.startswith("## "):
            if inside:
                break
            inside = bool(head.match(line))
            continue
        if inside:
            out.append(line)
    return "\n".join(out).strip() + "\n" if inside else None


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as f:
        result = notes(f.read(), sys.argv[2])
    if result is None:
        print(f"release_notes: no section for {sys.argv[2]} in {sys.argv[1]}", file=sys.stderr)
        return 1
    sys.stdout.write(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
