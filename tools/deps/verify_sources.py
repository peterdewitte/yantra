#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks the vendored sources of the bundled libraries (RAD-136): for each
# row of tools/deps/manifest.tsv with a release archive, downloads the
# archive (or takes it from the cache folder), checks its SHA-256 and
# compares every vendored file under the row's folder with the file of the
# same path in the archive (README.md, the project's own note, excepted).
# Exit code 0 when all match, 1 on a mismatch, 2 on a download or usage
# error. Needs network access unless the archives are in the cache.
# Usage: tools/deps/verify_sources.py [cache-dir]   (default build/deps-cache)
import hashlib
import pathlib
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]


def rows():
    for line in (ROOT / "tools/deps/manifest.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, version, _licence, folder, archive, sha256 = line.split("\t")
        if archive != "-":
            yield name, version, folder, archive, sha256


def fetch(url, sha256, cache):
    path = cache / url.rsplit("/", 1)[1]
    if not path.exists():
        print(f"download {url}")
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        tmp = path.with_suffix(path.suffix + ".part")
        tmp.write_bytes(data)
        tmp.replace(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != sha256:
        raise ValueError(f"{path.name}: SHA-256 {digest}, manifest says {sha256}")
    return path


def main():
    cache = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/deps-cache"
    cache.mkdir(parents=True, exist_ok=True)
    problems = 0
    for name, version, folder, url, sha256 in rows():
        try:
            archive = fetch(url, sha256, cache)
        except (OSError, ValueError) as e:
            print(f"{name}: {e}")
            return 2
        with tarfile.open(archive) as tar:
            members = {}
            for m in tar.getmembers():
                if m.isfile():
                    members[m.name.split("/", 1)[1] if "/" in m.name else m.name] = m
            vendored = sorted(p for p in (ROOT / folder).rglob("*")
                              if p.is_file() and p.name != "README.md")
            for p in vendored:
                rel = p.relative_to(ROOT / folder).as_posix()
                m = members.get(rel)
                if m is None:
                    print(f"{name}: {folder}/{rel} is not in {archive.name}")
                    problems += 1
                elif tar.extractfile(m).read() != p.read_bytes():
                    print(f"{name}: {folder}/{rel} differs from {archive.name}")
                    problems += 1
        print(f"{name} {version}: {len(vendored)} files checked against {archive.name}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
