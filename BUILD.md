<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building Yantra

Yantra builds and is tested on Linux (x86-64; arm64 in the nightly CI).
The Windows build (MSYS2 UCRT64) is written down below but not yet tested:
DEFERRED (Windows). [ARCHITECTURE.md](ARCHITECTURE.md) explains the
layers and targets; [CONTRIBUTING.md](CONTRIBUTING.md) the rules for
changes.

## Dependencies (Linux)

The libraries are those of the specification's section 16.2. Ubuntu 24.04
package names (other distributions differ):

| Needed for | Packages |
|---|---|
| build | `cmake` (3.20+), `ninja-build`, `g++` (C++20; gcc 13+) or `clang`, `pkg-config`, `python3`, `binutils` |
| core | `libsqlite3-dev` (SQLite 3.37+, the project file, IND-01), `libzstd-dev` (zstd 1.4+, compressed recordings, REC-03), `libhpdf-dev` (libharu 2.3+, PDF reports, EXP-05) |
| desktop app (`YANTRA_BUILD_APP`, ON) | `libglfw3-dev` (GLFW 3.3+), `libgl-dev` (OpenGL 3), `libcurl4-openssl-dev` (libcurl 7.85+: MAK sheet download DEMO-12, sharing EXP-07) |
| radio plugins (yantra-radios) | `cargo` and `rustc` (its protocol crate); its bundled libraries (SoapySDR, osmo-tetra, libusb, ...) are built from source, no package needed |
| tests | `python3-yaml` (YAML lint), `reuse` (`pipx install reuse`), `xvfb` and `libgl1-mesa-dri` (app launch and UI tests on software OpenGL; skipped without Xvfb) |
| docs and screenshots | `doxygen` (plugin API reference), `curl` (API examples, DOC-21), `oxipng` (`cargo install oxipng`, screenshot size) |

Vendored in `third_party/` (no package): Dear ImGui, stb, nlohmann/json,
CivetWeb, miniaudio, doctest and the yantra-radios submodule
(THIRD_PARTY.md). PDFium is fetched at configure time (below).
`-DYANTRA_BUILD_APP=OFF` builds the core, tools and plugins without any
GUI library or libcurl.

## Default build

    git submodule update --init --recursive
    cmake --preset default            # or: cmake -S . -B build -G Ninja
    ninja -C build
    ctest --test-dir build -j"$(nproc)"

The build type is RelWithDebInfo unless one is given. Executables go to
`build/bin`, libraries and plugins to `build/lib`. In-source builds are
refused.

### PDFium (IND-02)

PDF floor plans use a pinned prebuilt PDFium (`cmake/YantraPdfium.cmake`,
THIRD_PARTY.md): release `chromium/8125` of
https://github.com/bblanchon/pdfium-binaries. The first configure
downloads `pdfium-linux-x64.tgz` (about 4 MB) into `build/_deps/` and
checks its SHA-256 against the pinned value; a wrong hash stops the
configure. Offline: download that archive once and pass
`-DYANTRA_PDFIUM_ARCHIVE=<file>` (the hash is checked the same way). With
`-DYANTRA_PDFIUM=OFF`, or when the archive cannot be had, Yantra builds
without PDF support and says so when a PDF plan is opened; the PDF tests
then check that message instead. Linux x86-64 only (other platforms:
DEFERRED).

### Radio plugins (PLG-26)

Yantra itself has no radio code: the radio plugins come from the
yantra-radios submodule (`third_party/yantra-radios`, pinned at a tagged
release). `cmake/YantraRadiosBuild.cmake` builds the submodule as a build
of its own (`build/radios/build`, without its tests, with the tree's
compiler, build type, `YANTRA_SIMD` and `YANTRA_SANITIZER`) and copies the
shipped plugins (hackrf, simulator, soapysdr, pei) into the build tree's
radio plugin folder `build/lib/yantra/plugins/radios/`. The radio API
example plugin (beacon) and the fake-SDR test plugin go to
`build/tests/radio-plugins/` for the tests only; they are never installed.
Yantra compiles no source of the submodule; only its `radio.h` header.

The first build also builds the radio plugins' bundled libraries
(yantra-radios `tools/deps/build_deps.sh`) once into
`build/radios-cache/deps-<submodule commit>`. That folder survives
reconfigures; a moved pin (another submodule commit checked out)
reconfigures by itself and builds a new cache entry. Sanitizer trees build
those libraries in their own radios build instead, instrumented.

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_BUILD_RADIOS` | `ON` | `OFF`: no radio plugins; the tests that need them skip. |
| `YANTRA_RADIOS_PREBUILT` | empty | Use a prebuilt yantra-radios package instead of building the submodule: an install tree, a release package (`.tar.gz`) or a release folder holding one. No beacon or soapysdr_fake test plugin then (their tests skip). |
| `YANTRA_RADIOS_CACHE_DIR` | `build/radios-cache` | Where the bundled libraries are cached; point several build trees at one folder to share it. |
| `YANTRA_RADIOS_FAKE_SDR` | `ON` | Also build yantra-radios' test plugin `soapysdr_fake` (about half a minute) for the unplug/replug test (NFR-04); never installed. |
| `YANTRA_RADIOS_PEI` | `ON` | `OFF`: the TETRA terminal plugin pei is not shipped (RAD-80). |

Example with a yantra-radios release built next to yantra:

    cmake -S . -B build -G Ninja \
      -DYANTRA_RADIOS_PREBUILT=../yantra-radios/build/release/v0.2.7

### Other options

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_BUILD_APP` | `ON` | The desktop application (GLFW, OpenGL, libcurl). |
| `YANTRA_BUILD_TESTS` | `ON` | The test suite under ctest. |
| `YANTRA_SIMD` | `ON` | SIMD ray-tracing kernels; `OFF`: the plain C++ fallback (TST-03). |
| `YANTRA_WERROR` | `OFF` | Warnings are errors (CI presets). |
| `YANTRA_PLUGIN_PROBE` | `ON` | The probe feature plugin (remote API, PLG-24); `OFF`: no network listener at all. |
| `YANTRA_SANITIZER` | empty | `asan-ubsan` or `tsan` (TST-05). |
| `YANTRA_FUZZ` | `OFF` | libFuzzer fuzz targets (clang, TST-12). |
| `YANTRA_SOURCE_URL` | the project's source link | Source link in About, the API status reply and packages (NFR-01). |

## Build trees

Each tree is a preset of `CMakePresets.json` (configure, build and test
presets of the same name). Never run two ctest runs in one tree at once.

| Tree | Configure | Test |
|---|---|---|
| default (`build/`) | `cmake --preset default` | `ctest --test-dir build -j"$(nproc)"` |
| scalar (`build-scalar/`) | `cmake --preset scalar` (`YANTRA_SIMD=OFF`, `YANTRA_BUILD_APP=OFF`) | `ctest --test-dir build-scalar -j"$(nproc)"` |
| ASan + UBSan (`build-asan/`) | `cmake --preset asan-ubsan` (Debug) | `ctest --preset asan-ubsan -j4` (= `-LE timing`) |
| TSan (`build-tsan/`) | `cmake --preset tsan` (Debug) | `ctest --preset tsan -j4` (= `-LE timing`) |
| plugins off (`build-plugins-off/`) | `cmake --preset plugins-off` | `ctest --test-dir build-plugins-off -j"$(nproc)"` |
| fuzz (any folder) | `CC=clang CXX=clang++ cmake -S . -B build-fuzz -G Ninja -DYANTRA_FUZZ=ON` | `ninja -C build-fuzz fuzz-all`, then run the targets |
| CI | `cmake --preset ci` / `ci-scalar` / `ci-plugins-off` | as the trees above, warnings are errors |

Notes:

- The scalar tree must give the same results as the default tree
  (golden files, TST-03).
- The sanitizer trees run 5-60 times slower; their hang guards scale with
  `YANTRA_TEST_TIME_SCALE` (set by the tree) and the real-time tests
  (label `timing`) are left out. They need a lot of memory: on a machine
  with 16 GB run them with `-j4`, one tree at a time.
- The plugins-off tree (`YANTRA_PLUGIN_PROBE=OFF`, `YANTRA_RADIOS_PEI=OFF`)
  is the PLG-01 check: the core, indoor and app suites pass without these
  plugins, and the ctest `plugins_off_check` makes sure neither is in the
  tree's plugin folders. Pass
  `-DYANTRA_RADIOS_CACHE_DIR=<the default tree>/radios-cache` to reuse the
  radio plugins' bundled libraries.

## Tests

Every test runs under ctest (TST-01); CONTRIBUTING.md says how to add
one. Labels:

| Label | Meaning |
|---|---|
| `fast` | the default |
| `slow` | long suites; left out of the push CI |
| `timing` | real-time checks; left out of the sanitizer trees; run them on an idle machine |
| `lint`, `docs` | repository lint, `reuse lint`, doc links, images, snippets, UI lint, Doxygen, doc examples (DOC-21: `doc_examples`, `doc_api_examples`) |
| `fuzz` | seeded fuzz replays (below) |
| `opt_in` | registered only when `YANTRA_TESTS_OPT_IN` is set when ctest runs |

    ctest --test-dir build -LE 'slow|timing' -j"$(nproc)"       # what CI runs on a push
    YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -L opt_in -j"$(nproc)"

## Fuzz targets (TST-12)

`tests/fuzz` has one fuzz target per parser of untrusted input: project
files, recordings, record files through the record sink into the
measurement pipeline, cell database CSV, calibration tables, buildings
with their scene export, plan sources (images and PDF), the export CSV
and DXF readers, command lines (API-01/02) and acceptance profiles. In
every tree, ctest (label `fuzz`) writes a seed corpus with
`yantra-fuzz-seeds` (Yantra's own writers and the repository's test data)
and replays it with seeded mutations; the sanitizer trees run the same
tests. A longer run by hand, e.g. 50000 mutations with another seed:

    build/fuzz/fuzz_command --mutations 50000 --seed 7 build/fuzz-corpus/command

`YANTRA_FUZZ_SAVE=<file>` keeps the input that is running, so after a
crash it holds the input; put it in `tests/fuzz/regressions/<target>/`
with the fix. With `-DYANTRA_FUZZ=ON` (clang) the targets are libFuzzer
binaries instead.

## Documentation and screenshots

- `ninja -C build docs-plugin-api`: the plugin API reference (Doxygen,
  DOC-17) in `build/docs/plugin-api/html`; the ctest `plugin_api_doxygen`
  fails on any Doxygen warning.
- `ninja -C build sample_report`: regenerates `docs/samples/` (the
  Fraunces demo report, DEMO-06); `sample_report_compare` checks it.
- `python3 tools/screenshots/screenshots.py --build build`: renders the
  screenshot set (SCR-01..04) into `docs/img` on a private Xvfb display
  and shrinks it with oxipng; `--only NAME` for some scenes, `--compare
  DIR` for the visual comparison (SCR-05).

## Build paths in binaries (NFR-09)

Every target is compiled with `-ffile-prefix-map=<source>=.` and
`-ffile-prefix-map=<build>=build` (top-level `CMakeLists.txt`), so the
debug information and `__FILE__` of the binaries name no folder of the
machine they were built on (a home folder is personal data). The ctest
`install_build_paths` installs into a DESTDIR in the build tree and fails
when an installed file holds the source folder, the build folder or the
builder's home folder. A debugger then needs the source folder:
`directory <source>` or `set substitute-path . <source>` in gdb. The
asan-ubsan tree is never installed (gcc's AddressSanitizer keeps full
source paths).

## Install

`ninja -C build install` (or `cmake --install build --prefix <p>`)
installs `bin/yantra`, `bin/yantra-probe`, `bin/yantra-plugin-test` with
the plugin headers and template, the plugins in `lib/yantra/plugins`
(radio plugins in `lib/yantra/plugins/radios`), PDFium in `lib/yantra`,
the desktop entry (`share/applications/yantra.desktop`), the logo as its
icon (`share/icons/hicolor/<size>/apps/yantra.png`, `share/yantra/logo`,
BRD-06), the fonts, branding, manual and demo projects in `share/yantra`,
the licence bundles in `share/licenses`, and yantra-radios' udev rules
and RTL-SDR blacklist in `lib/udev/rules.d` and `lib/modprobe.d`. The
ctest `install_destdir` (tests/install) installs into a DESTDIR in the
build tree and checks that layout (nothing outside the prefix, no test
plugin, the licence bundle complete, the installed plugins self-contained
and loadable). Sanitizer trees are never installed.

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

## CI

`.github/workflows/`: `ci.yml` (every push: lint and `reuse lint`, Linux
x86-64 build with warnings as errors, fast tests, sample report, timing
tests retried once), `nightly.yml` (default, scalar, plugins-off, asan,
tsan and arm64 trees with all tests including slow and opt-in),
`screenshots.yml` (the screenshot set as an artifact, SCR-04) and
`windows.yml` (MSYS2 UCRT64, written but not run: DEFERRED (Windows)).

## Windows (MSYS2 UCRT64) - not yet tested, DEFERRED (Windows)

These steps are how the Windows build is meant to run; they have not been
run yet. The code keeps platform parts behind small interfaces
(`core/src/platform`). In a UCRT64 shell:

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,python,rust} \
              mingw-w64-ucrt-x86_64-{sqlite3,zstd,libharu,glfw,curl}
    git submodule update --init --recursive
    cmake --preset default
    cmake --build build
    ctest --test-dir build -LE 'slow|timing'

Open Windows items: the radio plugins as `.dll` (yantra-radios RAD-131),
a PDFium Windows archive, thread names (SetThreadDescription), the
installer (NSIS) and the CI job in `windows.yml`.
