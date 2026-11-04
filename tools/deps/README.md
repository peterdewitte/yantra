<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# tools/deps: static dependency builds (RAD-136)

Every library bundled into the plugins is pinned under `third_party/`
(git submodule, vendored git commit, or vendored files of a release
archive with its SHA-256) and built statically with only the features
used (RAD-132). `manifest.tsv` lists them: name, version, licence, folder,
release archive and SHA-256; its first three columns are the dependency
manifest compiled into the artefacts (`yantra::receiver::bundled_libraries()`).

| File | Purpose |
|------|---------|
| `manifest.tsv` | bundled libraries with versions, licences and archive checksums |
| `deps.cmake` | static targets `yantra::talloc` and `yantra::osmocore` (the libosmocore subset osmo-tetra needs) from the vendored sources; included by the top-level CMakeLists.txt |
| `CMakeLists.txt` | standalone project around `deps.cmake` with install rules |
| `build_deps.sh <prefix>` | configures, builds and installs the standalone project into `<prefix>` (libraries, headers, licence texts, `deps-manifest.tsv`); reuses the prefix when nothing changed (stamp) |
| `verify_sources.py [cache]` | downloads the release archives (or takes them from the cache, default `build/deps-cache`), checks their SHA-256 and compares every vendored file with the archive |
| `sdr.cmake` | static targets `yantra::usb`, `yantra::soapysdr` and `yantra::soapy_modules` (SoapySDR, SoapyHackRF/libhackrf, SoapyRTLSDR/librtlsdr, SoapyAirspy/libairspy, SoapyRemote, libusb), linked whole so the modules' static registrations stay in; `YANTRA_SOAPY_MODULES` lists the modules; included by `deps.cmake` |
| `talloc/config.h`, `libosmocore/config.h`, `libusb/config.h` | hand-written configure results: the upstream build systems (waf, autotools) are not on the tool lists, so the sources are compiled directly |
| `soapysdr/modules_static.cpp` | replaces SoapySDR's `lib/Modules.in.cpp`: no module search paths, no dlopen; the modules compiled into a plugin register themselves when it is loaded, so a system SoapySDR or its modules are never mixed in (RAD-132) |
| `libosmocore/backtrace.c` | replaces libosmocore's `src/core/backtrace.c`, which needs the logging framework the subset leaves out |

Two ways to use it, the same on Linux and in MSYS2 UCRT64 (Windows
DEFERRED):

- plain build: the top level includes `deps.cmake`, so the libraries are
  built in the build tree (also instrumented in the sanitizer trees);
- prefix: `tools/deps/build_deps.sh build/deps`, then configure with
  `-DYANTRA_DEPS_PREFIX=$PWD/build/deps`. CI caches the prefix keyed on
  the hash of this folder and the vendored sources (RAD-134), and Yantra
  can build the plugins the same way (RAD-138).

osmo-tetra itself (submodule, never `crypto/`) is compiled by
`cmake/OsmoTetra.cmake` into `yantra::osmo_tetra` on `yantra::osmocore`.
SoapyRemote's client is a module like the others (network radios: the
servers in `YANTRA_SOAPY_REMOTE`); its server side is the static target
`yantra_soapy_remote_server` for the loopback tests only. SoapySDR modules
not built (RAD-50, THIRD_PARTY.md): SoapyBladeRF (libbladeRF needs the ADI
no-OS AD9361 driver, whose licence is not GPL-3.0 compatible: owner
question), SoapyPlutoSDR (libiio needs libxml2: owner question); S:
SoapyLMS7, SoapyUHD.
To add a module: vendor the release files unchanged under `third_party/`,
add manifest rows (archive URL and SHA-256), compile it in `sdr.cmake`
(static library linked whole by `yantra::soapy_modules`, one
`YANTRA_SOAPY_MODULES` entry), install it in `CMakeLists.txt`, add its
folder to the stamp in `build_deps.sh`, and run `verify_sources.py`.
