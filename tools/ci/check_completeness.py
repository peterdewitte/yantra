#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Completeness check of the radio plugin artefacts (RAD-132, RAD-133).

For every plugin (.so on Linux, .dll on Windows) given on the command line,
directly or as a folder that is scanned (not recursively):

  needed   Linux: the NEEDED entries (readelf -d) name only the allowed
           system libraries of RAD-132 (glibc and its parts, libudev).
           Windows: the DLL imports (objdump -p) name only Windows system
           DLLs and the UCRT. Everything else must be built in statically.
  exports  the plugin exports exactly one symbol, yantra_radio_entry
           (version script on Linux, .def file on Windows), so bundled
           libraries never clash between plugins or with Yantra.
  rpath    (Linux) no RPATH/RUNPATH: a plugin never looks for libraries
           next to itself or in the build tree.

The third RAD-133 check, the conformance kit in a clean environment, is
run by CI with the radio test host (RAD-91) once it exists.

An empty set of plugins passes (the skeleton has none yet).

Exit codes: 0 clean, 1 findings, 2 usage or tool error.
With --expect KIND[,KIND...] the check inverts for negative tests: exit 0
only if the findings are exactly of these kinds.
The Windows part is DEFERRED (Windows): it is written and its parsers are
tested with --self-test, but it has not run on a real .dll.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ENTRY = "yantra_radio_entry"

# RAD-132: only the operating system's base libraries stay outside.
# glibc ships libc, libm, libmvec, libdl, libpthread, librt and the dynamic
# loader; libudev is the one non-glibc system library allowed.
LINUX_ALLOWED = [
    r"libc\.so\.6",
    r"libm\.so\.6",
    r"libmvec\.so\.1",
    r"libdl\.so\.2",
    r"libpthread\.so\.0",
    r"librt\.so\.1",
    r"ld-linux-[a-z0-9_-]+\.so\.[0-9]+",
    r"libudev\.so\.1",
]

# Windows system DLLs and the UCRT (RAD-132); compared case-insensitively.
WINDOWS_ALLOWED = [
    r"kernel32\.dll", r"user32\.dll", r"advapi32\.dll", r"ws2_32\.dll",
    r"setupapi\.dll", r"cfgmgr32\.dll", r"winusb\.dll", r"ole32\.dll",
    r"shell32\.dll", r"bcrypt\.dll", r"ntdll\.dll", r"iphlpapi\.dll",
    r"ucrtbase\.dll", r"msvcrt\.dll",
    r"api-ms-win-crt-[a-z0-9-]+\.dll", r"api-ms-win-core-[a-z0-9-]+\.dll",
]


def _matches(name: str, patterns: list[str], extra: list[str], icase: bool) -> bool:
    flags = re.IGNORECASE if icase else 0
    return any(re.fullmatch(p, name, flags) for p in patterns + extra)


# ---- parsers (pure functions, tested by --self-test) ---------------------

def parse_readelf_dynamic(text: str) -> tuple[list[str], list[str]]:
    """NEEDED names and RPATH/RUNPATH values from `readelf -d -W`."""
    needed, rpath = [], []
    for line in text.splitlines():
        m = re.search(r"\((NEEDED|RPATH|RUNPATH)\)\s+.*\[(.*)\]", line)
        if m:
            (needed if m.group(1) == "NEEDED" else rpath).append(m.group(2))
    return needed, rpath


def parse_readelf_dynsyms(text: str) -> list[str]:
    """Defined, visible, non-local symbols from `readelf --dyn-syms -W`.

    Version-definition symbols (Ndx ABS, from the version script) are not
    exports and are skipped.
    """
    out = []
    for line in text.splitlines():
        f = line.split()
        # Num: Value Size Type Bind Vis Ndx Name
        if len(f) < 8 or not f[0].endswith(":") or not f[0][:-1].isdigit():
            continue
        bind, vis, ndx, name = f[4], f[5], f[6], f[7]
        if ndx in ("UND", "ABS") or bind == "LOCAL" or vis in ("HIDDEN", "INTERNAL"):
            continue
        out.append(name.split("@")[0])
    return sorted(set(out))


def parse_objdump_pe(text: str) -> tuple[list[str], list[str]]:
    """DLL imports and exported names from `objdump -p` of a PE file."""
    imports = re.findall(r"^\s*DLL Name:\s*(\S+)", text, re.MULTILINE)
    exports, in_table = [], False
    for line in text.splitlines():
        if re.match(r"^\[Ordinal/Name Pointer\] Table", line.strip()):
            in_table = True
            continue
        if in_table:
            m = re.match(r"^\s*\[\s*\d+\]\s+(\S+)", line)
            if m:
                exports.append(m.group(1))
            else:
                in_table = False
    return imports, sorted(set(exports))


# ---- checks --------------------------------------------------------------

def _run(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"check_completeness: cannot run {' '.join(cmd)}: {e}", file=sys.stderr)
        sys.exit(2)


def check_linux(path: str, extra: list[str], tools: dict) -> list[tuple[str, str]]:
    findings = []
    needed, rpath = parse_readelf_dynamic(_run([tools["readelf"], "-d", "-W", path]))
    for n in needed:
        if not _matches(n, LINUX_ALLOWED, extra, False):
            findings.append(("needed", f"NEEDED {n} is not an allowed system library (RAD-132)"))
    for r in rpath:
        findings.append(("rpath", f"RPATH/RUNPATH {r} set"))
    syms = parse_readelf_dynsyms(_run([tools["readelf"], "--dyn-syms", "-W", path]))
    if syms != [ENTRY]:
        shown = ", ".join(syms[:10]) + (" ..." if len(syms) > 10 else "")
        findings.append(("exports", f"exports {len(syms)} symbol(s) [{shown}], expected only {ENTRY}"))
    return findings


def check_windows(path: str, extra: list[str], tools: dict) -> list[tuple[str, str]]:
    findings = []
    imports, exports = parse_objdump_pe(_run([tools["objdump"], "-p", path]))
    for n in imports:
        if not _matches(n, WINDOWS_ALLOWED, extra, True):
            findings.append(("needed", f"imports {n}, not a Windows system DLL or the UCRT (RAD-132)"))
    if exports != [ENTRY]:
        findings.append(("exports", f"exports [{', '.join(exports[:10])}], expected only {ENTRY}"))
    return findings


def collect(paths: list[str], suffix: str) -> list[str]:
    out = []
    for p in paths:
        if os.path.isdir(p):
            out += sorted(os.path.join(p, f) for f in os.listdir(p)
                          if f.endswith(suffix) and os.path.isfile(os.path.join(p, f)))
        elif os.path.isfile(p):
            out.append(p)
        else:
            print(f"check_completeness: no such file or folder: {p}", file=sys.stderr)
            sys.exit(2)
    return out


def self_test() -> int:
    dyn = """
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/x]
"""
    assert parse_readelf_dynamic(dyn) == (["libc.so.6", "libstdc++.so.6"], ["/tmp/x"])
    syms = """
Symbol table '.dynsym' contains 4 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND malloc@GLIBC_2.2.5 (2)
     2: 0000000000000000     0 OBJECT  GLOBAL DEFAULT  ABS YANTRA_RADIO_1
     3: 0000000000001139    11 FUNC    GLOBAL DEFAULT   13 yantra_radio_entry@@YANTRA_RADIO_1
     4: 0000000000001150    11 FUNC    WEAK   DEFAULT   13 _ZNSt6vectorIiE9push_backEv
"""
    assert parse_readelf_dynsyms(syms) == ["_ZNSt6vectorIiE9push_backEv", "yantra_radio_entry"]
    pe = """
The Import Tables (interpreted .idata section contents)
 vma:            Hint    Time      Forward  DLL       First
        DLL Name: KERNEL32.dll
        DLL Name: api-ms-win-crt-runtime-l1-1-0.dll
        DLL Name: libstdc++-6.dll

The Export Tables (interpreted .edata section contents)
[Ordinal/Name Pointer] Table
        [   0] yantra_radio_entry

"""
    imports, exports = parse_objdump_pe(pe)
    assert imports == ["KERNEL32.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "libstdc++-6.dll"]
    assert exports == [ENTRY]
    assert _matches("KERNEL32.dll", WINDOWS_ALLOWED, [], True)
    assert not _matches("libstdc++-6.dll", WINDOWS_ALLOWED, [], True)
    assert _matches("ld-linux-x86-64.so.2", LINUX_ALLOWED, [], False)
    assert _matches("ld-linux-aarch64.so.1", LINUX_ALLOWED, [], False)
    assert not _matches("libSoapySDR.so.0.8", LINUX_ALLOWED, [], False)
    print("check_completeness: self-test OK")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*", help="plugin files or folders to scan")
    ap.add_argument("--platform", choices=["linux", "windows"],
                    default="windows" if os.name == "nt" else "linux")
    ap.add_argument("--allow", action="append", default=[], metavar="REGEX",
                    help="extra allowed library (e.g. sanitizer runtimes in sanitizer builds)")
    ap.add_argument("--expect", default="", metavar="KINDS",
                    help="negative test: pass only if the findings are exactly these kinds")
    ap.add_argument("--readelf", default=os.environ.get("READELF", "readelf"))
    ap.add_argument("--objdump", default=os.environ.get("OBJDUMP", "objdump"))
    ap.add_argument("--self-test", action="store_true", help="test the output parsers and exit")
    a = ap.parse_args()
    if a.self_test:
        return self_test()

    tools = {"readelf": a.readelf, "objdump": a.objdump}
    suffix = ".so" if a.platform == "linux" else ".dll"
    files = collect(a.paths, suffix)
    check = check_linux if a.platform == "linux" else check_windows
    kinds: set[str] = set()
    for f in files:
        found = check(f, a.allow, tools)
        for kind, msg in found:
            kinds.add(kind)
            print(f"{f}: {kind}: {msg}")
        if not found:
            print(f"{f}: OK")
    if not files:
        print(f"check_completeness: no {suffix} plugins found, nothing to check")

    if a.expect:
        want = set(a.expect.split(","))
        if kinds == want:
            print(f"check_completeness: expected findings {sorted(want)} seen")
            return 0
        print(f"check_completeness: expected findings {sorted(want)}, got {sorted(kinds)}")
        return 1
    return 1 if kinds else 0


if __name__ == "__main__":
    sys.exit(main())
