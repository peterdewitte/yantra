#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows packages of Yantra with Yantra's own script (NFR-09, OA-02).

Made on Linux from the cross-compiled Windows tree (preset windows), by
`ninja -C build-win package-windows` (which passes the arguments):

  OUT/yantra-<version>-windows-x86_64.zip        the portable zip
  OUT/yantra-<version>-windows-x86_64-setup.exe  the NSIS setup (makensis)
  OUT/SHA256SUMS

Builds no code. Steps: `cmake --install` of the tree into a staging
folder (the programs, Yantra's plugins, yantra-radios' self-contained
radio plugins and tools with their licence bundle (PLG-26, RAD-137),
PDFium and the MSYS2 UCRT64 DLLs the programs import (copy_dlls.py),
data, fonts with their OFL texts, logo, Fraunces demo, in-app manual,
quick starts); Yantra's own programs and plugins stripped; the licence
texts of every bundled MSYS2 package (share/licenses/msys2, with
PACKAGES.txt: DLL, package, version, licence); the licence texts of
REUSE.toml (share/doc/yantra/LICENSES); LICENSE.txt, README.txt and
SOURCE.txt (the corresponding source of the GPL/AGPL/LGPL parts, from
YANTRA_SOURCE_URL: an empty one fails, NFR-09) at the top; then
check_windows_package.py on the staged folder, the zip (deterministic:
sorted, dated SOURCE_DATE_EPOCH or the last commit) and the setup .exe
(yantra.nsi) from the same folder. Never Mesa (a test tool, 16.2), never a
MAK plan (DEMO-12), no personal data.

Exit: 0 ok, 1 error, 77 makensis missing with --setup-required off.
"""

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import check_windows_package  # noqa: E402

# The MSYS2 UCRT64 package of each DLL the package may bundle, the folder
# of its licence texts in the sysroot (share/licenses/<folder>) and its
# licence (the package's own field, SPDX). A new DLL fails the packaging
# until it is listed here (and in THIRD_PARTY.md).
MSYS2_DLLS = {
    "glfw3.dll": ("glfw", "glfw", "Zlib"),
    "libbrotlicommon.dll": ("brotli", "brotli", "MIT"),
    "libbrotlidec.dll": ("brotli", "brotli", "MIT"),
    "libcurl-4.dll": ("curl-winssl", "curl", "curl"),
    "libgcc_s_seh-1.dll": ("libgcc", "libgcc", "GPL-3.0-or-later WITH GCC-exception-3.1"),
    "libhpdf.dll": ("libharu", "libharu", "Zlib"),
    "libiconv-2.dll": ("libiconv", "libiconv", "LGPL-2.1-or-later"),
    "libidn2-0.dll": ("libidn2", None, "GPL-2.0-or-later OR LGPL-3.0-or-later"),
    "libintl-8.dll": ("gettext-runtime", "gettext-runtime", "LGPL-2.1-or-later"),
    "libpng16-16.dll": ("libpng", "libpng", "libpng-2.0"),
    "libpsl-5.dll": ("libpsl", "libpsl", "MIT"),
    "libsqlite3-0.dll": ("sqlite", "sqlite", "blessing (public domain)"),
    "libssh2-1.dll": ("libssh2-wincng", "libssh2", "BSD-3-Clause"),
    "libstdc++-6.dll": ("libstdc++", "libstdc++", "GPL-3.0-or-later WITH GCC-exception-3.1"),
    "libunistring-5.dll": ("libunistring", "libunistring", "LGPL-3.0-or-later OR GPL-2.0-or-later"),
    "libwinpthread-1.dll": ("libwinpthread", "libwinpthread", "MIT AND BSD-3-Clause"),
    "libzstd.dll": ("zstd", "zstd", "BSD-3-Clause OR GPL-2.0-or-later"),
    "zlib1.dll": ("zlib", "zlib", "Zlib"),
}
# The libidn2 package carries no licence file: the GNU licence texts it
# names, from the build machine's common licences (base-files).
COMMON_LICENSES = {"libidn2": [("/usr/share/common-licenses/GPL-2", "COPYING"),
                               ("/usr/share/common-licenses/LGPL-3", "COPYING.LESSERv3"),
                               ("/usr/share/common-licenses/GPL-3", "COPYINGv3")]}
OWN = ("yantra.exe", "yantra-probe.exe", "yantra-plugin-test.exe")


def fail(msg: str) -> int:
    print(f"package_windows: {msg}", file=sys.stderr)
    return 1


def lock_versions(locks: list[pathlib.Path]) -> dict[str, tuple[str, str, str]]:
    """package name -> (version, file name, sha256) from MSYS2 lock files."""
    out = {}
    for lock in locks:
        for line in lock.read_text().splitlines():
            parts = line.split()
            if len(parts) == 5 and parts[0] == "file":
                out[parts[1].removeprefix("mingw-w64-ucrt-x86_64-")] = (parts[2], parts[3], parts[4])
    return out


def git(source: pathlib.Path, *args: str) -> str:
    r = subprocess.run(["git", "-C", str(source), *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def write(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def readme(version: str, source_url: str) -> str:
    return f"""Yantra {version} for Windows x64
==============================

Yantra measures TETRA coverage inside buildings, floor by floor: import
the floor plans, scale and line them up, walk the floors with a radio,
and get interpolated coverage maps and an acceptance report. It records
broadcast and system information, received levels and signal quality
only: no voice, short data, subscriber addresses or decryption.

Start
  bin\\yantra.exe             the app (Help > Tour; --demo: the demo project
                             with the simulated radio, no radio needed)
  bin\\yantra-probe.exe       measuring without a window, remote API
  bin\\yantra-plugin-test.exe the plugin test kit
  bin\\yantra-hackrf.exe     the HackRF device check of yantra-radios
  bin\\yantra-radio-host.exe the radio plugin host of yantra-radios (info,
                             devices, conformance)
  bin\\yantra-gen.exe        the TETRA test-signal generator of yantra-radios
                             (scenarios in share\\yantra-radios\\scenarios)

The portable zip runs from any folder; nothing is installed. The setup
.exe installs the same files for the current user (no administrator
rights) with a Start menu entry and an uninstaller (Settings > Apps).

Needs Windows 10 or 11 (x64) and a graphics driver with OpenGL 3.3.
Settings, projects and plugins of the user: %APPDATA%\\yantra; caches:
%LOCALAPPDATA%\\yantra.

USB radios (HackRF One, RTL-SDR, Airspy) need the WinUSB driver: install
it once per device with Zadig (https://zadig.akeo.ie), then the radio
appears in the Radio menu. A TETRA terminal (PEI) on a serial port needs
no extra driver beyond its USB serial driver.

Documentation: the user manual as PDF in share\\doc\\yantra (Help > User
manual (PDF)) and as text in share\\yantra\\manual (Help), quick starts
in share\\yantra\\quickstart, licences in share\\licenses and LICENSE.txt,
third-party components in share\\yantra\\THIRD_PARTY.md, the source code:
SOURCE.txt and {source_url}.

Homepage https://curtat.be; maintainer Curtat BV <info@curtat.be>.
"""


def source_note(version, source_url, commit, radios_url, radios_commit, radios_tag,
                msys2_rows, pdfium_version) -> str:
    rows = "\n".join(f"  {p} {v} ({lic})" for p, v, lic in msys2_rows)
    return f"""Corresponding source (GPL-3.0, AGPL-3.0, LGPL)
==============================================

Yantra is free software under the GNU General Public License version 3 or
later (LICENSE.txt). The radio plugins with the TETRA decoder include
osmo-tetra and libosmocore under the GNU Affero General Public License
version 3 or later; some bundled libraries are under the GNU Lesser General
Public License (libusb in the radio plugins; libiconv, libintl,
libunistring and libidn2 next to the programs). You may get the complete
corresponding source of this package, free of charge, as follows:

Yantra {version}
  {source_url}
  (clone: {source_url}.git), commit {commit}

yantra-radios (radio plugins, decoder, generator; the third_party/
yantra-radios submodule of Yantra, with every bundled library's source
at its pinned version and the build scripts, RAD-137)
  {radios_url}
  {radios_tag} (commit {radios_commit})

MSYS2 UCRT64 packages whose DLLs are next to the programs in bin\\ (the
build pins them by SHA-256 in tools/windows/msys2.lock of Yantra and
tools/sdk/msys2.lock of yantra-radios); their sources and build recipes:
https://github.com/msys2/MINGW-packages and
https://repo.msys2.org/mingw/sources/
{rows}

PDFium {pdfium_version} (BSD-3-Clause and Apache-2.0; prebuilt by
https://github.com/bblanchon/pdfium-binaries, source
https://pdfium.googlesource.com/pdfium/).

The licence texts: LICENSE.txt, share\\doc\\yantra\\LICENSES,
share\\licenses (yantra-radios, pdfium, msys2) and
share\\yantra\\THIRD_PARTY.md.
"""


def nsis_uninstall_list(stage: pathlib.Path) -> str:
    lines = []
    for p in sorted(stage.rglob("*"), key=lambda x: (-len(x.parts), str(x))):
        rel = str(p.relative_to(stage)).replace("/", "\\")
        if p.is_dir():
            continue
        lines.append(f'  Delete "$INSTDIR\\{rel}"')
    dirs = sorted((p for p in stage.rglob("*") if p.is_dir()), key=lambda x: (-len(x.parts), str(x)))
    for d in dirs:
        lines.append(f'  RMDir "$INSTDIR\\{str(d.relative_to(stage)).replace("/", chr(92))}"')
    return "\n".join(lines) + "\n"


def make_zip(stage: pathlib.Path, zip_path: pathlib.Path, epoch: int) -> None:
    stamp = time.gmtime(max(epoch, 315532800))[:6]
    tmp = zip_path.with_suffix(".zip.tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(stage.rglob("*")):
            rel = f"{stage.name}/{p.relative_to(stage).as_posix()}"
            if p.is_dir():
                info = zipfile.ZipInfo(rel + "/", stamp)
                info.external_attr = (0o40755 << 16) | 0x10
                z.writestr(info, b"")
            else:
                info = zipfile.ZipInfo(rel, stamp)
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(p, "rb") as f:
                    z.writestr(info, f.read(), compresslevel=9)
    tmp.replace(zip_path)


def main() -> int:
    ap = argparse.ArgumentParser(description="Windows zip and setup .exe of Yantra (NFR-09)")
    ap.add_argument("--build", type=pathlib.Path, required=True, help="the Windows build tree")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--source", type=pathlib.Path, default=HERE.parent.parent)
    ap.add_argument("--version", required=True)
    ap.add_argument("--source-url", default="")
    ap.add_argument("--sysroot", type=pathlib.Path, required=True)
    ap.add_argument("--strip", required=True)
    ap.add_argument("--cmake", default="cmake")
    ap.add_argument("--makensis", default="makensis")
    ap.add_argument("--ico", type=pathlib.Path, required=True)
    ap.add_argument("--pdfium-version", default="")
    ap.add_argument("--radios-url", default="")
    ap.add_argument("--msys2-lock", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--keep-stage", action="store_true")
    a = ap.parse_args()

    if not a.source_url:
        return fail("YANTRA_SOURCE_URL is empty (NFR-09 release check)")
    if not (a.build / "bin/yantra.exe").is_file():
        return fail(f"{a.build} has no bin/yantra.exe (a Windows tree with the app)")
    if shutil.which(a.makensis) is None:
        return fail(f"makensis not found ({a.makensis}): install NSIS")
    name = f"yantra-{a.version}-windows-x86_64"
    out = a.out.resolve()
    stage = out / name
    shutil.rmtree(stage, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    log = out / "install.log"
    with open(log, "w") as f:
        r = subprocess.run([a.cmake, "--install", str(a.build), "--prefix", str(stage)], stdout=f, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        return fail(f"cmake --install failed (see {log})")
    if not (stage / "share/doc/yantra" / f"yantra-manual-{a.version}.pdf").is_file():
        return fail("no PDF manual in the staging folder: make it first (ninja docs-manual, DOC-02a)")

    # The bundled DLLs (copy_dlls.py at install) and their packages.
    dll_names = sorted(p.name for p in (stage / "bin").glob("*.dll"))
    locks = lock_versions(a.msys2_lock)
    rows, seen = [], set()
    lic_root = stage / "share/licenses/msys2"
    listing = []
    for dll in dll_names:
        if dll.lower() == "pdfium.dll":
            continue
        entry = MSYS2_DLLS.get(dll.lower())
        if entry is None:
            return fail(f"bin/{dll}: not in MSYS2_DLLS (add its package and licence, and THIRD_PARTY.md)")
        pkg, folder, lic = entry
        version = locks.get(pkg, ("?",))[0]
        if version == "?":
            return fail(f"{pkg}: not in the MSYS2 lock files {[str(x) for x in a.msys2_lock]}")
        listing.append(f"{dll} {pkg} {version} {lic}")
        if pkg in seen:
            continue
        seen.add(pkg)
        rows.append((pkg, version, lic))
        dest = lic_root / pkg
        dest.mkdir(parents=True, exist_ok=True)
        if folder:
            src = a.sysroot / "share/licenses" / folder
            if not src.is_dir():
                return fail(f"{pkg}: no licence texts in {src}")
            shutil.copytree(src, dest, dirs_exist_ok=True)
        for src, target in COMMON_LICENSES.get(pkg, []):
            if not pathlib.Path(src).is_file():
                return fail(f"{pkg}: {src} not found (the GNU licence texts, base-files)")
            shutil.copyfile(src, dest / target)
    write(lic_root / "PACKAGES.txt",
          "# DLL, MSYS2 UCRT64 package (mingw-w64-ucrt-x86_64-<package>), version, licence\n"
          + "\n".join(listing) + "\n")

    # Yantra's own programs and plugins without debug information.
    own = [stage / "bin" / n for n in OWN] + sorted((stage / "lib/yantra/plugins").rglob("*.dll")) \
        + [p for p in (stage / "bin").glob("yantra-*.exe") if p.name not in OWN]
    for p in own:
        r = subprocess.run([a.strip, "--strip-debug", str(p)], capture_output=True, text=True)
        if r.returncode != 0:
            return fail(f"strip {p.name}: {r.stderr.strip()}")

    # Licences and notes at the top.
    docs = stage / "share/doc/yantra/LICENSES"
    docs.mkdir(parents=True, exist_ok=True)
    for lic in sorted((a.source / "LICENSES").glob("*.txt")):
        shutil.copyfile(lic, docs / lic.name)
    shutil.copyfile(a.source / "LICENSE", stage / "LICENSE.txt")
    commit = git(a.source, "describe", "--always", "--dirty", "--abbrev=12") or "unknown"
    radios = a.source / "third_party/yantra-radios"
    radios_commit = git(radios, "rev-parse", "--short=12", "HEAD") or "unknown"
    radios_tag = git(radios, "describe", "--tags", "--exact-match", "HEAD") or "the pinned commit"
    radios_url = a.radios_url.removesuffix(".git") or "https://github.com/peterdewitte/yantra-radio-plugins"
    write(stage / "README.txt", readme(a.version, a.source_url))
    write(stage / "SOURCE.txt", source_note(a.version, a.source_url, commit, radios_url, radios_commit,
                                            radios_tag, rows, a.pdfium_version or "(see share/licenses/pdfium)"))

    bad = check_windows_package.check(
        {p.relative_to(stage).as_posix(): p.read_bytes() for p in stage.rglob("*") if p.is_file()}, a.source)
    if bad:
        for b in bad:
            print(f"package_windows: FAIL: {b}", file=sys.stderr)
        return 1

    epoch = int(os.environ.get("SOURCE_DATE_EPOCH") or git(a.source, "log", "-1", "--format=%ct") or time.time())
    zip_path = out / f"{name}.zip"
    make_zip(stage, zip_path, epoch)

    setup = out / f"{name}-setup.exe"
    files_nsh = out / "uninstall-files.nsh"
    files_nsh.write_text(nsis_uninstall_list(stage))
    size_kb = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file()) // 1024
    nums = (re.findall(r"\d+", a.version) + ["0", "0", "0"])[:3]
    cmd = [a.makensis, "-V2", "-NOCD",
           f"-DVERSION={a.version}", f"-DVERSION4={'.'.join(nums)}.0", f"-DSTAGE={stage}",
           f"-DOUTFILE={setup}", f"-DICON={a.ico.resolve()}", f"-DSOURCE_URL={a.source_url}",
           f"-DFILES_NSH={files_nsh}", f"-DSIZE_KB={size_kb}", str(HERE / "yantra.nsi")]
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(epoch))
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0 or "warning" in (r.stdout + r.stderr).lower():
        (out / "makensis.log").write_text(r.stdout + r.stderr)
        return fail(f"makensis failed or warned (see {out / 'makensis.log'})")
    files_nsh.unlink()

    sums = []
    for p in (zip_path, setup):
        sums.append(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n")
    (out / "SHA256SUMS").write_text("".join(sums))
    if not a.keep_stage:
        shutil.rmtree(stage)
    for p in (zip_path, setup):
        print(f"package_windows: {p} ({p.stat().st_size / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
