<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Building Yantra

Yantra builds and is tested on Linux (x86-64; arm64 in the nightly CI)
and on Windows x64, cross-compiled on Linux and tested on Windows (section
"Windows"). [ARCHITECTURE.md](ARCHITECTURE.md) explains the
layers and targets; [CONTRIBUTING.md](CONTRIBUTING.md) the rules for
changes.

## Dependencies (Linux)

The libraries are those of the specification's section 16.2. Ubuntu 24.04
package names (other distributions differ):

| Needed for | Packages |
|---|---|
| build | `cmake` (3.20+), `ninja-build`, `pkg-config`, `python3`, `binutils`; the compiler is clang from the cross SDK (below); `g++` (C++20; gcc 13+) for the second tree (`gcc`) |
| core | `libsqlite3-dev` (SQLite 3.37+, the project file, IND-01), `libzstd-dev` (zstd 1.4+, compressed recordings, REC-03), `libhpdf-dev` (libharu 2.3+, PDF reports, EXP-05) |
| desktop app (`YANTRA_BUILD_APP`, ON) | `libglfw3-dev` (GLFW 3.3+), `libgl-dev` (OpenGL 3), `libcurl4-openssl-dev` (libcurl 7.85+: MAK sheet download DEMO-12, sharing EXP-07) |
| radio plugins (yantra-radios) | its protocol crate is built with the SDK's `cargo`/`rustc` (the gcc tree: the system's `cargo` and `rustc`); its bundled libraries (SoapySDR, osmo-tetra, libusb, ...) are built from source, no package needed |
| tests | `python3-yaml` (YAML lint), `reuse` (`pipx install reuse`), `xvfb` and `libgl1-mesa-dri` (app launch and UI tests on software OpenGL; skipped without Xvfb) |
| docs and screenshots | `doxygen` (plugin API reference), `pandoc` and `weasyprint` (`ninja docs`: HTML and PDF, DOC-20), `curl` (API examples, DOC-21), `oxipng` (`cargo install oxipng`, screenshot size), `ffmpeg` (program only: the animated captures, SCR-06) |
| packaging | `dpkg-deb` (package `dpkg`, the .deb, NFR-09) |

Vendored in `third_party/` (no package): Dear ImGui, stb, nlohmann/json,
CivetWeb, miniaudio, doctest and the yantra-radios submodule
(THIRD_PARTY.md). PDFium is fetched at configure time (below).
`-DYANTRA_BUILD_APP=OFF` builds the core, tools and plugins without any
GUI library or libcurl.

## Compiler: clang from the cross SDK (OA-02)

Every tree except `gcc` is built with clang from ONE pinned LLVM release,
the cross SDK of yantra-radios (RAD-139), used through the submodule: the
presets set `CMAKE_TOOLCHAIN_FILE` to
`third_party/yantra-radios/tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake`
(clang, lld, llvm-ar; the system's glibc and libstdc++; the SDK's rustc
and cargo for the radio plugins' crate). The SDK is downloaded and
checked from the submodule's lock files into a git-ignored folder; nothing
is installed in the system:

    python3 third_party/yantra-radios/tools/sdk/yantra_sdk.py make     # or: check
    # it lands in third_party/yantra-radios/sdk; one SDK can serve several
    # checkouts (e.g. that of yantra-radios itself):
    export YANTRA_SDK_DIR=<folder of the SDK>

The toolchain file finds the SDK in `-DYANTRA_SDK_DIR`, then
`$YANTRA_SDK_DIR` (the presets pass it on and keep it in the cache), then
`third_party/yantra-radios/sdk`, and refuses an SDK made from other lock
files. The radio plugins and their bundled libraries are built with the
same toolchain. gcc stays as a second Linux tree (preset `gcc`, below).
Every tree builds without warnings.

## Default build

    git submodule update --init --recursive
    cmake --preset default            # clang from the SDK (above)
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
compiler (toolchain file or compilers), build type, `YANTRA_SIMD` and `YANTRA_SANITIZER`) and copies the
shipped plugins (hackrf, simulator, soapysdr, pei) into the build tree's
radio plugin folder `build/lib/yantra/plugins/radios/`. The radio API
example plugin (beacon) and the fake-SDR test plugin go to
`build/tests/radio-plugins/` for the tests only; they are never installed.
Yantra compiles no source of the submodule; only its `radio.h` header.

The first build also builds the radio plugins' bundled libraries
(yantra-radios `tools/deps/build_deps.sh`) once into
`build/radios-cache/deps-<submodule commit>-<compiler>-<version>`. That folder survives
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
      -DYANTRA_RADIOS_PREBUILT=../yantra-radios/build/release/v0.3.0

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
| ASan + UBSan (`build-asan/`) | `cmake --preset asan-ubsan` (Debug) | `ctest --preset asan-ubsan -j"$(nproc)"` (= `-LE timing`) |
| TSan (`build-tsan/`) | `cmake --preset tsan` (Debug) | `ctest --preset tsan -j"$(nproc)"` (= `-LE timing`) |
| plugins off (`build-plugins-off/`) | `cmake --preset plugins-off` | `ctest --test-dir build-plugins-off -j"$(nproc)"` |
| gcc (`build-gcc/`) | `cmake --preset gcc` (the system's gcc, second tree) | `ctest --test-dir build-gcc -j"$(nproc)"` |
| fuzz (`build-fuzz/`) | `cmake --preset fuzz` (clang + ASan/UBSan, `YANTRA_FUZZ=ON`) | `ninja -C build-fuzz fuzz-all`, then run the targets |
| CI | `cmake --preset ci` / `ci-scalar` / `ci-plugins-off` / `ci-gcc` | as the trees above, warnings are errors |

Notes:

- The scalar tree must give the same results as the default tree
  (golden files, TST-03).
- All trees but `gcc` use clang from the SDK (above); the `gcc` tree uses
  the system's gcc and must pass the same tests.
- The sanitizer trees run 5-60 times slower; their hang guards scale with
  `YANTRA_TEST_TIME_SCALE` (set by the tree) and the real-time tests
  (label `timing`) are left out. They need a lot of memory: on a machine
  with 16 GB run them with `-j4`, one tree at a time (32 GB: `-j"$(nproc)"`).
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
- `ninja -C build docs` (DOC-20): every Markdown page to HTML with
  pandoc, the user manual on one page (`docs/manual/manual.html` and
  `.pdf`), the quick starts in every language and the method and legal
  notes also to PDF with WeasyPrint (`pandoc`, `weasyprint`), the plugin
  API reference in `docs/plugin-api`, all in `build/docs/site`; then
  every relative link, anchor and picture is checked offline
  (`tools/docs/build_docs.sh`; web links are listed in
  `external-links.txt`, never fetched). The ctest `docs_build` does the
  same into its own folder (skipped without pandoc).
- `ninja -C build sample_report`: regenerates `docs/samples/` (the
  Fraunces demo report, DEMO-06); `sample_report_compare` checks it.
- `python3 tools/screenshots/screenshots.py --build build`: renders the
  screenshot set (SCR-01..04) into `docs/img` on a private Xvfb display
  and shrinks it with oxipng; `--only NAME` for some scenes, `--compare
  DIR` for the visual comparison (SCR-05).
- `ninja -C build captures`: the animated capture of the indoor workflow
  (SCR-06), `docs/img/indoor_workflow.gif` (about 550 kB) and `.webm`
  (about 230 kB), 8 s each, from the app's screenshot-mode frames joined
  by ffmpeg (`tools/screenshots/captures.py`).

## Build paths in binaries (NFR-09)

Every target is compiled with `-ffile-prefix-map=<source>=.`,
`-ffile-prefix-map=<build>=build` and, with the cross SDK,
`-ffile-prefix-map=<YANTRA_SDK_DIR>=sdk` (top-level `CMakeLists.txt`), so the
debug information and `__FILE__` of the binaries name no folder of the
machine they were built on (a home folder is personal data). The ctest
`install_build_paths` installs into a DESTDIR in the build tree and fails
when an installed file holds the source folder, the build folder or the
builder's home folder. A debugger then needs the source folder:
`directory <source>` or `set substitute-path . <source>` in gdb. The
asan-ubsan tree is never installed (AddressSanitizer may keep full
source paths).

## Install

`ninja -C build install` (or `cmake --install build --prefix <p>`)
installs `bin/yantra`, `bin/yantra-probe`, `bin/yantra-plugin-test` with
the plugin headers and template, the plugins in `lib/yantra/plugins`
(radio plugins in `lib/yantra/plugins/radios`), PDFium in `lib/yantra`,
the desktop entry (`share/applications/yantra.desktop`), the logo as its
icon (`share/icons/hicolor/<size>/apps/yantra.png`, `share/yantra/logo`,
BRD-06), the fonts, branding, manual, quick starts, method and legal
notes and demo projects in `share/yantra`, the changelog and, when
`ninja docs` ran first, the built documentation with the manual PDF
(DOC-02) in `share/doc/yantra/html`,
the licence bundles in `share/licenses`, and yantra-radios' udev rules
and RTL-SDR blacklist in `lib/udev/rules.d` and `lib/modprobe.d`. The
ctest `install_destdir` (tests/install) installs into a DESTDIR in the
build tree and checks that layout (nothing outside the prefix, no test
plugin, the licence bundle complete, the installed plugins self-contained
and loadable). Sanitizer trees are never installed.

## Linux package (NFR-09)

`ninja -C build deb` builds the documentation (`ninja docs`) and then
`build/package/yantra_<version>-1_<arch>.deb` with Yantra's own script
`packaging/deb/build_deb.sh` (dpkg-deb with root-owned files, no sudo;
nothing is installed on the build machine): the install layout above
below `/usr` (stripped), the built documentation with the manual PDF in
`/usr/share/doc/yantra/html`, a DEP-5 `copyright` file and the licence
texts in `/usr/share/doc/yantra`, and maintainer scripts that reload the
udev rules. The radio plugins are self-contained, so the package needs no
SoapySDR or vendor package; the other dependencies come from
dpkg-shlibdeps. Package metadata: maintainer `Curtat BV
<info@curtat.be>`, homepage https://curtat.be, `Vcs-Browser`/`Vcs-Git`
and the copyright `Source` from the build setting `YANTRA_SOURCE_URL`
(default https://github.com/peterdewitte/yantra; the package script
refuses an empty one). The installed programs find PDFium in
`/usr/lib/yantra` through their run path `$ORIGIN/../lib/yantra`.

`packaging/deb/check_deb.sh DEB SOURCE_URL` checks a package without
installing it (control fields, contents, icons 16..512 equal to
`data/logo/png`, no test plugin, root ownership, md5sums, the unpacked
app finding its PDFium, no build-machine folder, home folder or git
identity of the builder, no MAK plan); the ctest `package_deb` builds the
package from the default tree with the documentation of `docs_build` and
runs that check (offline). The Windows zip and NSIS installer follow in
step 13.9 of the build (section "Windows").

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

`.github/workflows/`: `ci.yml` (every push: lint, `reuse lint` and the
workflow files parsed with PyYAML; the Linux x86-64 build with clang from
the cross SDK (cached by its lock files, with the radio plugins' bundled
libraries) and warnings as errors, fast tests, sample report, timing tests
retried once; a second job with gcc (`ci-gcc`); the docs job builds `ninja
docs` and keeps the site as an artifact, DOC-20), `nightly.yml` (default,
scalar, plugins-off, asan and tsan trees with clang from the SDK, the gcc
tree, and arm64 with gcc (the SDK is x86-64), with all tests including slow
and opt-in), `screenshots.yml` (the screenshot set as an artifact, SCR-04)
and `windows.yml` (native MSYS2 UCRT64 with gcc, the second Windows check;
written but not run). The workflows are YAML-linted locally; they have
never run (no remote).

## Windows (OA-02, HW-06)

Windows x64 (UCRT) is cross-compiled on Linux with clang from the cross
SDK of the yantra-radios submodule (RAD-139) and Yantra's own lock of
MSYS2 UCRT64 packages, `tools/windows/msys2.lock` (GLFW, SQLite, zstd,
libcurl with Windows' own TLS, libharu and what they need; Mesa for the
tests only). Make the SDK with both locks once (downloads checked by
SHA-256 and signature, nothing installed in the system):

    export YANTRA_SDK_DIR=~/yantra-sdk
    python3 third_party/yantra-radios/tools/sdk/yantra_sdk.py make --msys2-lock tools/windows/msys2.lock

To check an SDK made that way, give the same lock (it checks the
radios' locks and Yantra's packages):

    python3 third_party/yantra-radios/tools/sdk/yantra_sdk.py check --msys2-lock tools/windows/msys2.lock

(`python3 tools/windows/update_msys2_lock.py` pins today's packages; then
make the SDK again.) PDFium comes from its pinned Windows release
(`pdfium-win-x64.tgz`, cmake/YantraPdfium.cmake). The Windows tree takes
its build-time data (the demo projects, the TST-10 record files) from a
Linux tree of the same sources, `YANTRA_HOST_BUILD_DIR` (default `build`),
so build that first:

    cmake --preset default && ninja -C build
    cmake --preset windows && ninja -C build-win     # ci-windows: warnings are errors

`build-win/bin` gets the programs, the tests and the DLLs they import
(`tools/windows/copy_dlls.py`, list in `bin/yantra-dlls.txt`), plus Mesa's
`opengl32.dll` (llvmpipe) so the tests render off-screen on a machine
without OpenGL 3 (the test VM's display adapter); the packages (step 13.9)
use the system's OpenGL. The radio plugins are the submodule's
self-contained `.dll` in `lib/yantra/plugins/radios`. User folders on
Windows: `%APPDATA%/Yantra` (settings, data, calibration table, plugins in
`%APPDATA%/Yantra/plugins`), `%LOCALAPPDATA%/Yantra` (caches).

The tree's ctest runs on Windows (no emulator). Checks of the sources and
of the build machine's tools (lints, Doxygen, docs, header checks) carry
the label `build_host`: they skip on Windows and run on Linux with

    ctest --test-dir build-win -L build_host

The Windows test VM (the owner's onair-wintest VirtualBox VM; `status`
first, it must be powered off) runs the rest; the tests start programs
that are not on their command lines (the radio tools, the app), so push
every program with `--all`:

    ~/onair/OnAir.Claude/.claude/skills/onair-wintest/wintest.sh status
    ~/onair/OnAir.Claude/.claude/skills/onair-wintest/wintest.sh test ~/yantra/build-win --all -LE timing
    ~/onair/OnAir.Claude/.claude/skills/onair-wintest/wintest.sh test ~/yantra/build-win --all -L timing -j 1

Results land in `build-win/wintest-results/<time>/` (junit.xml,
LastTest.log). What the tree needs on the test machine:

- The build tree finds the sources through `yantra-source-dir.txt` (CMake
  writes it; CMakeCache.txt is not copied), so the app started from
  `build-win/bin` finds `data/` and the demo project.
- Paths compiled into the tests name `/home/...`: Windows reads them on
  the current drive (`C:`), where the VM mirrors the trees. Folders from
  environment variables without a drive (`APPDATA=/home/...`) are on the
  current drive too (`platform::complete_path`).
- The app tests set `GALLIUM_DRIVER=llvmpipe`: Mesa's first choice, its
  d3d12 driver, ends the program (exit code 0x80070057) where there is no
  Direct3D 12 device. A screenshot renders into an off-screen frame
  buffer of the requested size (the VM's screen is 1024 x 768, and
  Windows keeps a window within the screen).
- Programs are started without a shell (`platform::run_program`, tests:
  `yantra::test::run_program`); files compared byte for byte are written
  in binary mode (text mode writes CRLF on Windows).
- The demo projects of the Windows tree come from the Linux tree; the
  Windows C runtime's math functions round differently in the last bit,
  so DEMO-06/DEMO-13 "made again" compare two runs made in the VM there.
- Timing (`-L timing -j 1`): `bench_interpolation` (source search) and
  `bench_pipeline` exceed their limits, which were set on Linux. In the
  VM the runtime's malloc, `std::mutex` (winpthreads) and math functions
  cost 2 to 10 times what they cost on Linux; per-platform limits are not
  decided yet. Tests that check Linux-only things are registered as
skipped with the reason (label `skipped`): the X11/Wayland/WSL launch
checks (`app_launch_win32` and `app_screenshot_start` check the Windows
start and an off-screen screenshot), the plugin template build (needs the
compiler and the kit on one machine). The crash cases of the project file
and the recordings (a writer killed with SIGKILL, fork) and PDFium's
operator new check are compiled out on Windows. File names with
characters outside the system code page are not yet checked on Windows.

The native MSYS2 UCRT64 build (gcc, `windows.yml` in CI) stays a second
check; MSYS2 is not installed on the test machine, so it has not been run
locally. In a UCRT64 shell:

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,python,rust} \
              mingw-w64-ucrt-x86_64-{sqlite3,zstd,libharu,glfw,curl-winssl}
    git submodule update --init --recursive
    cmake --preset gcc
    cmake --build build-gcc
    ctest --test-dir build-gcc -LE 'slow|timing'
