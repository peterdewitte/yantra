#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that code quoted in the documentation is the real, compiled code
(RAD-97 tutorial rule;
copied from yantra 68ea20a ci/check_doc_snippets.py).

A Markdown file marks a quoted block with an HTML comment right before
its fenced code block:

    <!-- snippet: plugins/example/beacon_plugin.cpp#stream_main -->
    ```c
    ...
    ```

The block must equal, line by line, the lines between `[snippet:process]`
and `[/snippet]` in that source file (the marker lines themselves are not
part of it). Every snippet named in a source file must be quoted somewhere,
so a tutorial cannot silently lose a step.

Usage: check_doc_snippets.py <source root> <markdown file>...
"""
import re
import sys
from pathlib import Path

MARK = re.compile(r"<!--\s*snippet:\s*([^#\s]+)#([a-z0-9_]+)\s*-->")
OPEN = re.compile(r"\[snippet:([a-z0-9_]+)\]")
CLOSE = re.compile(r"\[/snippet\]")


def source_snippets(path):
    snippets = {}
    current = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        opened = OPEN.search(line)
        if opened:
            if current is not None:
                raise ValueError(f"{path}:{number}: snippet inside snippet {current}")
            current = opened.group(1)
            if current in snippets:
                raise ValueError(f"{path}:{number}: duplicate snippet {current}")
            snippets[current] = []
        elif CLOSE.search(line):
            if current is None:
                raise ValueError(f"{path}:{number}: [/snippet] without [snippet:...]")
            current = None
        elif current is not None:
            snippets[current].append(line)
    if current is not None:
        raise ValueError(f"{path}: snippet {current} is not closed")
    return snippets


def doc_blocks(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        mark = MARK.search(lines[i])
        if not mark:
            i += 1
            continue
        start = i + 1
        if start >= len(lines) or not lines[start].startswith("```"):
            raise ValueError(f"{path}:{i + 1}: snippet marker not followed by a code block")
        end = start + 1
        while end < len(lines) and not lines[end].startswith("```"):
            end += 1
        if end >= len(lines):
            raise ValueError(f"{path}:{start + 1}: code block is not closed")
        yield i + 1, mark.group(1), mark.group(2), lines[start + 1:end]
        i = end + 1


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    root = Path(argv[1])
    errors = []
    sources = {}
    quoted = set()
    for doc in argv[2:]:
        doc_path = Path(doc)
        try:
            blocks = list(doc_blocks(doc_path))
        except ValueError as e:
            errors.append(str(e))
            continue
        for line, rel, name, block in blocks:
            src = root / rel
            if rel not in sources:
                if not src.is_file():
                    errors.append(f"{doc_path}:{line}: {rel} does not exist")
                    sources[rel] = None
                    continue
                try:
                    sources[rel] = source_snippets(src)
                except ValueError as e:
                    errors.append(str(e))
                    sources[rel] = None
                    continue
            if sources[rel] is None:
                continue
            quoted.add((rel, name))
            expected = sources[rel].get(name)
            if expected is None:
                errors.append(f"{doc_path}:{line}: {rel} has no snippet '{name}'")
            elif expected != block:
                for n, (a, b) in enumerate(zip(expected, block)):
                    if a != b:
                        break
                else:
                    n = min(len(expected), len(block))
                errors.append(f"{doc_path}:{line}: snippet {rel}#{name} differs from the "
                              f"source at its line {n + 1}")
    for rel, snippets in sources.items():
        for name in sorted(snippets or {}):
            if (rel, name) not in quoted:
                errors.append(f"{rel}: snippet '{name}' is not quoted in the documentation")
    for e in errors:
        print("doc snippets: " + e)
    if not errors:
        print(f"doc snippets: OK ({len(quoted)} quoted blocks)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
