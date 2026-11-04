#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install into a DESTDIR and check the result (RAD-99, RAD-132, RAD-137).

usage: check_install.py BUILD_DIR DESTDIR

Runs `cmake --install BUILD_DIR --prefix /usr` with DESTDIR set (nothing
is written outside DESTDIR; no udevadm, no root), then checks that the
plugins, tools, udev rules, RTL-SDR blacklist, licence bundle and docs
are there, that nothing landed in etc/ or outside usr/, that the
installed plugins pass the completeness check (RAD-133) and that the
installed yantra-hackrf finds the installed HackRF plugin by itself.
"""
import os
import shutil
import subprocess
import sys

EXPECTED = [
    "usr/lib/yantra-radios/plugins/hackrf.so",
    "usr/lib/yantra-radios/plugins/simulator.so",
    "usr/lib/yantra-radios/plugins/soapysdr.so",
    "usr/bin/yantra-radio-host",
    "usr/bin/yantra-gen",
    "usr/bin/yantra-hackrf",
    "usr/lib/udev/rules.d/60-yantra-sdr.rules",
    "usr/lib/modprobe.d/yantra-rtl-sdr-blacklist.conf",
    "usr/share/licenses/yantra-radios/LICENSE",
    "usr/share/licenses/yantra-radios/THIRD_PARTY.md",
    "usr/share/licenses/yantra-radios/LICENSES/GPL-3.0-or-later.txt",
    "usr/share/licenses/yantra-radios/osmo-tetra/COPYING",
    "usr/share/licenses/yantra-radios/libusb/COPYING",
    "usr/share/licenses/yantra-radios/soapysdr/LICENSE_1_0.txt",
    "usr/share/licenses/yantra-radios/soapyhackrf/LICENSE",
    "usr/share/licenses/yantra-radios/hackrf/BSD-3-Clause.txt",
    "usr/share/doc/yantra-radios/README.md",
    "usr/share/doc/yantra-radios/RADIO_PLUGINS.md",
    "usr/share/doc/yantra-radios/YANTRA_HACKRF.md",
    "usr/share/yantra-radios/scenarios/indoor_good_das.json",
]
NOT_INSTALLED = [
    "usr/lib/yantra-radios/plugins/beacon.so",       # the example
    "usr/lib/yantra-radios/plugins/hackrf_fake.so",  # test builds
    "usr/lib/yantra-radios/plugins/soapysdr_fake.so",
]


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    build, root = sys.argv[1], os.path.abspath(sys.argv[2])
    shutil.rmtree(root, ignore_errors=True)
    env = dict(os.environ, DESTDIR=root)
    subprocess.run(["cmake", "--install", build, "--prefix", "/usr"], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    problems = []
    files = []
    for d, _, names in os.walk(root):
        files += [os.path.relpath(os.path.join(d, n), root) for n in names]
    for f in files:
        if not f.startswith("usr/") or f.startswith("usr/etc/") or f.startswith("etc/"):
            problems.append(f"installed outside usr/ or into etc/: {f}")
    for f in EXPECTED:
        if f not in files:
            problems.append(f"missing: {f}")
    for f in NOT_INSTALLED:
        if f in files:
            problems.append(f"must not be installed: {f}")
    rules = open(os.path.join(root, "usr/lib/udev/rules.d/60-yantra-sdr.rules")).read()
    if 'ATTRS{idProduct}=="6089"' not in rules or 'TAG+="uaccess"' not in rules:
        problems.append("udev rules: no HackRF One uaccess rule")
    blacklist = open(os.path.join(root, "usr/lib/modprobe.d/yantra-rtl-sdr-blacklist.conf")).read()
    if "blacklist dvb_usb_rtl28xxu" not in blacklist:
        problems.append("blacklist: dvb_usb_rtl28xxu missing")
    check = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "ci",
                         "check_completeness.py")
    plugins = os.path.join(root, "usr/lib/yantra-radios/plugins")
    if subprocess.run([sys.executable, check, plugins]).returncode != 0:
        problems.append("installed plugins fail the completeness check")
    # The installed tool finds the installed plugin (no HackRF here: exit 2,
    # not 1 "cannot load the plugin").
    tool = subprocess.run([os.path.join(root, "usr/bin/yantra-hackrf"), "list"], capture_output=True,
                          text=True, env={k: v for k, v in os.environ.items() if k != "YANTRA_FAKE_SOAPY"})
    if tool.returncode != 2 or "no HackRF found" not in tool.stderr:
        problems.append(f"installed yantra-hackrf list: exit {tool.returncode}: {tool.stderr.strip()}")
    for p in problems:
        print("check_install:", p)
    print(f"check_install: {len(files)} files, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
