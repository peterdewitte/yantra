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
| osmo-tetra @ 59fb603 | AGPL-3.0-or-later | TETRA lower layers (RAD-101): burst split, coding primitives, MAC PDU parsing. Its `crypto/` directory is never built or used, nor are its upper MAC, SDS or voice parts. GPL-3.0 section 13 allows the combination; the plugin descriptor states the AGPL parts (RAD-12) | git submodule `third_party/osmo-tetra` from https://gitea.osmocom.org/tetra/osmo-tetra.git, pinned to 59fb603 (59fb603999ca, `git submodule update --init`); licence text in its `COPYING`. The lower-layer sources (PHY burst split, coding primitives, MAC PDU parsers) are compiled into the static library `yantra::osmo_tetra` (`cmake/OsmoTetra.cmake`) on the static libosmocore subset; the generator's cross-check test (`tests/gen_osmo`, RAD-93) links it plus osmo-tetra's burst synchroniser and test PDUs |
| libosmocore 1.11.0 (subset) | GPL-2.0-or-later | bits, bit vectors, convolutional (Viterbi) decoder, utilities and panic used by osmo-tetra's lower layers (RAD-101); `gsmtap.h` for GSMTAP (RAD-115) | vendored subset of the release archive https://downloads.osmocom.org/releases/libosmocore/libosmocore-1.11.0.tar.bz2 (SHA-256 8e8dbf5e7d6d705aa34461a4ab3a0bd57f6961582cef95a1aafef3c74604c6dc) in `third_party/libosmocore/`, files unchanged (`tools/deps/verify_sources.py`), licence text `COPYING`; built statically by `tools/deps/deps.cmake` (portable Viterbi kernels only, own `config.h`). Local replacement, not a patch: `src/core/backtrace.c` is replaced by `tools/deps/libosmocore/backtrace.c` (no logging framework) |
| talloc 2.4.3 (subset) | LGPL-3.0-or-later | memory allocator used by libosmocore | vendored subset (`talloc.c`, `talloc.h`, `lib/replace/replace.h`, `lib/replace/win32_replace.h`, `LICENSE`) of the release archive https://www.samba.org/ftp/talloc/talloc-2.4.3.tar.gz (SHA-256 dc46c40b9f46bb34dd97fe41f548b0e8b247b77a918576733c528e83abd854dd) in `third_party/talloc/`, unchanged; built statically by `tools/deps/deps.cmake` with its own `config.h` (talloc's waf build is not used); the release archive is part of the corresponding source (RAD-137) |
| SoapySDR 0.8.1 | BSL-1.0 | the only SDR front layer (RAD-50) | vendored subset of the release archive https://github.com/pothosware/SoapySDR/archive/refs/tags/soapy-sdr-0.8.1.tar.gz (SHA-256 a508083875ed75d1090c24f88abef9895ad65f0f1b54e96d74094478f0c400e6) in `third_party/soapysdr/`, files unchanged (`tools/deps/verify_sources.py`), licence `LICENSE_1_0.txt`; built statically by `tools/deps/sdr.cmake`. Local replacement, not a patch: `lib/Modules.in.cpp` (module search and dlopen) is replaced by `tools/deps/soapysdr/modules_static.cpp`: the modules are compiled into the plugin and nothing is loaded from the system |
| SoapyHackRF 0.3.6 | MIT | SoapySDR module of the HackRF (RAD-50, RAD-60) | vendored subset of the release archive https://github.com/pothosware/SoapyHackRF/archive/refs/tags/soapy-hackrf-0.3.6.tar.gz (SHA-256 7bdecb65074f0c30af707234fbb970687626d4b6e1768592325aa706ae46e572) in `third_party/soapyhackrf/`, files unchanged (`tools/deps/verify_sources.py`); compiled into the SDR plugins |
| libhackrf 2026.01.3 | BSD-3-Clause | vendor library of SoapyHackRF (`host/libhackrf/src/hackrf.c`, `hackrf.h`) | vendored subset of the release archive https://github.com/greatscottgadgets/hackrf/releases/download/v2026.01.3/hackrf-2026.01.3.tar.xz (SHA-256 d2b76a1115d9b4df648c29efb2f3c8e80009b7cf9a8adf37abbfdba72101b086) in `third_party/hackrf/`, files unchanged (`tools/deps/verify_sources.py`); licence in the file headers (text `LICENSES/BSD-3-Clause.txt`) |
| SoapyRTLSDR 0.3.4 | MIT | SoapySDR module of the RTL-SDR (RAD-50) | vendored subset of the release archive https://github.com/pothosware/SoapyRTLSDR/archive/refs/tags/soapy-rtlsdr-0.3.4.tar.gz (SHA-256 1602035b849bca2e9c189712a08d22dc0c7c3a936f9063ac9851bf3229d4460e) in `third_party/soapyrtlsdr/`, files unchanged (`tools/deps/verify_sources.py`) |
| librtlsdr 2.0.3 (osmocom) | GPL-2.0-or-later | vendor library of SoapyRTLSDR, with RTL-SDR Blog V4 support | vendored subset of the release archive https://github.com/osmocom/rtl-sdr/archive/refs/tags/v2.0.3.tar.gz (SHA-256 23dec530f40456a8396e7c897f313859877e18b4c6432c6d323645871e351f6f) in `third_party/rtl-sdr/`, files unchanged (`tools/deps/verify_sources.py`); licence `COPYING`; built with `DETACH_KERNEL_DRIVER` |
| SoapyAirspy 0.2.1 | MIT | SoapySDR module of the Airspy (RAD-50) | vendored subset of the release archive https://github.com/pothosware/SoapyAirspy/archive/refs/tags/soapy-airspy-0.2.1.tar.gz (SHA-256 6170c8462a026282c48b89cadf70d93b671999b3930267442a1754e2a67248f2) in `third_party/soapyairspy/`, files unchanged (`tools/deps/verify_sources.py`) |
| libairspy 1.0.10 | BSD-3-Clause | vendor library of SoapyAirspy (`libairspy/src`) | vendored subset of the release archive https://github.com/airspy/airspyone_host/archive/refs/tags/v1.0.10.tar.gz (SHA-256 fcca23911c9a9da71cebeffeba708c59d1d6401eec6eb2dd73cae35b8ea3c613) in `third_party/airspy/`, files unchanged (`tools/deps/verify_sources.py`); licence in the file headers (text `LICENSES/BSD-3-Clause.txt`) |
| libusb 1.0.30 | LGPL-2.1-or-later | USB access of the vendor libraries | vendored subset of the release archive https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2 (SHA-256 fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf) in `third_party/libusb/`, files unchanged (`tools/deps/verify_sources.py`), licence `COPYING`; built statically by `tools/deps/sdr.cmake` with its own `config.h` (usbfs backend, netlink hot-plug, no libudev; autotools not used); the release archive is part of the corresponding source (RAD-137) |
| SoapyBladeRF + libbladeRF, SoapyPlutoSDR + libiio/libad9361, SoapyRemote; S: SoapyLMS7 (LimeSuite), SoapyUHD (UHD) | per module | further SoapySDR modules of RAD-50 | *planned*: not built yet (see the build log); each is listed here with its own licence when it is added |
| pocketfft (C++ header-only), branch `cpp` at 4d865cb | BSD-3-Clause | FFT, spectrum and channelizer of the receiver (RAD-100); later the generator channel model | vendored unchanged in `third_party/pocketfft/` from https://github.com/mreineck/pocketfft (commit 4d865cba24359b05b37da4e75aee24498c3b78e7, 2026-10-18; `pocketfft_hdronly.h` SHA-256 518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d, `LICENSE.md`); header-only, compiled into the receiver library with `POCKETFFT_NO_MULTITHREADING` |
| nlohmann/json 3.12.0 | MIT | record files (RAD-65), scene JSON (RAD-36), later SigMF metadata and scenario files | vendored single header in `third_party/nlohmann_json/` from the v3.12.0 release (`json.hpp`, SHA-256 aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63), unchanged; header-only, compiled into what includes it |
| serde, serde_json (Rust crates) | MIT OR Apache-2.0 | allowed by RAD-04 for JSON in the Rust protocol crate | not used: the protocol crate (`receiver/rust`, crate `yantra-proto`) has no external crates (`Cargo.lock` lists only itself; `cargo-deny` checks it) |
| Rust standard library | MIT OR Apache-2.0 | linked statically through the Rust protocol crate `yantra-proto` (`receiver/rust`, static library in the TETRA decoder, RAD-101, RAD-132) | Rust toolchain |

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
| SoapySDRPlay3 and the SDRplay API | the SDRplay API is proprietary and cannot be built into a GPL plugin (RAD-50, RAD-132) |
