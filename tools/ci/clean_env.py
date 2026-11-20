#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RAD-133: the conformance kit (RAD-91) on every plugin in a clean
# environment, without SoapySDR, the vendor libraries or any other package
# of the build machine: the plugins of the build (lib/yantra/plugins/radios),
# the fake-module test builds (tests/radio-plugins, RAD-92) with their fixture, and the
# test host. A plugin that needs a library or file of the build machine
# fails here.
#
# Two ways to get the clean environment:
#   --sandbox bwrap (default): a bubblewrap sandbox on this machine (no
#     root): an empty root with only the dynamic loader, the glibc parts
#     and libudev of RAD-132 plus the C++ runtime the test host itself
#     needs, the kit read-only, a private /tmp, no network, a cleared
#     environment. Exits 77 (ctest SKIP) when bubblewrap is missing or
#     user namespaces are not allowed.
#   --sandbox none: run directly; for a machine that is already clean
#     (the CI job in a bare container, .github/workflows/ci.yml).
#
# Usage: clean_env.py --host EXE --plugins DIR [--plugins DIR]...
#                     --fixture FILE --report-dir DIR [--sandbox bwrap|none]
#                     [--jobs N] [--expect-fail PLUGIN]...
# --expect-fail: a fixture plugin that needs a library of the build tree;
# it loads outside, so the run fails unless the sandbox refuses it (proof
# that the environment is clean).
import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

SKIP = 77
# RAD-132 system libraries (tools/ci/check_completeness.py) and the C++
# runtime of the test host (the plugins link it statically).
SYSTEM_LIBS = re.compile(r"^(libc\.so\.6|libm\.so\.6|libmvec\.so\.1|libdl\.so\.2|libpthread\.so\.0|"
                         r"librt\.so\.1|ld-linux-[a-z0-9_-]+\.so\.[0-9]+|libudev\.so\.1)$")
HOST_RUNTIME = re.compile(r"^(libstdc\+\+\.so\.6|libgcc_s\.so\.1)$")
# Fake-module serial of the fixture (tests/radio/soapysdr, tests/radio/hackrf).
FAKE_SERIAL = "0000000000000000a1b2c3d4e5f60001"


def ldd_paths(exe):
    """Libraries the dynamic loader resolves for exe: name -> real path."""
    out = subprocess.run(["ldd", str(exe)], capture_output=True, text=True, check=True).stdout
    libs = {}
    for line in out.splitlines():
        m = re.match(r"\s*(\S+)\s+=>\s+(\S+)", line)
        if m and m.group(2) != "not":
            libs[m.group(1)] = pathlib.Path(m.group(2))
        else:
            m = re.match(r"\s*(/\S+ld-linux\S*)\s", line)
            if m:
                libs[pathlib.Path(m.group(1)).name] = pathlib.Path(m.group(1))
    return libs


def sandbox_libs(host):
    """The only libraries visible in the sandbox: the host's own needs,
    limited to the system libraries and the C++ runtime, plus the other
    glibc parts and libudev where the machine has them."""
    libs = {}
    resolved = ldd_paths(host)
    for name, path in resolved.items():
        if SYSTEM_LIBS.match(name) or HOST_RUNTIME.match(name):
            libs[name] = path
        else:
            raise SystemExit(f"clean_env: the test host needs {name}, which is not a system library")
    libc = resolved.get("libc.so.6")
    if libc is not None:
        for p in libc.parent.iterdir():
            if SYSTEM_LIBS.match(p.name) and p.name not in libs:
                libs[p.name] = p
    return libs


def bwrap_prefix(host, kit_binds, report_dir, env):
    bwrap = shutil.which("bwrap")
    if bwrap is None:
        return None
    libs = sandbox_libs(host)
    cmd = [bwrap, "--unshare-all", "--die-with-parent", "--clearenv",
           "--tmpfs", "/", "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
           "--dir", "/usr/lib", "--dir", "/lib64", "--symlink", "usr/lib", "/lib"]
    for name, path in sorted(libs.items()):
        real = path.resolve()
        cmd += ["--ro-bind", str(real), f"/usr/lib/{name}"]
        if name.startswith("ld-linux"):
            cmd += ["--ro-bind", str(real), f"/lib64/{name}"]
    for src, dst in kit_binds:
        cmd += ["--ro-bind", str(src), dst]
    cmd += ["--bind", str(report_dir), "/report", "--chdir", "/tmp",
            "--setenv", "LD_LIBRARY_PATH", "/usr/lib", "--setenv", "PATH", "/kit"]
    for k, v in env.items():
        cmd += ["--setenv", k, v]
    return cmd


def sandbox_works(prefix):
    try:
        r = subprocess.run(prefix + ["/kit/yantra-radio-host"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return False
    # The host prints its usage (exit 2) when it starts at all.
    return "usage: yantra-radio-host" in r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, type=pathlib.Path)
    ap.add_argument("--plugins", action="append", required=True, type=pathlib.Path)
    ap.add_argument("--fixture", required=True, type=pathlib.Path)
    ap.add_argument("--report-dir", required=True, type=pathlib.Path)
    ap.add_argument("--sandbox", choices=["bwrap", "none"], default="bwrap")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--expect-fail", action="append", default=[], type=pathlib.Path)
    a = ap.parse_args()

    plugins = []
    for d in a.plugins:
        plugins += sorted(d.glob("*.so"))
    if not plugins:
        print("clean_env: no plugins found")
        return 1
    a.report_dir.mkdir(parents=True, exist_ok=True)

    if a.sandbox == "bwrap":
        binds = [(a.host.resolve(), "/kit/yantra-radio-host"), (a.fixture.resolve(), "/kit/fixture.json")]
        binds += [(p.resolve(), f"/kit/plugins/{p.name}") for p in plugins + a.expect_fail]
        prefix = bwrap_prefix(a.host, binds, a.report_dir.resolve(), {})
        if prefix is None:
            print("clean_env: SKIP: bubblewrap (bwrap) is not installed")
            return SKIP
        if not sandbox_works(prefix):
            print("clean_env: SKIP: the bubblewrap sandbox cannot start here (user namespaces?)")
            return SKIP
        host, plugin_dir, fixture, report, control = "/kit/yantra-radio-host", "/kit/plugins", \
            "/kit/fixture.json", "/report", "/tmp/fake_control"
    else:
        prefix = []
        host, plugin_dir, fixture, report = str(a.host), None, str(a.fixture), str(a.report_dir)
        control = str(a.report_dir / "fake_control")

    def run(plugin):
        name = plugin.stem
        path = f"{plugin_dir}/{plugin.name}" if plugin_dir else str(plugin)
        env = {}
        if name.endswith("_fake"):
            env["YANTRA_FAKE_SOAPY"] = json.dumps(
                {"control": f"{control}/{name}", "devices": [{"serial": FAKE_SERIAL, "scenario": fixture}]})
        kit = [host, "conformance", "--plugin", path, "--report", f"{report}/{name}.json"]
        if prefix:
            cmd = list(prefix)  # after --clearenv: bwrap applies its options in order
            for k, v in env.items():
                cmd += ["--setenv", k, v]
            cmd += kit
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
        else:
            if env:
                pathlib.Path(control, name).mkdir(parents=True, exist_ok=True)
            r = subprocess.run(kit, capture_output=True, text=True, timeout=900, env={**os.environ, **env})
        return name, r.returncode, r.stdout + r.stderr

    status = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        for name, code, text in pool.map(run, a.expect_fail):
            refused = code != 0 and "cannot open shared object" in text
            print(f"{name}: exit {code}, {'refused as expected' if refused else 'NOT refused'}")
            if not refused:
                status = 1
                print(text)
        for name, code, text in pool.map(run, plugins):
            last = [l for l in text.splitlines() if l.startswith("result:")]
            print(f"{name}: exit {code}, {last[-1] if last else 'no result line'}")
            if code != 0:
                status = 1
                print(text)
    print(f"clean_env: {len(plugins)} plugins, {'all pass' if status == 0 else 'FAILED'}"
          f" ({'bubblewrap sandbox' if prefix else 'no sandbox'})")
    return status


if __name__ == "__main__":
    sys.exit(main())
