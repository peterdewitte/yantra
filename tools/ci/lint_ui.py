#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The source half of the UI lint (DOC-20, UI-12; the run-time half is
# tests/app/ui/test_ui_lint.cpp). It finds every text the app passes to
# tr(), trf() and tr_mark() (and the report composer's c.tr()/c.trf() in
# core/src/report, which the app runs through tr(); step 10.4, and its c.fill()
# templates, which go through the same tr()) and checks the translation tables
# data/i18n/{nl,fr,de}.json against them:
#   - every text has a translation in every language, and no table keeps
#     a text the app no longer has;
#   - a translation keeps the printf conversions (in order) and the "{}"
#     places of its text, has no "###" (the id part never goes through a
#     table), is not empty and has no placeholder marks (TODO, FIXME, ...);
#   - only Latin-1 characters (the bundled UI font) and no "@" (the tables
#     are embedded through CMake's file(CONFIGURE @ONLY));
#   - every "manual N.M" a text refers to is a section of the manual
#     (app/model/src/manual.cpp and a "## N.M" heading in docs/manual), and
#     its translation refers to the same sections;
#   - no text, translation, plugin text (every plugin in plugins/:
#     descriptors, schemas, settings, sensors, help and error texts,
#     translation tables), text of the radio parts (radio-common/, the
#     radio API's validation messages, the --help and output of the radio
#     tools and yantra-demo), demo project description (data/demo) or
#     committed sample report (docs/samples) shows a requirement ID such as
#     "IND-43" or "REC-01..03": customers see these texts, and the IDs
#     belong in code comments, test names and commit messages only. The
#     two test kits' findings (yantra-radio-host conformance,
#     yantra-plugin-test) name the API rule they check, for plugin
#     developers, and are not checked.
# Usage: lint_ui.py [source-dir]          check (exit 1 on a finding)
#        lint_ui.py --extract [source-dir] print the texts as a JSON list
import json
import os
import re
import sys

LANGUAGES = ("nl", "fr", "de")
SOURCE_DIRS = ("app/gui", "app/model/src", "app/model/include/yantra/app", "core/src/report")
CALL = re.compile(r'\b(tr|trf|tr_mark|fill)\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)(?=[),])')
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
PRINTF = re.compile(r"%(?:%|[-+ #0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfFgGaAcspn])")
MANUAL_REF = re.compile(r"\bmanual (\d+(?:\.\d+)?)")
MARKS = re.compile(r"TODO|FIXME|XXX|TRANSLATE|\?\?\?|<<|>>|\[(?:nl|fr|de|en)\]", re.IGNORECASE)
# The requirement ID prefixes of the specs (Yantra and radios).
REQUIREMENT_ID = re.compile(r"\b(?:API|BRD|DB|DEC|DEMO|DOC|EXP|FUN|GEO|HW|IND|NFR|OA|PLG|PP|RAD|REC|RF|SCR|TRM|TST|UI)-[0-9]{1,3}\b"
                            r"|\b(?:API|BRD|DB|DEC|DEMO|DOC|EXP|FUN|GEO|HW|IND|NFR|OA|PLG|PP|RAD|REC|RF|SCR|TRM|TST|UI)-[0-9]{1,3}[a-z.]")
# Every plugin (Yantra's and the radio plugins, PLG-29) and the radio
# parts' code and tools whose texts users see (PLG-30, RAD-141).
PLUGIN_DIRS = ("plugins", "radio-common", "radio-api", "gen", "tools/yantra-gen", "tools/yantra-hackrf",
               "tools/yantra-pei-sim", "tools/yantra-sens", "tools/yantra-radio-host", "tools/yantra-demo")
# Developer reports that cite the rule each check enforces (see above).
PLUGIN_TEXT_EXEMPT = ("tools/yantra-radio-host/conformance.cpp",)
DEMO_TEXT_KEYS = ("name", "site", "operator_name", "notes", "label", "title", "shows", "address", "surveyor")
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'", "?": "?", "0": "\0"}


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "x":
                m = re.match(r"[0-9a-fA-F]+", s[i + 2:])
                out.append(chr(0xDC00 + int(m.group(0), 16)))  # a byte of UTF-8
                i += 2 + len(m.group(0))
                continue
            if n == "u":
                out.append(chr(int(s[i + 2:i + 6], 16)))
                i += 6
                continue
            out.append(ESCAPES.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    # The \x escapes are bytes of UTF-8 sequences ("m\xc2\xb2" is "m" and a superscript two).
    return "".join(out).encode("utf-8", "surrogateescape").decode("utf-8", "surrogateescape")


def strip_comments(src):
    # Line and block comments go; strings stay (a "//" in a string is kept).
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i)
            i = n if j < 0 else j + 2
        elif src[i] == '"':
            m = LITERAL.match(src, i)
            if m is None:
                out.append(src[i])
                i += 1
            else:
                out.append(m.group(0))
                i = m.end()
        elif src[i] == "'":
            m = re.match(r"'(?:[^'\\]|\\.)*'", src[i:])
            k = len(m.group(0)) if m else 1
            out.append(src[i:i + k])
            i += k
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def texts(root):
    found = {}
    for d in SOURCE_DIRS:
        base = os.path.join(root, d)
        for name in sorted(os.listdir(base)):
            if not name.endswith((".cpp", ".h")):
                continue
            path = os.path.join(base, name)
            with open(path, encoding="utf-8") as f:
                src = strip_comments(f.read())
            for m in CALL.finditer(src):
                text = unescape("".join(LITERAL.findall(m.group(2))))
                text = text.split("###")[0]
                if text:
                    found.setdefault(text, "%s/%s" % (d, name))
    return found


def plugin_texts(root):
    # Every string literal of the plugins' and radio parts' sources (their
    # windows, tooltips, field help, settings, sensors, messages and
    # translation tables), test data and build output aside.
    found = []
    for d in PLUGIN_DIRS:
        for base, dirs, names in os.walk(os.path.join(root, d)):
            dirs[:] = sorted(x for x in dirs if x not in ("testdata", "tests", "target", "fixtures"))
            for name in sorted(names):
                if not name.endswith((".c", ".cpp", ".h", ".hpp", ".inc")):
                    continue
                path = os.path.join(base, name)
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                if rel in PLUGIN_TEXT_EXEMPT:
                    continue
                with open(path, encoding="utf-8") as f:
                    src = strip_comments(f.read())
                for m in LITERAL.finditer(src):
                    try:
                        text = unescape(m.group(1))
                    except (UnicodeError, ValueError):
                        text = m.group(1)  # bytes that are not UTF-8: check the source text
                    found.append((rel, text))
    return found


def demo_texts(root):
    # The texts of the bundled demo projects and scenarios a user sees
    # (project notes, names, titles), not the files' own descriptions.
    found = []
    folder = os.path.join(root, "data/demo")
    for base, dirs, names in os.walk(folder):
        dirs.sort()
        for name in sorted(names):
            if not name.endswith(".json"):
                continue
            path = os.path.join(base, name)
            with open(path, encoding="utf-8") as f:
                try:
                    data = json.load(f)
                except ValueError:
                    continue

            def walk(v):
                if isinstance(v, dict):
                    for k, x in v.items():
                        if k in DEMO_TEXT_KEYS and isinstance(x, str):
                            found.append((os.path.relpath(path, root), x))
                        else:
                            walk(x)
                elif isinstance(v, list):
                    for x in v:
                        walk(x)
            walk(data)
    return found


def sample_report_texts(root):
    found = []
    folder = os.path.join(root, "docs/samples")
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if name.endswith(".html"):
            with open(os.path.join(folder, name), encoding="utf-8") as f:
                found.append(("docs/samples/" + name, re.sub(r"<[^>]*>", " ", f.read())))
    return found


def requirement_ids(text):
    return sorted(set(m.group(0).rstrip(".") for m in REQUIREMENT_ID.finditer(text)))


def manual_sections(root):
    with open(os.path.join(root, "app/model/src/manual.cpp"), encoding="utf-8") as f:
        code = set(re.findall(r'\{"(\d+(?:\.\d+)?)",', f.read()))
    headings = set()
    folder = os.path.join(root, "docs/manual")
    for name in os.listdir(folder):
        if name.endswith(".md"):
            with open(os.path.join(folder, name), encoding="utf-8") as f:
                headings |= set(re.findall(r"^#{1,2} (\d+(?:\.\d+)?) ", f.read(), re.MULTILINE))
    return code, headings


def check(root):
    problems = []
    found = texts(root)
    code, headings = manual_sections(root)
    for n in sorted(code ^ headings):
        problems.append("manual section %s is in %s only" % (n, "manual.cpp" if n in code else "docs/manual"))
    for text, where in sorted(found.items()):
        for ref in MANUAL_REF.findall(text):
            if ref not in code or ref not in headings:
                problems.append("%s: refers to manual %s, which does not exist: %r" % (where, ref, text))
        if "@" in text:
            problems.append("%s: '@' in a text: %r" % (where, text))
        if requirement_ids(text):
            problems.append("%s: requirement ID %s in a UI text: %r" % (where, ", ".join(requirement_ids(text)), text))
    for where, text in plugin_texts(root) + demo_texts(root) + sample_report_texts(root):
        for rid in requirement_ids(text):
            problems.append("%s: requirement ID %s in a text users see" % (where, rid))
    for lang in LANGUAGES:
        path = os.path.join(root, "data/i18n/%s.json" % lang)
        with open(path, encoding="utf-8") as f:
            table = json.load(f)
        if table.get("format") != "yantra-ui-strings/1" or table.get("language") != lang:
            problems.append("%s: format or language wrong" % path)
        strings = table.get("strings", {})
        for text in sorted(found):
            if text not in strings:
                problems.append("%s: no translation of %r (%s)" % (lang, text, found[text]))
        for text, value in sorted(strings.items()):
            if text not in found:
                problems.append("%s: stale entry (not in the app): %r" % (lang, text))
                continue
            if not isinstance(value, str) or not value.strip():
                problems.append("%s: empty translation of %r" % (lang, text))
                continue
            if PRINTF.findall(value) != PRINTF.findall(text):
                problems.append("%s: printf conversions differ: %r -> %r" % (lang, text, value))
            if value.count("{}") != text.count("{}"):
                problems.append("%s: '{}' places differ: %r -> %r" % (lang, text, value))
            if "###" in value:
                problems.append("%s: '###' in the translation of %r" % (lang, text))
            if MARKS.search(value) and not MARKS.search(text):
                problems.append("%s: placeholder mark in %r" % (lang, value))
            if "@" in value:
                problems.append("%s: '@' in %r" % (lang, value))
            if requirement_ids(value):
                problems.append("%s: requirement ID %s in %r" % (lang, ", ".join(requirement_ids(value)), value))
            if any(ord(c) > 0xFF for c in value):
                problems.append("%s: not Latin-1 (the UI font): %r" % (lang, value))
            if any(r not in value for r in MANUAL_REF.findall(text)):
                problems.append("%s: manual sections differ: %r -> %r" % (lang, text, value))
            if text.startswith(" ") != value.startswith(" ") or text.endswith(" ") != value.endswith(" "):
                problems.append("%s: leading/trailing space differs: %r -> %r" % (lang, text, value))
    return problems, len(found)


def main(argv):
    extract = "--extract" in argv
    args = [a for a in argv if not a.startswith("--")]
    root = args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    if extract:
        json.dump(sorted(texts(root)), sys.stdout, ensure_ascii=False, indent=1)
        sys.stdout.write("\n")
        return 0
    problems, n = check(root)
    for p in problems:
        print("lint_ui: " + p)
    print("lint_ui: %d texts, %d languages, %d findings" % (n, len(LANGUAGES), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
