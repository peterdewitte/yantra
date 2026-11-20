#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The radio boundary lint (PLG-30, RAD-141; ctest boundary_lint).

One repository holds the application and the radio parts (PLG-29); this
lint keeps them apart. Every source folder belongs to an area:

  app             app/
  core            core/
  plugin-api      plugin-api/  (Yantra's plugin API, yantra/plugin.h)
  radio-api       radio-api/   (the radio API, yantra/radio.h)
  yantra-plugin   plugins/tetra, plugins/probe, plugins/beacon
  radio-plugin    plugins/hackrf, soapy, pei, simulator, example
  radio-common    radio-common/, gen/
  radio-tool      tools/yantra-radio-host, yantra-gen, yantra-hackrf,
                  yantra-pei-sim, yantra-sens, and tests/radio
  (neutral)       everything else: the top level, cmake/, tests/, the
                  other tools/, docs/, packaging/, third_party/

and may use only itself, the neutral folders and the areas of ALLOWED:
the application and core see the two APIs, never a plugin or
radio-common; the radio side sees the radio API and radio-common, never
app/, core/ or the plugin API. Two checks, both ways:

  includes  every #include of a C/C++ file of an area resolves (next to
            the file, else, for a name with a folder such as
            "yantra/core/x.h", by its path suffix among the repository's
            headers) to a header of an allowed area; a header that only
            a forbidden area has is a finding;
  links     with --targets <build>/boundary/targets.tsv (written at
            configure time by cmake/YantraBoundary.cmake: each target, its
            folder and its direct link dependencies), no target of an
            area reaches a target of a forbidden area, directly or
            through other targets.

Usage: lint_boundary.py [--targets FILE] [source-dir]
       lint_boundary.py --self-test FIXTURE-DIR
The self-test runs the lint on a small tree with deliberate violations
(tests/lint/boundary_fixture) and fails unless it reports exactly the
findings listed in the fixture's expected.txt.
"""
import os
import re
import sys

AREAS = (
    ("app/", "app"),
    ("core/", "core"),
    ("plugin-api/", "plugin-api"),
    ("radio-api/", "radio-api"),
    ("plugins/tetra/", "yantra-plugin"),
    ("plugins/probe/", "yantra-plugin"),
    ("plugins/beacon/", "yantra-plugin"),
    ("plugins/hackrf/", "radio-plugin"),
    ("plugins/soapy/", "radio-plugin"),
    ("plugins/pei/", "radio-plugin"),
    ("plugins/simulator/", "radio-plugin"),
    ("plugins/example/", "radio-plugin"),
    ("radio-common/", "radio-common"),
    ("gen/", "radio-common"),
    ("tools/yantra-radio-host/", "radio-tool"),
    ("tools/yantra-gen/", "radio-tool"),
    ("tools/yantra-hackrf/", "radio-tool"),
    ("tools/yantra-pei-sim/", "radio-tool"),
    ("tools/yantra-sens/", "radio-tool"),
    ("tests/radio/", "radio-tool"),
)
ALLOWED = {
    "app": {"core", "plugin-api", "radio-api"},
    "core": {"plugin-api", "radio-api"},
    "plugin-api": set(),
    "radio-api": set(),
    "yantra-plugin": {"plugin-api", "radio-api"},
    "radio-common": {"radio-api"},
    "radio-plugin": {"radio-api", "radio-common"},
    "radio-tool": {"radio-api", "radio-common", "radio-plugin"},
}
SOURCES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc")
SKIP = ("third_party/", "tests/lint/boundary_fixture/", ".git/", "sdk/")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)


def area_of(rel):
    for prefix, area in AREAS:
        if rel.startswith(prefix):
            return area
    return None


def skipped(rel):
    first = rel.split("/", 1)[0]
    return (rel.startswith(SKIP) or first.startswith("build") or "/target/" in rel
            or os.path.basename(rel) == "CMakeFiles")


def source_files(root):
    for base, dirs, names in os.walk(root):
        rel_base = os.path.relpath(base, root).replace(os.sep, "/")
        rel_base = "" if rel_base == "." else rel_base + "/"
        dirs[:] = sorted(d for d in dirs if not skipped(rel_base + d + "/") and not d.startswith("."))
        for name in sorted(names):
            if name.endswith(SOURCES):
                yield rel_base + name


def allowed(src_area, dst_area):
    return dst_area is None or dst_area == src_area or dst_area in ALLOWED[src_area]


def check_includes(root, files):
    # Every header by each of its path suffixes ("c.h", "b/c.h", "a/b/c.h").
    index = {}
    for rel in files:
        parts = rel.split("/")
        for i in range(len(parts)):
            index.setdefault("/".join(parts[i:]), []).append(rel)
    findings = []
    for rel in files:
        src_area = area_of(rel)
        if src_area is None:
            continue
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
            text = f.read()
        for m in INCLUDE.finditer(text):
            name = m.group(2).strip()
            here = os.path.normpath(os.path.join(os.path.dirname(rel), name)).replace(os.sep, "/")
            if m.group(1) == '"' and os.path.isfile(os.path.join(root, here)):
                candidates = [here]
            elif "/" not in name:
                continue  # a bare name not next to the file: a system or include-path header
            else:
                candidates = index.get(os.path.normpath(name).replace(os.sep, "/").lstrip("./"), [])
            if not candidates or any(allowed(src_area, area_of(c)) for c in candidates):
                continue
            line = text.count("\n", 0, m.start()) + 1
            where = sorted({area_of(c) for c in candidates})
            findings.append("include: %s:%d (%s) includes %s of %s" % (rel, line, src_area, name, ", ".join(where)))
    return findings


def read_targets(path):
    targets = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            name, folder = fields[0], fields[1]
            deps = [d for d in (fields[2].split(";") if len(fields) > 2 else []) if d]
            targets[name] = (folder, deps)
    return targets


def check_links(targets):
    findings = []
    areas = {n: area_of(folder + "/") if folder else None for n, (folder, _) in targets.items()}
    for name in sorted(targets):
        src_area = areas[name]
        if src_area is None:
            continue
        # Breadth first, so the reported path is a shortest one.
        seen, queue, reported = {name: None}, [name], set()
        while queue:
            cur = queue.pop(0)
            for dep in targets[cur][1]:
                if dep in seen or dep not in targets:
                    continue
                seen[dep] = cur
                dst_area = areas[dep]
                if not allowed(src_area, dst_area):
                    if dep not in reported:
                        path, p = [dep], cur
                        while p is not None:
                            path.append(p)
                            p = seen[p]
                        findings.append("link: %s (%s) uses %s of %s (%s)" % (
                            name, src_area, dep, dst_area, " -> ".join(reversed(path))))
                        reported.add(dep)
                    continue
                queue.append(dep)
    return findings


def run(root, targets_file):
    files = list(source_files(root))
    findings = check_includes(root, files)
    nlinks = 0
    if targets_file:
        targets = read_targets(targets_file)
        nlinks = len(targets)
        findings += check_links(targets)
    return findings, len(files), nlinks


def self_test(fixture):
    findings, _, _ = run(os.path.join(fixture, "tree"), os.path.join(fixture, "targets.tsv"))
    with open(os.path.join(fixture, "expected.txt"), encoding="utf-8") as f:
        expected = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    ok = sorted(findings) == sorted(expected)
    for line in sorted(set(expected) - set(findings)):
        print("lint_boundary self-test: NOT FOUND: " + line)
    for line in sorted(set(findings) - set(expected)):
        print("lint_boundary self-test: UNEXPECTED: " + line)
    print("lint_boundary self-test: %d findings, %d expected: %s" % (len(findings), len(expected), "ok" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ["--self-test"] and len(argv) == 2:
        return self_test(argv[1])
    targets_file = None
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--targets" and i + 1 < len(argv):
            targets_file = argv[i + 1]
            i += 2
            continue
        if argv[i].startswith("-"):
            print(__doc__, file=sys.stderr)
            return 2
        args.append(argv[i])
        i += 1
    root = args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    findings, nfiles, ntargets = run(root, targets_file)
    for line in findings:
        print("lint_boundary: " + line)
    print("lint_boundary: %d source files, %d targets, %d findings" % (nfiles, ntargets, len(findings)))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
