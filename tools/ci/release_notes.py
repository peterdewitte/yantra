#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print the CHANGELOG.md section of a version as release notes (RAD-134).

Usage: release_notes.py CHANGELOG.md v1.2.0
A section starts with a level-2 heading that names the version, with or
without the leading "v" (e.g. "## [1.2.0] - 2026-11-03" or "## v1.2.0"),
and ends at the next level-2 heading. Without one, a level-3 heading
counts (the radio parts' releases before the merge, "### [0.4.0]" in the
section "Radio parts before the merge", PLG-29), ending at the next
heading of level 3 or less. Exit code 1 if there is none.
"""
import re
import sys


def notes(text: str, tag: str, level: int = 2) -> str | None:
    version = tag[1:] if tag.startswith("v") else tag
    head = re.compile(r"^#{%d}\s+\[?v?" % level + re.escape(version) + r"\]?(\s|$)")
    end = re.compile(r"^#{1,%d} " % level)
    out, inside = [], False
    for line in text.splitlines():
        if end.match(line):
            if inside:
                break
            inside = bool(head.match(line))
            continue
        if inside:
            out.append(line)
    if inside:
        return "\n".join(out).strip() + "\n"
    return notes(text, tag, 3) if level == 2 else None


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
