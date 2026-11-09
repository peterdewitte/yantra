<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Building yantra-radios

How to build the plugins, the tools and the tests, how the bundled
libraries are built in statically, the completeness checks and the
release (RAD-01..05, RAD-131..138). Tests and their labels:
[CONTRIBUTING.md](CONTRIBUTING.md#building-and-testing).

## Linux (x86-64; arm64 is optional)

Needs CMake >= 3.20, Ninja, gcc (>= 13) or clang with C++20, Python 3,
cargo/rustc (the protocol crate, RAD-101) and binutils (readelf, for the
completeness check). Optional: Doxygen (the radio API reference), reuse
(licence lint), bubblewrap (the clean-environment kit run). No SoapySDR,
vendor library or libusb package is needed: they are built from
`third_party/`.

    git submodule update --init
    cmake -S . -B build -G Ninja
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

`tools/ci/run_linux.sh [build-dir]` runs the same steps as the CI job
(static dependency build, warnings as errors, fast tests, completeness,
lint).

### Presets (CMakePresets.json)

| Preset | Tree | What |
|---|---|---|
| `default` | `build` | RelWithDebInfo, SIMD kernels |
| `scalar` | `build-scalar` | `-DYANTRA_SIMD=OFF`: the plain C++ fallback, same results (goldens identical) |
| `ci`, `ci-scalar` | `build` | warnings are errors (`YANTRA_WERROR`) |
| `asan-ubsan` | `build-asan` | AddressSanitizer + UBSan, hang guards x5; tests `-LE timing` |
| `tsan` | `build-tsan` | ThreadSanitizer, hang guards x10; tests `-LE timing` |
| `fuzz` | `build-fuzz` | clang + libFuzzer targets (RAD-96, [docs/FUZZING.md](docs/FUZZING.md)) |

### Options

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_SIMD` | ON | SIMD kernels of the receiver |
| `YANTRA_WERROR` | OFF | warnings are errors (ON in CI) |
| `YANTRA_SANITIZER` | "" | `asan-ubsan` or `tsan` |
| `YANTRA_DEPS_PREFIX` | "" | use the dependency build of `tools/deps/build_deps.sh` instead of building them in-tree |
| `YANTRA_RADIOS_BUILD_TESTS` | ON | the test suite |
| `YANTRA_RADIOS_SOURCE_URL` | the public repository | source URL in the plugin descriptors (RAD-12, RAD-137) |

## Bundled libraries, built statically (RAD-132, RAD-136)

Every library a plugin uses is pinned under `third_party/` (a git
submodule, or an unchanged subset of a release archive whose SHA-256 is in
`tools/deps/manifest.tsv`) and compiled with only the features used, as
position-independent code with hidden symbols, into each plugin:
osmo-tetra, libosmocore and talloc (`tools/deps/deps.cmake`), SoapySDR with
the HackRF, RTL-SDR, Airspy and Remote modules, libhackrf, librtlsdr,
libairspy and libusb (`tools/deps/sdr.cmake`), pocketfft, nlohmann/json and
the Rust protocol crate. SoapySDR's module loader is replaced so no system
SoapySDR or module is ever loaded. The list, versions, licences and local
replacements: [THIRD_PARTY.md](THIRD_PARTY.md), [tools/deps/README.md](tools/deps/README.md).

- In-tree (default): the main build compiles them, so sanitizer trees
  instrument them too.
- Prefix: `tools/deps/build_deps.sh <prefix>` builds and installs them once
  (reused while `tools/deps` and the vendored sources are unchanged, the
  CI cache key); configure with `-DYANTRA_DEPS_PREFIX=<prefix>`.
- `tools/deps/verify_sources.py [cache]` downloads each release archive
  (or takes it from `build/deps-cache`), checks its SHA-256 and that every
  vendored file is byte-identical to the archive's (CI lint job).

Each artefact embeds the dependency manifest (name, version, licence):
`yantra-radio-host --version` and the plugins' "Build info" sensor
(RAD-45).

## Completeness checks (RAD-133)

- `tools/ci/check_completeness.py <dir or .so>`: the NEEDED entries hold
  only the allowed system libraries (glibc parts, libudev), no RPATH, no
  text relocations or executable stack, and only `yantra_radio_entry` is
  exported (version script `cmake/radio_plugin.map`). The file also names
  no folder of the machine that built it (this source tree, the builder's
  home folder): `tools/deps/deps.cmake` and `cmake/RustProto.cmake` make
  source paths relative with `-ffile-prefix-map` and `--remap-path-prefix`,
  so a debugger needs `directory <source tree>`. `package_release.sh` looks
  for the same folders in every file of the package (not in an `asan-ubsan`
  tree, never shipped: gcc's AddressSanitizer keeps the full paths). ctests
  `completeness_<plugin>` and `completeness_plugins`.
- `tools/ci/clean_env.py`: the conformance kit on the plugins in an
  environment without SoapySDR or vendor packages (bubblewrap locally,
  ctest `clean_env_conformance`; a bare ubuntu:24.04 container in CI with
  `--sandbox none`).

## Install (RAD-99)

`cmake --install build --prefix /usr` (or any prefix; `DESTDIR` works):
plugins to `lib/yantra-radios/plugins`, tools to `bin`, scenarios to
`share/yantra-radios`, udev rules to `lib/udev/rules.d`, the RTL-SDR DVB-T
blacklist to `lib/modprobe.d`, the licence bundle to
`share/licenses/yantra-radios`, docs to `share/doc/yantra-radios`.
Nothing is written to `/etc` and nothing runs udevadm: for a prefix
other than `/usr`, copy the udev rules to `/etc/udev/rules.d` and reload
udev as root. ctest `install_destdir` checks the result.

## Windows (MSYS2 UCRT64) - deferred

The Windows build (RAD-131: `.dll` plugins and `.exe` tools, gcc in MSYS2
UCRT64, static GCC runtime, `.def` export files, DLL import checks with
`objdump -p`) is written but not yet run: the CI jobs `windows-ucrt64` in
`ci.yml`, `windows-check.yml` and the `windows-x64` release job hold the
steps. In a UCRT64 shell:

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,rust,binutils}
    tools/deps/build_deps.sh build/deps
    cmake --preset ci -DYANTRA_DEPS_PREFIX="$PWD/build/deps"
    cmake --build --preset ci
    ctest --test-dir build -LE slow
    python3 tools/ci/check_completeness.py --platform windows build/bin/plugins

The code is kept portable (the terminal plugin's serial ports and the
WinUSB/Zadig driver notes are the open Windows items).

## CI (RAD-134)

`.github/workflows/`: `ci.yml` (every push: lint incl. shell, YAML,
Python, FFI widths, reuse, cargo-deny, `verify_sources.py`; Linux build
and fast tests; Windows build; completeness checks incl. the clean
environment), `ci-full.yml` (nightly and before a release: opt-in and slow
suites, sanitizers, scalar tree, arm64), `sens-quick.yml` (sensitivity
sweep), `windows-check.yml` and `release.yml`. The workflows are complete
while the repository has no remote; `tools/ci/run_linux.sh` runs the same
Linux steps locally.

## Docs

`ninja -C build docs-radio-api`: the radio API reference (Doxygen) in
`build/docs/radio-api/html` (ctest `radio_api_doxygen` fails on any
Doxygen warning). Docs checks: `doc_snippets` (code in the tutorial is the
compiled example plugin), `doc_links` (relative links and anchors),
`plugin_docs` (the plugin pages' settings, presets, sensors and self-test
items, and the record schema tables, against the built plugins;
`ninja -C build update-plugin-docs` rewrites the generated parts).

## Release

The version is `project(YantraRadios VERSION ...)` in CMakeLists.txt, the
tag `v<version>` (annotated), the notes the CHANGELOG.md section of the
version (RAD-98). Before tagging: the full set of CONTRIBUTING.md (default
and scalar trees, opt-in suites, both sanitizer trees, reuse lint,
`verify_sources.py`), then

    ninja -C build release

which runs `tools/ci/make_release.sh` into `build/release/v<version>/`:

| File | Contents |
|---|---|
| `yantra-radios-v<version>-linux-x86_64.tar.gz` | the install tree: plugins (`lib/yantra-radios/plugins/*.so`), tools, scenarios, udev rules, docs and the licence bundle `share/licenses/yantra-radios` (LICENSE, LICENSES/, THIRD_PARTY.md, each bundled library's licence texts and copyright notices, BUNDLE.md) |
| `yantra-radios-v<version>-source.tar.gz` | the complete corresponding source (RAD-137): the repository at the tag with the pinned submodules (without osmo-tetra's `crypto/`) and `upstream/` with every release archive of `tools/deps/manifest.tsv` and its SHA256SUMS |
| `RELEASE_NOTES.md` | the CHANGELOG.md section |
| `SHA256SUMS` | SHA-256 of the files above |

The packaged plugins pass the completeness check before the folder is
written. `release.yml` does the same on a pushed tag for Linux (and, when
it runs, Windows) and publishes the files. Yantra pins the tagged commit
as its submodule and builds the plugins with these scripts, or takes the
release `.so` files of the tag (RAD-138).
