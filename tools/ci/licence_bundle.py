#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The licence bundle of the plugins (RAD-137, RAD-132, RAD-135).

usage: licence_bundle.py check SOURCE_DIR
       licence_bundle.py bundle SOURCE_DIR DEST_DIR

The bundled libraries are the rows of tools/deps/manifest.tsv (name,
version, SPDX licence, folder, release archive, SHA-256), plus the Rust
standard library that the protocol crate links statically.

check: every row's folder exists; every folder under third_party/ is a
row (doctest excepted: tests only, never shipped); every SPDX id of a
row's licence expression has a text, the component's own licence file or
LICENSES/<id>.txt; THIRD_PARTY.md has a row with the name, the version
and the SPDX expression; the archive rows have a SHA-256. Exit 1 on a
problem.

bundle: writes DEST_DIR (installed to share/licenses/yantra-radios and
packed into every release, next to the plugins): LICENSE, LICENSES/,
THIRD_PARTY.md, one folder per bundled library with its licence texts
(and, where the licence is only in the source headers, the copyright
lines collected from them in COPYRIGHT.txt), and BUNDLE.md, the index:
component, version, SPDX id, licence files and where the source is.
"""
import os
import pathlib
import re
import shutil
import sys

# doctest: tests only; the others: the application's own libraries
# (THIRD_PARTY.md, "Libraries"), never in a radio plugin (PLG-29).
SKIP = {"doctest", "civetweb", "imgui", "miniaudio", "stb"}
TEXT_GLOBS = ("COPYING*", "LICEN[CS]E*", "NOTICE*")
SOURCE_URL = "https://github.com/peterdewitte/yantra-radio-plugins"
RUST_STD = {"name": "Rust standard library", "version": "toolchain of the build", "licence": "MIT OR Apache-2.0",
            "folder": "", "archive": "-", "sha256": "-"}


def rows(root):
    out = []
    for line in (root / "tools/deps/manifest.tsv").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        name, version, licence, folder, archive, sha256 = line.split("\t")
        out.append({"name": name, "version": version, "licence": licence, "folder": folder,
                    "archive": archive, "sha256": sha256})
    return out


def spdx_ids(expr):
    return [t for t in re.split(r"[\s()]+", expr) if t and t not in ("AND", "OR", "WITH")]


def own_texts(root, folder):
    d = root / folder
    found = []
    for g in TEXT_GLOBS:
        found += [p for p in d.glob(g) if p.is_file()]
    return sorted(set(found))


def copyright_lines(root, folder):
    """Copyright lines of the source headers (for licences given only there)."""
    lines = []
    for p in sorted((root / folder).rglob("*")):
        if p.suffix not in (".c", ".h", ".cpp", ".hpp") or not p.is_file():
            continue
        head = p.read_text(encoding="utf-8", errors="replace")[:4000]
        for line in head.splitlines():
            m = re.search(r"(Copyright\s.*)$", line)
            if m:
                text = m.group(1).strip().rstrip("*/").strip()
                if text not in lines:
                    lines.append(text)
    return lines


def check(root):
    problems = []
    third = (root / "THIRD_PARTY.md").read_text(encoding="utf-8")
    table = [line for line in third.splitlines() if line.startswith("|")]
    folders = set()
    for r in rows(root):
        where = f"manifest row {r['name']}"
        folder = root / r["folder"]
        folders.add(r["folder"])
        if not folder.is_dir():
            problems.append(f"{where}: no folder {r['folder']}")
            continue
        texts = own_texts(root, r["folder"])
        for i in spdx_ids(r["licence"]):
            if not texts and not (root / "LICENSES" / f"{i}.txt").exists():
                problems.append(f"{where}: no licence text for {i} (own file or LICENSES/{i}.txt)")
        if r["archive"] != "-" and not re.fullmatch(r"[0-9a-f]{64}", r["sha256"]):
            problems.append(f"{where}: archive without a SHA-256")
        key = re.sub(r"[^a-z0-9]", "", r["name"].lower())  # nlohmann-json = nlohmann/json
        hit = [line for line in table if key in re.sub(r"[^a-z0-9]", "", line.lower()) and r["version"] in line]
        if not hit:
            problems.append(f"THIRD_PARTY.md: no row with {r['name']} {r['version']}")
        elif not any(r["licence"] in line for line in hit):
            problems.append(f"THIRD_PARTY.md: the row of {r['name']} does not say {r['licence']}")
    for d in sorted((root / "third_party").iterdir()):
        rel = f"third_party/{d.name}"
        if d.is_dir() and d.name not in SKIP and rel not in folders:
            problems.append(f"{rel}: not in tools/deps/manifest.tsv")
    if "Rust standard library" not in third:
        problems.append("THIRD_PARTY.md: no row for the Rust standard library")
    for i in spdx_ids(RUST_STD["licence"])[:1]:
        if not (root / "LICENSES" / f"{i}.txt").exists():
            problems.append(f"Rust standard library: no LICENSES/{i}.txt")
    return problems


def bundle(root, dest):
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    shutil.copy2(root / "LICENSE", dest / "LICENSE")
    shutil.copy2(root / "THIRD_PARTY.md", dest / "THIRD_PARTY.md")
    shutil.copytree(root / "LICENSES", dest / "LICENSES")
    index = ["# Licence bundle of yantra-radios", "",
             "yantra-radios is GPL-3.0-or-later (`LICENSE`); its TETRA plugins contain osmo-tetra "
             "(AGPL-3.0-or-later). These libraries are built into the plugins (RAD-132); their "
             "licence texts are in the folders below. The complete corresponding source of every "
             f"release is published at {SOURCE_URL} (release asset "
             "`yantra-radios-<version>-source.tar.gz`, RAD-137). Details: `THIRD_PARTY.md`.", "",
             "| Component | Version | Licence (SPDX) | Licence files | Source |", "|---|---|---|---|---|"]
    for r in rows(root) + [RUST_STD]:
        comp = pathlib.PurePath(r["folder"]).name if r["folder"] else "rust-std"
        out = dest / comp
        out.mkdir(exist_ok=True)
        files = []
        texts = own_texts(root, r["folder"]) if r["folder"] else []
        for t in texts:
            shutil.copy2(t, out / t.name)
            files.append(f"{comp}/{t.name}")
        if not texts:
            ids = spdx_ids(r["licence"])
            if r is RUST_STD:
                ids = ids[:1]  # used under MIT (dual MIT OR Apache-2.0)
            for i in ids:
                shutil.copy2(root / "LICENSES" / f"{i}.txt", out / f"{i}.txt")
                files.append(f"{comp}/{i}.txt")
            lines = (copyright_lines(root, r["folder"]) if r["folder"]
                     else ["Copyright (c) The Rust Project Contributors"])
            if lines:
                (out / "COPYRIGHT.txt").write_text(
                    f"{r['name']} {r['version']}: copyright notices of its source files "
                    f"(licence {r['licence']}, text next to this file)\n\n" + "\n".join(lines) + "\n",
                    encoding="utf-8")
                files.append(f"{comp}/COPYRIGHT.txt")
        if r["archive"] != "-":
            source = f"{r['archive']} (SHA-256 {r['sha256']}); vendored subset in {r['folder']}"
        elif r["folder"]:
            source = f"pinned in {r['folder']} (THIRD_PARTY.md)"
        else:
            source = "the Rust toolchain that built the release"
        index.append(f"| {r['name']} | {r['version']} | {r['licence']} | {', '.join(files)} | {source} |")
    (dest / "BUNDLE.md").write_text("\n".join(index) + "\n", encoding="utf-8")


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "check":
        problems = check(pathlib.Path(a[1]).resolve())
        for p in problems:
            print("licence_bundle:", p)
        print(f"licence_bundle: {len(problems)} problems")
        return 1 if problems else 0
    if len(a) == 3 and a[0] == "bundle":
        root = pathlib.Path(a[1]).resolve()
        problems = check(root)
        if problems:
            for p in problems:
                print("licence_bundle:", p, file=sys.stderr)
            return 1
        bundle(root, pathlib.Path(a[2]).resolve())
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
