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
| `talloc/config.h`, `libosmocore/config.h` | hand-written configure results: the upstream build systems (waf, autotools) are not on the tool lists, so the sources are compiled directly |
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
SoapySDR, its modules, the vendor libraries and libusb join this folder
with the SDR plugins (phase 5).
