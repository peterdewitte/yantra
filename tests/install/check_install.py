#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install Yantra into a DESTDIR and check the shipped radio plugins
(PLG-26, RAD-99, RAD-132, RAD-133, RAD-137).

Runs `cmake --install BUILD --prefix /usr` with DESTDIR set (nothing is
written outside DESTDIR; no udevadm, no root), then checks: the shipped
radio plugins in usr/lib/yantra/plugins/radios (byte for byte the built
ones) and no test plugin anywhere; yantra-radios' licence bundle (the same
files as its own install), udev rules and RTL-SDR blacklist; nothing in
etc/ or outside usr/; the installed plugins pass yantra-radios'
completeness check and load in its test host from the installed folder.
Adapted from yantra-radios tests/install/check_install.py.
"""
import argparse
import filecmp
import os
import shutil
import subprocess
import sys

SHIPPED = ["hackrf.so", "pei.so", "simulator.so", "soapysdr.so"]
TEST_ONLY = ["beacon.so", "soapysdr_fake.so", "hackrf_fake.so"]
PLUGIN_DIR = "usr/lib/yantra/plugins/radios"
LICENCES = "usr/share/licenses/yantra-radios"
EXPECTED = [
    "usr/lib/udev/rules.d/60-yantra-sdr.rules",
    "usr/lib/modprobe.d/yantra-rtl-sdr-blacklist.conf",
    f"{LICENCES}/BUNDLE.md",
    f"{LICENCES}/LICENSE",
    f"{LICENCES}/THIRD_PARTY.md",
    f"{LICENCES}/LICENSES/GPL-3.0-or-later.txt",
    f"{LICENCES}/osmo-tetra/COPYING",
    f"{LICENCES}/libusb/COPYING",
    f"{LICENCES}/hackrf/COPYRIGHT.txt",
    # The TETRA technology plugin in the application's plugin folder (PLG-09, PLG-11).
    "usr/lib/yantra/plugins/tetra.so",
] + [f"{PLUGIN_DIR}/{p}" for p in SHIPPED]


def walk(root: str) -> list:
    files = []
    for d, _, names in os.walk(root):
        files += [os.path.relpath(os.path.join(d, n), root) for n in names]
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", required=True)
    ap.add_argument("--destdir", required=True)
    ap.add_argument("--radios-stage", required=True, help="yantra-radios install tree the plugins came from")
    ap.add_argument("--radios-source", required=True, help="yantra-radios source (completeness check)")
    ap.add_argument("--plugins", required=True, help="the build tree's shipped radio plugin folder")
    ap.add_argument("--app", type=int, default=1, help="1: the application is built")
    a = ap.parse_args()
    root = os.path.abspath(a.destdir)
    shutil.rmtree(root, ignore_errors=True)
    env = dict(os.environ, DESTDIR=root)
    subprocess.run(["cmake", "--install", a.build, "--prefix", "/usr"], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    problems = []
    files = walk(root)
    for f in files:
        if not f.startswith("usr/") or f.startswith("usr/etc/"):
            problems.append(f"installed outside usr/ or into etc/: {f}")
        if os.path.basename(f) in TEST_ONLY:
            problems.append(f"test plugin installed: {f}")
    expected = EXPECTED + (["usr/bin/yantra"] if a.app else [])
    for f in expected:
        if f not in files:
            problems.append(f"missing: {f}")
    plugins = os.path.join(root, PLUGIN_DIR)
    installed = sorted(os.listdir(plugins)) if os.path.isdir(plugins) else []
    if installed != sorted(SHIPPED):
        problems.append(f"{PLUGIN_DIR} holds {installed}, expected {sorted(SHIPPED)}")
    for p in SHIPPED:
        built, inst = os.path.join(a.plugins, p), os.path.join(plugins, p)
        if os.path.exists(inst) and not filecmp.cmp(built, inst, shallow=False):
            problems.append(f"{PLUGIN_DIR}/{p} differs from the built plugin")
    # The licence bundle as yantra-radios installs it, complete.
    stage_bundle = os.path.join(a.radios_stage, "share/licenses/yantra-radios")
    want = sorted(walk(stage_bundle))
    got = sorted(walk(os.path.join(root, LICENCES)))
    if want != got:
        problems.append(f"licence bundle: {len(got)} files, yantra-radios installs {len(want)}: "
                        f"missing {sorted(set(want) - set(got))[:5]}")
    rules = os.path.join(root, "usr/lib/udev/rules.d/60-yantra-sdr.rules")
    if os.path.exists(rules):
        text = open(rules, encoding="utf-8").read()
        if 'ATTRS{idProduct}=="6089"' not in text or 'TAG+="uaccess"' not in text:
            problems.append("udev rules: no HackRF One uaccess rule")
    # Self-contained (RAD-132/133): yantra-radios' own check on the installed folder.
    check = os.path.join(a.radios_source, "tools", "ci", "check_completeness.py")
    if os.path.isdir(plugins) and subprocess.run([sys.executable, check, plugins]).returncode != 0:
        problems.append("installed plugins fail the completeness check")
    # They load from the installed folder (test host of the same release).
    host = os.path.join(a.radios_stage, "bin", "yantra-radio-host")
    if os.path.exists(host):
        for p in SHIPPED:
            r = subprocess.run([host, "info", "--plugin", os.path.join(plugins, p)], capture_output=True, text=True)
            if r.returncode != 0:
                problems.append(f"installed {p} does not load: {r.stderr.strip()[:200]}")
    for p in problems:
        print("check_install:", p)
    print(f"check_install: {len(files)} files, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
