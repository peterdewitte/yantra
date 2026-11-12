#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Windows packages' programs run on Windows (NFR-09, TST-02).

  smoke_test.py --zip ZIP --scratch DIR --version V --mesa-from DIR
  smoke_test.py --setup SETUP.exe --scratch DIR --version V --mesa-from DIR

ctest package_windows_zip / package_windows_setup of the Windows tree, run
natively on Windows (the test VM: wintest.sh test build-win -R
package_windows); elsewhere skipped (exit 77). No shell: Python only.

--zip unpacks the portable zip (zipfile) into the scratch folder; --setup
installs the setup .exe silently (/S /D=<scratch>\\install) for the
current user, checks its uninstall entry and Start menu shortcut, and at
the end uninstalls it silently and checks that every installed file, the
entry and the shortcut are gone. Then, in the unpacked or installed
folder:

- yantra.exe --version prints the version;
- yantra-probe.exe (standard input "quit") loads every plugin from the
  package's own lib\\yantra\\plugins (Yantra's and the radio plugins,
  PLG-26) and exits with 0;
- yantra-radio-host.exe info --plugin <package>\\...\\simulator.dll;
- yantra-gen.exe --list lists the scenarios of the package's own
  share\\yantra-radios\\scenarios (found relative to the program, RAD-71);
- the PDF manual share\\doc\\yantra\\yantra-manual-<version>.pdf is there
  (DOC-02a; Help > User manual (PDF) opens it);
- yantra.exe --demo --screenshot renders the demo project (the package's
  data folder) off-screen and writes a 1280x800 PNG.

The screenshot needs OpenGL 3: the test VM has no such driver, so the
test copies Mesa's opengl32.dll (llvmpipe, a 16.2 test tool) and the DLLs
it needs from the Windows test tree (--mesa-from) next to the unpacked
yantra.exe and sets GALLIUM_DRIVER=llvmpipe; the package itself ships no
Mesa (check_windows_package.py) and uses the system's OpenGL driver.
The copies are removed before the uninstall. User folders (%APPDATA%,
%LOCALAPPDATA%) of the programs point into the scratch folder.
"""

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from check_windows_package import read_pe  # noqa: E402

UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Yantra"
PLUGINS = ["probe.dll", "tetra.dll", "radios\\hackrf.dll", "radios\\pei.dll",
           "radios\\simulator.dll", "radios\\soapysdr.dll"]


class Failed(Exception):
    pass


def run(cmd, env, stdin=None, timeout=300, cwd=None):
    print("$ " + " ".join(str(c) for c in cmd), flush=True)
    r = subprocess.run([str(c) for c in cmd], input=stdin, capture_output=True, text=True,
                       env=env, timeout=timeout, cwd=cwd, errors="replace")
    out = r.stdout + r.stderr
    sys.stdout.write(out if out.endswith("\n") or not out else out + "\n")
    return r.returncode, out


def expect(cond, what):
    if not cond:
        raise Failed(what)
    print(f"ok: {what}", flush=True)


def mesa_files(build_bin: pathlib.Path, package_bin: pathlib.Path) -> list[pathlib.Path]:
    """opengl32.dll of the test tree and the DLLs it imports that the
    package does not have."""
    have = {p.name.lower() for p in package_bin.glob("*.dll")}
    todo, out, seen = [build_bin / "opengl32.dll"], [], set()
    while todo:
        f = todo.pop()
        if f.name.lower() in seen:
            continue
        seen.add(f.name.lower())
        out.append(f)
        for name in read_pe(f.read_bytes())[0]:
            dep = build_bin / name
            if name.lower() not in have and name.lower() not in seen and dep.is_file():
                todo.append(dep)
    return out


def png_size(path: pathlib.Path):
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", data[16:24])


def check_programs(root: pathlib.Path, version: str, mesa_from: pathlib.Path, scratch: pathlib.Path) -> None:
    bin_ = root / "bin"
    users = scratch / "users"
    shutil.rmtree(users, ignore_errors=True)
    for d in ("appdata", "localappdata", "home", "tmp"):
        (users / d).mkdir(parents=True)
    env = dict(os.environ, APPDATA=str(users / "appdata"), LOCALAPPDATA=str(users / "localappdata"),
               USERPROFILE=str(users / "home"), TMP=str(users / "tmp"), TEMP=str(users / "tmp"))
    for k in ("YANTRA_PLUGIN_PATH", "YANTRA_USER_PLUGIN_DIR", "YANTRA_DATA_DIR", "YANTRA_SCENARIO_DIR"):
        env.pop(k, None)

    rc, out = run([bin_ / "yantra.exe", "--version"], env)
    expect(rc == 0 and f"yantra {version}" in out, f"yantra.exe --version prints {version}")

    rc, out = run([bin_ / "yantra-probe.exe", "--no-monitor"], env, stdin="quit\n")
    expect(rc == 0, "yantra-probe.exe exits with 0 after quit")
    plugins = (root / "lib" / "yantra" / "plugins").resolve()
    loaded = {os.path.normcase(os.path.realpath(line.rsplit(" from ", 1)[1].strip()))
              for line in out.splitlines() if ": loaded " in line and " from " in line}
    for p in PLUGINS:
        expect(os.path.normcase(str(plugins / p)) in loaded, f"yantra-probe loads {p} from the package")

    rc, out = run([bin_ / "yantra-radio-host.exe", "info", "--plugin", plugins / "radios" / "simulator.dll"], env)
    expect(rc == 0 and "simulator" in out.lower(), "yantra-radio-host.exe info on the packaged simulator.dll")

    rc, out = run([bin_ / "yantra-gen.exe", "--list"], env)
    scenarios = (root / "share" / "yantra-radios" / "scenarios").resolve()
    first = out.splitlines()[0].lstrip("# ").strip() if out.strip() else ""
    expect(rc == 0 and first and os.path.normcase(os.path.realpath(first)) == os.path.normcase(str(scenarios))
           and "fraunces" in out, f"yantra-gen.exe --list names the package's scenarios ({first})")

    pdf = root / "share" / "doc" / "yantra" / f"yantra-manual-{version}.pdf"
    expect(pdf.is_file() and pdf.read_bytes()[:5] == b"%PDF-", f"the PDF manual {pdf.name} (DOC-02a)")

    copied = []
    try:
        for f in mesa_files(mesa_from, bin_):
            shutil.copy2(f, bin_ / f.name)
            copied.append(bin_ / f.name)
        print(f"test only: Mesa next to the packaged yantra.exe: {', '.join(p.name for p in copied)}", flush=True)
        shot = scratch / "demo.png"
        shot.unlink(missing_ok=True)
        rc, out = run([bin_ / "yantra.exe", "--demo", "--screenshot", shot],
                      dict(env, GALLIUM_DRIVER="llvmpipe"), timeout=400)
        expect(rc == 0 and "first frame (win32, OpenGL" in out, "yantra.exe --demo --screenshot: first frame")
        expect(shot.is_file() and png_size(shot) == (1280, 800), "the demo screenshot is a 1280x800 PNG")
        demo = users / "appdata" / "yantra" / "demo" / "fraunces" / "fraunces.yantra"
        expect(demo.is_file(), "the demo project came from the package's data folder (a fresh copy in %APPDATA%)")
    finally:
        for p in copied:
            p.unlink(missing_ok=True)


def zip_test(a) -> None:
    root_dir = a.scratch / "unpacked"
    shutil.rmtree(root_dir, ignore_errors=True)
    with zipfile.ZipFile(a.zip) as z:
        z.extractall(root_dir)
    tops = list(root_dir.iterdir())
    expect(len(tops) == 1 and tops[0].name == a.zip.stem, f"the zip unpacks into {a.zip.stem}")
    check_programs(tops[0], a.version, a.mesa_from, a.scratch)
    shutil.rmtree(root_dir, ignore_errors=True)


def setup_test(a) -> int:
    import winreg

    def entry():
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY) as k:
                return {n: winreg.QueryValueEx(k, n)[0] for n in
                        ("DisplayVersion", "Publisher", "InstallLocation", "URLInfoAbout", "Comments")}
        except OSError:
            return None

    if entry() is not None:
        print("SKIPPED: Yantra is installed for this user (its uninstall entry exists); not touched")
        return 77
    shortcut = pathlib.Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs/Yantra.lnk"
    inst = (a.scratch / "install").resolve()
    shutil.rmtree(inst, ignore_errors=True)
    rc, _ = run([a.setup, "/S", f"/D={inst}"], dict(os.environ), timeout=400)
    expect(rc == 0 and (inst / "bin" / "yantra.exe").is_file(), "the setup installs silently")
    rc = 1
    try:
        e = entry()
        expect(e is not None and e["DisplayVersion"] == a.version and e["Publisher"] == "Curtat BV"
               and e["URLInfoAbout"] == "https://curtat.be" and "https://" in e["Comments"]
               and pathlib.Path(e["InstallLocation"]).resolve() == inst,
               f"the uninstall entry (version, publisher, homepage, source, folder): {e}")
        expect(shortcut.is_file(), "the Start menu shortcut")
        expect((inst / "uninstall.exe").is_file(), "the uninstaller")
        check_programs(inst, a.version, a.mesa_from, a.scratch)
    finally:
        # _?= runs the uninstaller in place and waits for it (it cannot
        # delete itself then).
        if (inst / "uninstall.exe").is_file():
            rc, _ = run([inst / "uninstall.exe", "/S", f"_?={inst}"], dict(os.environ), timeout=400)
    left = sorted(p.relative_to(inst).as_posix() for p in inst.rglob("*") if p.is_file()) if inst.exists() else []
    expect(rc == 0 and left == ["uninstall.exe"], f"the silent uninstall removes every installed file (left: {left[:5]})")
    expect(entry() is None, "the uninstall entry is gone")
    expect(not shortcut.exists(), "the Start menu shortcut is gone")
    shutil.rmtree(inst, ignore_errors=True)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="The Windows packages' programs run (NFR-09)")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--zip", type=pathlib.Path)
    g.add_argument("--setup", type=pathlib.Path)
    ap.add_argument("--scratch", type=pathlib.Path, required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--mesa-from", type=pathlib.Path, required=True)
    a = ap.parse_args()
    if os.name != "nt":
        print("SKIPPED: runs on Windows (the test VM: wintest.sh test <build-win> -R package_windows)")
        return 77
    package = a.zip or a.setup
    if not package.is_file():
        print(f"FAIL: {package} not found: make it on the build machine first "
              "(ninja -C <windows tree> package-windows)")
        return 1
    a.scratch.mkdir(parents=True, exist_ok=True)
    try:
        if a.zip:
            zip_test(a)
            return 0
        return setup_test(a)
    except (Failed, subprocess.TimeoutExpired, OSError) as e:
        print(f"FAIL: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
