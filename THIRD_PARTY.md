<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Third-party components

yantra-radios is GPL-3.0-or-later (RAD-05). This file lists every library
and tool it uses, with its licence, what it is used for and how it is
obtained (RAD-04, RAD-135, Yantra NFR-01). Each entry is checked against
the component's own licence file when the component is added; licence
texts are in [LICENSES/](LICENSES/) or, for git submodules, in the
submodule itself.

Every bundled library is pinned under `third_party/` (git submodule, or
release archive with its SHA-256) and built statically into the plugins
with only the features used, by the scripts in `tools/deps/` (RAD-132,
RAD-136). Entries marked *planned* are not in the repository yet; their
version, source and checksum are filled in when they are added.

## Bundled libraries

| Component | Licence | Used for | How it is obtained |
|-----------|---------|----------|--------------------|
| osmo-tetra @ 59fb603 | AGPL-3.0-or-later | TETRA lower layers (RAD-101): burst split, coding primitives, MAC PDU parsing. Its `crypto/` directory is never built or used, nor are its upper MAC, SDS or voice parts. GPL-3.0 section 13 allows the combination; the plugin descriptor states the AGPL parts (RAD-12) | git submodule `third_party/osmo-tetra` from https://gitea.osmocom.org/tetra/osmo-tetra.git, pinned to 59fb603 (59fb603999ca, `git submodule update --init`); licence text in its `COPYING`. Not built yet: the sources used are linked from phase 4 |
| libosmocore | GPL-2.0-or-later | bits, convolutional code, CRC and GSMTAP helpers used by osmo-tetra | *planned*: pinned in `third_party/`, static build |
| SoapySDR | BSL-1.0 | the only SDR front layer (RAD-50) | *planned*: pinned in `third_party/`, static build |
| SoapySDR modules and their vendor libraries (e.g. HackRF, RTL-SDR) | per module (GPL-2.0-or-later, MIT, ...) | device access below SoapySDR (RAD-50) | *planned*: pinned in `third_party/`, static build; each listed here with its own licence |
| libusb | LGPL-2.1-or-later | USB access of the vendor libraries | *planned*: pinned in `third_party/`, static build (source offered with every release, RAD-137) |
| pocketfft (C++ header-only) | BSD-3-Clause | FFT and channelizer (RAD-100), generator channel model | *planned*: vendored in `third_party/pocketfft` |
| nlohmann/json 3.12.0 | MIT | record files (RAD-65), scene JSON (RAD-36), later SigMF metadata and scenario files | vendored single header in `third_party/nlohmann_json/` from the v3.12.0 release (`json.hpp`, SHA-256 aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63), unchanged; header-only, compiled into what includes it |
| serde, serde_json (Rust crates) | MIT OR Apache-2.0 | JSON in the Rust protocol crate (RAD-101) | *planned*: vendored crates, built offline |
| Rust standard library | MIT OR Apache-2.0 | linked statically through the Rust protocol crate | Rust toolchain |

## Test libraries (never shipped)

| Component | Licence | Used for | How it is obtained |
|-----------|---------|----------|--------------------|
| doctest 2.4.12 | MIT | unit tests (Yantra TST-01) | vendored single header in `third_party/doctest/` from the v2.4.12 release archive (SHA-256 73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70), unchanged |

## Build and test tools (never linked)

| Tool | Licence | Used for |
|------|---------|----------|
| CMake, Ninja | BSD-3-Clause, Apache-2.0 | build |
| gcc / clang | GPL-3.0-or-later WITH GCC-exception-3.1 / Apache-2.0 WITH LLVM-exception | C++20 compiler |
| cargo / rustc | MIT OR Apache-2.0 | Rust protocol crate (RAD-101) |
| cargo-deny, cargo-fuzz (optional) | MIT OR Apache-2.0 | licence check and fuzzing of the Rust crate |
| reuse | GPL-3.0-or-later | `reuse lint` licence compliance check |
| Python 3 (standard library) | PSF-2.0 | test and check scripts |
| PyYAML (CI lint only) | MIT | YAML syntax check of the workflows in `tools/ci/lint.sh` (RAD-134) |
| binutils (readelf, objdump) | GPL-3.0-or-later | completeness checks of the plugins (RAD-133) |

## Excluded

| Component | Reason |
|-----------|--------|
| OnAir code and libraries | excluded source; its `dect2cli` may only be run as an external test oracle, no code of it is used or shipped |
| osmo-tetra `crypto/` | decryption is out of scope |
| Proprietary survey software, file formats and dongles | excluded source |
