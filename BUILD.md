<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building Yantra

Linux only for now; Windows builds are planned (not yet built).

## Default build

    git submodule update --init --recursive
    cmake -S . -B build -G Ninja      # or: cmake --preset default
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

Needed: CMake 3.20+, Ninja, a C++20 compiler (GCC or Clang), SQLite 3.37+
with its headers (`libsqlite3-dev`; the project file, IND-01), zstd 1.4+
with its headers and pkg-config (`libzstd-dev`, `pkg-config`; compressed
recordings, REC-03), Python 3
with PyYAML, binutils, and `reuse` for the licence lint test.

PDF floor plans (IND-02) use a pinned prebuilt PDFium (THIRD_PARTY.md).
The first configure downloads it (about 4 MB) into `build/_deps/` and
checks its SHA-256. Offline: download `pdfium-linux-x64.tgz` of release
`chromium/8125` from https://github.com/bblanchon/pdfium-binaries once
and pass `-DYANTRA_PDFIUM_ARCHIVE=<file>` (the hash is checked the same
way). Without it, or with `-DYANTRA_PDFIUM=OFF`, Yantra builds without
PDF support and says so when a PDF plan is opened; the PDF tests then
check that message instead.

## Radio plugins (PLG-26)

Yantra itself has no radio code: the radio plugins come from the
yantra-radios submodule (`third_party/yantra-radios`). The default build
builds them from the submodule in a build of its own
(`build/radios/build`, without its tests) and puts the shipped plugins
(hackrf, simulator, soapysdr, pei) in the build tree's application radio
plugin folder `build/lib/yantra/plugins/radios/`. The radio API example
plugin (beacon) goes to `build/tests/radio-plugins/` for the tests only;
it is not installed.

The first build also builds the radio plugins' bundled libraries
(yantra-radios `tools/deps/build_deps.sh`) once into
`build/radios-cache/deps-<submodule commit>`. That folder survives
reconfigures and is only rebuilt when the submodule commit changes.
Sanitizer trees (`YANTRA_SANITIZER`) build those libraries in their own
radios build instead, instrumented. The scalar tree (`YANTRA_SIMD=OFF`)
builds the radio plugins scalar as well.

Options:

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_BUILD_RADIOS` | `ON` | `OFF`: no radio plugins; the tests that need them skip. |
| `YANTRA_RADIOS_PREBUILT` | empty | Use a prebuilt yantra-radios package instead of building the submodule: an install tree, a release package (`.tar.gz`) or a release folder holding one. No beacon or soapysdr_fake test plugin then (their tests skip). |
| `YANTRA_RADIOS_CACHE_DIR` | `build/radios-cache` | Where the bundled libraries are cached; point several build trees at one folder to share it. |
| `YANTRA_RADIOS_FAKE_SDR` | `ON` | Also build yantra-radios' test plugin `soapysdr_fake` (the generic SoapySDR plugin with its fake SoapySDR module, about half a minute) into `build/tests/radio-plugins` for the unplug/replug test (NFR-04); never installed. |

Example with a yantra-radios release built next to yantra:

    cmake -S . -B build -G Ninja \
      -DYANTRA_RADIOS_PREBUILT=$HOME/yantra-radios/build/release/v0.2.1

`ninja -C build install` installs `bin/yantra`, the radio plugins in
`lib/yantra/plugins/radios`, yantra-radios' licence bundle in
`share/licenses/yantra-radios` and its udev rules and RTL-SDR blacklist in
`lib/udev/rules.d` and `lib/modprobe.d` (packages: later). The ctest
`install_destdir` (tests/install) installs into a DESTDIR in the build tree
and checks that layout (nothing outside the prefix, no test plugin, the
licence bundle complete, the installed plugins self-contained and
loadable).

## Plugin folders (PLG-09)

Yantra loads plugins from two folders, each with a `radios/` subfolder for
the radio plugins:

- application: `<executable folder>/../lib/yantra/plugins` (in the build
  tree `build/lib/yantra/plugins`);
- user: `$XDG_DATA_HOME/yantra/plugins`, default
  `~/.local/share/yantra/plugins`.

For tests and development, `YANTRA_USER_PLUGIN_DIR` replaces the user
folder and `YANTRA_PLUGIN_PATH` (`:`-separated) adds folders searched
first. Tests always set these and never use the real home folder.

## Plugin development (PLG-13, PLG-15)

The build also makes the example technology plugin
(`build/lib/yantra/examples/example_beacon.so`, not installed) and the
plugin test kit `build/bin/yantra-plugin-test`; `ninja -C build install`
installs the kit, the plugin headers and the out-of-tree plugin template
(`share/yantra/plugin-template`). See [docs/PLUGINS.md](docs/PLUGINS.md).

## Other trees

| Tree | Configure |
|---|---|
| scalar | `cmake -S . -B build-scalar -G Ninja -DYANTRA_SIMD=OFF -DYANTRA_BUILD_APP=OFF` |
| ASan + UBSan | `cmake --preset asan-ubsan` (`build-asan`) |
| TSan | `cmake --preset tsan` (`build-tsan`); run `ctest -LE timing` |
