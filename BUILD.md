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
| radio plugins (`plugins/`, `radio-common/`) | their protocol crate is built with the SDK's `cargo`/`rustc` (the gcc tree: the system's `cargo` and `rustc`); their bundled libraries (SoapySDR, osmo-tetra, libusb, ...) are built from source, no package needed |
| tests | `python3-yaml` (YAML lint), `reuse` (`pipx install reuse`), optional `bubblewrap` (the radio plugins' clean-environment run), `xvfb` and `libgl1-mesa-dri` (app launch and UI tests on software OpenGL; skipped without Xvfb) |
| docs and screenshots | `doxygen` (plugin API reference), `pandoc` and `weasyprint` (`ninja docs`: HTML and PDF, DOC-20), `curl` (API examples, DOC-21), `oxipng` (`cargo install oxipng`, screenshot size), `ffmpeg` (program only: the animated captures, SCR-06) |
| packaging | `dpkg-deb` (package `dpkg`, the .deb, NFR-09) |

Vendored in `third_party/` (no package): Dear ImGui, stb, nlohmann/json,
CivetWeb, miniaudio and doctest, and the radio plugins' libraries
(osmo-tetra as a submodule, SoapySDR and its modules, libusb, ...;
THIRD_PARTY.md). PDFium is fetched at configure time (below).
`-DYANTRA_BUILD_APP=OFF` builds the core, tools and plugins without any
GUI library or libcurl.

## Compiler: clang from the cross SDK (OA-02)

Every tree except `gcc` is built with clang from ONE pinned LLVM release,
the cross SDK in `tools/sdk` (RAD-139): the
presets set `CMAKE_TOOLCHAIN_FILE` to
`tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake`
(clang, lld, llvm-ar; the system's glibc and libstdc++; the SDK's rustc
and cargo for the radio plugins' crate). The SDK is downloaded and
checked from its lock files into a git-ignored folder; nothing
is installed in the system:

    python3 tools/sdk/yantra_sdk.py make     # or: check
    # it lands in sdk/; one SDK can serve several checkouts:
    export YANTRA_SDK_DIR=<folder of the SDK>

The toolchain file finds the SDK in `-DYANTRA_SDK_DIR`, then
`$YANTRA_SDK_DIR` (the presets pass it on and keep it in the cache), then
`sdk/` (a folder or a link to one), and refuses an SDK made from other lock
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

### Radio plugins (PLG-29)

The application has no radio code. The radio parts (formerly
yantra-radios, merged with their history, their releases tagged
`radios-v*`) are folders of the same CMake project:

| Folder | What |
|---|---|
| `radio-api/` | the radio API header `yantra/radio.h` (`yantra::radio_api`, the only part the application compiles) and its Doxygen reference |
| `plugins/` | every plugin in its own folder: the radio plugins `hackrf`, `soapy` (soapysdr), `pei`, `simulator`, the radio API example `example` (beacon), and Yantra's plugins `tetra`, `probe`, `beacon` |
| `radio-common/` | the radio plugins' common code: `radios_common`, `receiver` (DSP, the TETRA decoder with the Rust protocol crate), `tetra_common`, `soapy_common` |
| `gen/`, `tools/yantra-gen` | the TETRA test-signal generator |
| `tools/yantra-radio-host`, `tools/yantra-hackrf`, `tools/yantra-pei-sim`, `tools/yantra-sens` | the radio test host with the conformance kit (RAD-91), the HackRF tool, the simulated terminal, the sensitivity sweep |
| `tools/deps/`, `tools/sdk/` | the bundled libraries' build (RAD-136) and the cross SDK (RAD-139) |
| `tests/radio/` | the radio parts' tests |

One configure, one `ninja`, one `ctest` (tests labelled by area: `app`,
`core`, `radio-api`, `plugin:<name>`, `radio-common`, `gen`, `docs`,
`lint`, `packaging`). The shipped radio plugins (hackrf, simulator,
soapysdr, pei) are built into `build/lib/yantra/plugins/radios/`; the
example plugin beacon and the fake-SDR test plugins go to
`build/tests/radio-plugins/` for the tests only and are never installed.
The radio plugins' bundled libraries are built in the tree from
`third_party/` (section [Radio parts](#radio-parts-rad-141)), so the
sanitizer trees instrument them too.

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_BUILD_RADIOS` | `ON` | `OFF`: no radio parts; the tests that need them skip. |
| `YANTRA_RADIO_PARTS_ONLY` | `OFF` | `ON`: the radio parts alone, without the application and its libraries (RAD-141; preset `radio-parts`), with their own install layout and `release` target. |
| `YANTRA_RADIOS_TESTS` | `ON` | The radio parts' tests (with `YANTRA_BUILD_TESTS`). |
| `YANTRA_RADIOS_PEI` | `ON` | `OFF`: the TETRA terminal plugin pei is not built (RAD-80). |
| `YANTRA_DEPS_PREFIX` | empty | Take the bundled libraries from a prefix made by `tools/deps/build_deps.sh` instead of building them in the tree. |
| `YANTRA_RADIOS_SOURCE_URL` | the public repository | Source URL in the radio plugin descriptors (RAD-12, RAD-137). |

### Other options

| Option | Default | Meaning |
|---|---|---|
| `YANTRA_BUILD_APP` | `ON` | The desktop application (GLFW, OpenGL, libcurl). |
| `YANTRA_BUILD_TESTS` | `ON` | The test suite under ctest. |
| `YANTRA_SIMD` | `ON` | SIMD kernels (the interpolation's distance kernel); `OFF`: the plain C++ fallback (TST-03). |
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
| radio parts only (`build-radios/`) | `cmake --preset radio-parts` (`YANTRA_RADIO_PARTS_ONLY=ON`, RAD-141) | `ctest --test-dir build-radios -j"$(nproc)"` |
| gcc (`build-gcc/`) | `cmake --preset gcc` (the system's gcc, second tree) | `ctest --test-dir build-gcc -j"$(nproc)"` |
| fuzz (`build-fuzz/`) | `cmake --preset fuzz` (clang + ASan/UBSan, `YANTRA_FUZZ=ON`) | `ninja -C build-fuzz fuzz-all`, then run the targets |
| CI | `cmake --preset ci` / `ci-scalar` / `ci-radio-parts` / `ci-plugins-off` / `ci-gcc` | as the trees above, warnings are errors |

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
  tree's plugin folders.

## Tests

Every test runs under ctest (TST-01); CONTRIBUTING.md says how to add
one. Labels:

| Label | Meaning |
|---|---|
| `fast` | the default |
| `slow` | long suites; left out of the push CI |
| `timing` | real-time checks; left out of the sanitizer trees; run them on an idle machine |
| `app`, `core`, `radio-api`, `plugin:<name>`, `radio-common`, `gen`, `packaging` | the area a test belongs to (`ctest -L plugin:hackrf`) |
| `lint`, `docs` | repository lint, `reuse lint`, doc links, images, snippets, UI lint, Doxygen, doc examples (DOC-21: `doc_examples`, `doc_api_examples`) |
| `fuzz` | seeded fuzz replays (below) |
| `opt_in` | registered only when `YANTRA_TESTS_OPT_IN` is set when ctest runs |

    ctest --test-dir build -LE 'slow|timing' -j"$(nproc)"       # what CI runs on a push
    YANTRA_TESTS_OPT_IN=1 ctest --test-dir build -L opt_in -j"$(nproc)"

## Fuzz targets (TST-12)

`tests/fuzz` has one fuzz target per parser of untrusted input: project
files, recordings, record files through the record sink into the
measurement pipeline, cell database CSV, calibration tables, buildings,
plan sources (images and PDF), the export CSV and DXF readers, command lines (API-01/02) and acceptance profiles. In
every tree, ctest (label `fuzz`) writes a seed corpus with
`yantra-fuzz-seeds` (Yantra's own writers and the repository's test data)
and replays it with seeded mutations; the sanitizer trees run the same
tests. A longer run by hand, e.g. 50000 mutations with another seed:

    build/fuzz/fuzz_command --mutations 50000 --seed 7 build/fuzz-corpus/command

The radio parts' parsers (IQ and SigMF files, PEI lines, TETRA bursts
and PDUs, scenarios, scenes, record files) have their own targets in
`tests/radio/fuzz` (RAD-96; seeds `yantra-radio-fuzz-seeds`, corpus
`build/fuzz-corpus-radio`, [docs/FUZZING.md](docs/FUZZING.md)).
`YANTRA_FUZZ_SAVE=<file>` keeps the input that is running, so after a
crash it holds the input; put it in `tests/fuzz/regressions/<target>/`
with the fix. With `-DYANTRA_FUZZ=ON` (clang) the targets are libFuzzer
binaries instead.

## Documentation and screenshots

- `ninja -C build docs-plugin-api`: the plugin API reference (Doxygen,
  DOC-17) in `build/docs/plugin-api/html`; the ctest `plugin_api_doxygen`
  fails on any Doxygen warning.
- `ninja -C build docs-manual` (DOC-02a): the PDF user manual alone,
  `build/docs/manual/yantra-manual-<version>.pdf`, made from
  `docs/manual/NN-*.md` by `tools/docs/build_manual.py` (pandoc writes one
  HTML page, WeasyPrint lays it out with `tools/docs/manual.css`): A4,
  title page with the Yantra logo, version, build date (the last commit,
  or `SOURCE_DATE_EPOCH`) and licence, contents with page numbers, the
  manual's own section numbers, bookmarks for every chapter and section,
  internal links for the contents and the section references "(13.2)",
  the light-theme screenshots as numbered figures, table headers repeated
  across pages, running chapter titles and page numbers, an index
  (`tools/docs/manual_index.txt`: main terms, windows and menus), Inter and
  JetBrains Mono embedded from `data/fonts`, a tagged PDF (PDF/UA-1) with
  metadata. English only (DOC-06). Its checks (DOC-20) fail the build: no
  pandoc or WeasyPrint warning, every picture present and laid out, every
  internal link and bookmark resolving, bookmarks equal to the headings,
  every chapter in the layout, every index term found, no text past the
  page margin or out of a table cell, every font embedded, metadata and
  page count. The ctest `docs_manual_faults` checks that the checker finds
  a fixture's faults.
- `ninja -C build docs` (DOC-20): every Markdown page to HTML with
  pandoc, the user manual on one page (`docs/manual/manual.html`) and
  as the PDF above, the quick starts in every language and the method and legal
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
the licence bundles in `share/licenses`, the generator's scenarios in
`share/yantra-radios/scenarios`, and the radio parts' udev rules
and RTL-SDR blacklist in `lib/udev/rules.d` and `lib/modprobe.d`. The
ctest `install_destdir` (tests/install) installs into a DESTDIR in the
build tree and checks that layout (nothing outside the prefix, no test
plugin, the licence bundle complete, the installed plugins self-contained
and loadable). Sanitizer trees are never installed.

On Windows (the cross-compiled tree, `cmake --install build-win --prefix
<folder>`) the layout is the same but for: PDFium and the DLLs the
programs and plugins import are installed next to the programs in `bin`
(`tools/windows/copy_dlls.py`; Mesa never), so are the radio tools
`yantra-hackrf.exe` and `yantra-radio-host.exe`; no desktop entry, udev
rules or hicolor icons (the icon is in `yantra.exe`).

## Linux package (NFR-09)

`ninja -C build deb` builds the documentation (`ninja docs`) and then
`build/package/yantra_<version>-1_<arch>.deb` with Yantra's own script
`packaging/deb/build_deb.sh` (dpkg-deb with root-owned files, no sudo;
nothing is installed on the build machine): the install layout above
below `/usr` (stripped), the PDF manual
`/usr/share/doc/yantra/yantra-manual-<version>.pdf` (Help > User manual
(PDF)), the built documentation in `/usr/share/doc/yantra/html`, the
TETRA test-signal generator `/usr/bin/yantra-gen` with its scenario files
in `/usr/share/yantra-radios/scenarios`, a DEP-5 `copyright` file and the licence
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
runs that check (offline). The Windows packages: section
[Windows packages](#windows-packages-nfr-09).

## Plugin folders (PLG-09)

Yantra loads plugins from two folders, each with a `radios` subfolder for
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
and opt-in), `screenshots.yml` (the screenshot set as an artifact, SCR-04),
`windows.yml` (native MSYS2 UCRT64 with gcc, the second Windows check;
written but not run), `sens-quick.yml` (the radio parts' sensitivity
sweep, on demand), the radio plugins' completeness checks and
conformance kit in the Linux job and again in a bare container
(`completeness-clean`, RAD-133), the radio parts alone (`radio-parts`,
RAD-141, `tools/ci/run_linux.sh`), and the Windows jobs of `ci.yml` in the reusable
`windows-cross.yml`: `windows-cross` (the Linux tree for the build-time
data, the Windows tree with clang from the SDK and Yantra's MSYS2 lock,
warnings as errors, the `build_host` tests, `ninja package-windows`; the
packages and the tree as artifacts) and `windows-x64-tests` on a Windows
runner (the tree's ctest natively, with the package tests). The runner's
build tools and the cross SDK (made from the lock files, cached by them)
are one composite action, `.github/actions/linux-sdk`.

`packages.yml` (TST-64; on a tag `v*`, by hand, and on pull requests that
touch `packaging/`, `tools/windows/`, the presets, the radio parts or
these workflows): the Linux x86-64 tree with clang from the SDK, its fast
and timing tests, `ninja deb` and the `package_deb` test (artifact
`linux-x86_64-packages`: the .deb and the PDF manual), and
`windows-cross.yml` (artifact `windows-x86_64-packages`: the setup .exe,
the portable zip, SHA256SUMS; the Windows tests on a Windows runner). On a
tag that names the project's version it publishes a GitHub release with
these files, SHA256SUMS over all of them and the CHANGELOG section of that
version as notes. The same packages locally:

    cmake --preset default && ninja -C build && ctest --test-dir build -LE 'slow|timing'
    ninja -C build deb                          # build/package/yantra_<version>-1_amd64.deb
    cmake --preset windows && ninja -C build-win
    ninja -C build-win package-windows          # build-win/package: zip, setup .exe, SHA256SUMS
    ctest --test-dir build-win -L build_host    # among them the packages' DLL import check

(`cmake -B <dir>` with a preset puts a tree elsewhere; a Windows tree
whose Linux tree is not `build` names it with
`-DYANTRA_HOST_BUILD_DIR=<dir>`.) The workflows are YAML-linted locally
(`tools/ci/lint.sh`, ctest `repo_lint`); they have
never run (no remote).

## Windows (OA-02, HW-06)

Windows x64 (UCRT) is cross-compiled on Linux with clang from the cross
SDK in `tools/sdk` (RAD-139) and Yantra's own lock of
MSYS2 UCRT64 packages, `tools/windows/msys2.lock` (GLFW, SQLite, zstd,
libcurl with Windows' own TLS, libharu and what they need; Mesa for the
tests only). Make the SDK with both locks once (downloads checked by
SHA-256 and signature, nothing installed in the system):

    export YANTRA_SDK_DIR=~/yantra-sdk
    python3 tools/sdk/yantra_sdk.py make --msys2-lock tools/windows/msys2.lock

To check an SDK made that way, give the same lock (it checks the
SDK's locks and Yantra's packages):

    python3 tools/sdk/yantra_sdk.py check --msys2-lock tools/windows/msys2.lock

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
without OpenGL 3 (the test VM's display adapter); the packages never ship
Mesa and use the system's OpenGL. `yantra.exe` carries the Yantra icon
(`packaging/windows/make_ico.py` makes `build-win/packaging/yantra.ico`
from the PNGs of `data/logo/png` as supplied, BRD-06) and version
information (`packaging/windows/version.rc.in`, llvm-rc); it is a
windowed program (no console window) that prints to the console it was
started from. The radio plugins are self-contained `.dll` in
`lib/yantra/plugins/radios`, linked with `-static` (RAD-132): they import
only Windows system DLLs, while the application's programs import the
MSYS2 DLLs next to them. User folders on
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

    W=~/onair/OnAir.Claude/.claude/skills/onair-wintest/wintest.sh
    ninja -C build-win package-windows      # the package tests need a current package
    $W status
    $W test ~/yantra/build-win --all -LE timing -E test_app_overflow
    $W test ~/yantra/build-win --all -j 3 -R test_app_overflow
    $W test ~/yantra/build-win --all -L timing -j 1

Each `test` restores the VM's clean snapshot, copies the tree, runs ctest
in the guest and powers the VM off. The GUI overflow test (TST-52,
`test_app_overflow`, 10 heavy cases) runs on its own with at most 3 jobs:
at 12 jobs together with the rest it exhausted the VM's memory. While the
VM runs, keep builds and tests on the host to 8 jobs (`-j8`). A run that
ends with "VM stopped while waiting for ssh (state gurumeditation)" left
the VM stuck: `VBoxManage controlvm onair-wintest poweroff`, wait about
90 s, run again (never `up` right after `down`). WSL2 is not used.

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

## Radio parts: cross SDK and Windows

### Cross SDK (RAD-139)

One folder with everything the clang trees and the Windows cross build
need, made from pinned downloads only (no sudo, nothing installed in the
system, no MSYS2 or Windows needed; about 1.2 GB of downloads, 1.8 GB
unpacked):

| Folder | What | Lock file |
|---|---|---|
| `llvm/` | one LLVM release: clang, clang++, lld, llvm-ar/ranlib, llvm-rc, llvm-objdump, llvm-strip, llvm-symbolizer and more, compiler-rt (sanitizers, libFuzzer) | `tools/sdk/llvm.lock` |
| `sysroots/x86_64-w64-mingw32/` | the Windows x64 (UCRT) target sysroot: MSYS2 UCRT64 binary packages (mingw-w64 headers, CRT and import libraries, winpthreads, the GCC runtime with libstdc++ and libgcc), only unpacked | `tools/sdk/msys2.lock` |
| `rust/` | rustc and cargo with the standard libraries of x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu | `tools/sdk/rust.lock` |
| `manifest/` | the lock files it was made from and every unpacked file with its SHA-256 | |

    tools/sdk/yantra_sdk.py make     # download (cached), check, unpack
    tools/sdk/yantra_sdk.py check    # every lock file against what is unpacked

- Location: `sdk/` in the repository (git-ignored), or `$YANTRA_SDK_DIR`
  (or `--sdk-dir`; CMake: `-DYANTRA_SDK_DIR`); the downloads are cached in
  `sdk-cache/` or `$YANTRA_SDK_CACHE` (`--cache`), so a re-run downloads
  nothing. Every path inside the SDK is relative: the folder can be moved
  or shared (`YANTRA_SDK_DIR`; `--msys2-lock <file>` adds the MSYS2 packages of
  another lock file to the sysroot).
- Outside the source tree (1.8 GB SDK, 1.4 GB cache; a tool that copies
  the tree, such as the Windows test VM's runner below, then leaves them
  out): either `YANTRA_SDK_DIR`/`YANTRA_SDK_CACHE` (and `-DYANTRA_SDK_DIR`
  for every tree), or links in place of the folders, which every tree and
  script then uses unchanged:

      mv sdk ~/yantra-sdk && ln -s ~/yantra-sdk sdk
      mv sdk-cache ~/yantra-sdk-cache && ln -s ~/yantra-sdk-cache sdk-cache

  Compiled files never name the SDK's folder: its paths (the given and
  the real one) are mapped to `sdk/` in debug information
  (`-ffile-prefix-map`, `tools/deps/deps.cmake`).
- Checks: each download's SHA-256 against the lock file and its
  publisher's signature with a private, temporary gpg home: LLVM's `.sig`
  with llvm.org's release keys (the signer's fingerprint is in
  `llvm.lock`), Rust's `.asc` with the Rust release key, every MSYS2
  package's `.sig` with the keys of the `msys2-keyring` package pinned in
  `msys2.lock`. `check` fails on any difference between the lock files and
  the SDK (ctest `sdk_check`); the toolchain files refuse an SDK made from
  other lock files.
- New versions: `tools/sdk/yantra_sdk.py update --llvm latest` (the
  newest stable LLVM release with a signed Linux x86-64 archive),
  `--msys2` (today's packages), `--rust` (the version of the `rustc` on
  PATH); then `make`. MSYS2 removes old package versions after a while:
  keep `sdk-cache/` to remake an older SDK.
- Rust: the Windows `rust-std` loads only into the official `rustc` build
  of the same version (a distribution's own `rustc` build is refused,
  E0514), so the SDK carries the official `rustc` and `cargo` of the
  system's version, and the toolchain files make the build use them
  (`YANTRA_CARGO_EXECUTABLE`, `RUSTC`) for every target and test. The
  system Rust stays as it is (the gcc trees use it).
- Toolchain files (`-DCMAKE_TOOLCHAIN_FILE=`; the SDK folder from
  `-DYANTRA_SDK_DIR`, `$YANTRA_SDK_DIR` or `sdk/`):
  - `tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake`: Linux with the
    SDK's clang and lld, the system's glibc and libstdc++;
  - `tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake`: Windows x64,
    target `x86_64-w64-windows-gnu`, `--sysroot` the SDK's sysroot, lld,
    libstdc++, `llvm-rc` for resources, the GCC runtime linked statically
    (`YANTRA_WIN_STATIC_RUNTIME`, ON) and winpthreads added to every link
    (`tools/sdk/mingw-clang-rules.cmake`: clang's MinGW driver does not
    link it, MSYS2's gcc does).
- On WSL2 (optional), Windows programs run on the PC's Windows through
  Windows interop: `tools/sdk/wsl_run.sh [--with <dll-folder>]
  <program.exe> [args]` copies the program into a scratch folder under
  the Windows `%TEMP%`, runs it and deletes the folder; with `--in-place`
  it runs the program where it is (the optional ctest runner of the
  Windows tree, below); `--check` exits 77 without interop. ctest
  `sdk_windows_smoke` (label slow; skipped without SDK or WSL interop)
  cross-compiles a C++20 program with threads, iostream, std::filesystem,
  exceptions, a version resource and a Rust static library, checks with
  `llvm-objdump -p` that it imports only Windows system DLLs and the UCRT,
  and runs it that way.

### The radio parts on Windows (RAD-131)

The Windows tree is cross-compiled on Linux with the cross SDK (above),
the radio parts with it:
`.dll` plugins, `yantra-radio-host.exe`, `yantra-gen.exe`,
`yantra-hackrf.exe` and every test program, the bundled libraries built
statically from the same sources (RAD-136), the Rust code with the SDK's
`cargo` for `x86_64-pc-windows-gnu`. Its ctest runs natively on a
Windows machine, the Windows test VM:

No emulator: every
test command is a Windows program (`.exe`), `cmake` or `python3` (no shell
scripts), so the tree's `CTestTestfile.cmake` runs as it is on Windows.

How the cases are listed: the build cannot run a Windows program, so it
writes one ctest per doctest group (`<group>/all`); ctest on Windows runs
each `.exe --list-test-cases` when it reads the tests
(`cmake/doctest_discover.py --at-test-time`) and registers one ctest per
case, as in the Linux trees (`ctest -N` on Linux shows the groups only).

Build-machine checks (label `build_host`): the tests that check the build
machine or run its tools (`sdk_check`, `sdk_windows_smoke`, `deps_build*`,
`deps_manifest`, `repo_lint`, `reuse_lint`, `radio_api_doxygen`,
`radio_api_header_*`, `rust_cargo_test`, `release_package`,
`radios_manual`, `radios_manual_faults`, `shipped_paths`) run through
`cmake/build_host_test.py`: skipped (77) on Windows, run on Linux with
`ctest --test-dir build-win -L build_host`. `fuzz_proto_rust` is skipped
in this tree (its cargo runs on the build machine, its corpus comes from
the Windows `yantra-radio-fuzz-seeds.exe`; the Linux trees run it).

On WSL2 the tree can run its ctest on the PC's Windows instead
(optional): `cmake --preset windows -DYANTRA_WINDOWS_TEST_RUNNER=wsl`
makes `tools/sdk/wsl_run.sh --in-place` the emulator
(`CMAKE_CROSSCOMPILING_EMULATOR`): ctest runs on Linux, each `.exe` where
it was built (`\\wsl.localhost\<distribution>`), with PATH holding only
the Windows system folders and the `YANTRA_*`/`SOAPY_*` variables of its
ctest (through `WSLENV`); the cases are listed at build time. Without WSL
interop the option is ignored with a warning. The CI's Linux job runs
only the checks of the tree (`ctest -L 'completeness|release'`); its
Windows runner runs the programs with `tools/ci/run_windows_tests.py`.

What runs on Windows: every doctest program, the CLI tests of the three
tools, the conformance kit on every plugin `.dll` (beacon, simulator,
soapysdr through its fake module, hackrf through its fake, pei through
the simulated terminal on a named pipe), and the checks:

- `completeness_<plugin>`, `completeness_plugins`: imports only Windows
  system DLLs and the UCRT (no `libstdc++-6.dll`, `libgcc_s_seh-1.dll`,
  `libwinpthread-1.dll`, nothing of MSYS2, no `msvcrt.dll`), exports only
  `yantra_radio_entry` (the `.def` file), no build folder inside
  (`tools/ci/check_completeness.py --platform windows`, which reads the
  PE import and export tables itself, so it runs on Windows too, or with
  `--objdump` from GNU or llvm-objdump output; `--build-folder` names the
  build machine's folders when it runs on another one);
- `completeness_programs` (every `.exe`: imports) and
  `completeness_programs_shipped` (the three tools: also no build folder);
- `completeness_negative` (a plugin importing a non-system DLL and
  exporting everything is caught), `completeness_build_paths`;
- `release_package`: the `.zip` package (below; a build-machine check).

Skipped on Windows (reported as skipped, with the reason, by
`yantra_skip_test()`): `plugin_docs` (it drives the Linux tree's `.so`
files; the pages are checked in the Linux trees), `clean_env_conformance`
(a bubblewrap sandbox; the CI's Windows runner is the clean machine),
`install_destdir_radios` (the Linux FHS layout with udev rules),
`completeness_surprises` (static TLS and GNU unique symbols are ELF
properties).

The terminal plugin on Windows (RAD-80): `test_pei` and `conformance_pei`
run in the VM against the simulated terminal `yantra-pei-sim.exe` on a
named pipe (`\\.\pipe\yantra-pei-*`), which the plugin opens through its
Win32 serial-port code (CreateFileW, overlapped ReadFile/WriteFile, timed
waits; on a pipe without the DCB of a COM port); `test_pei_port.cpp`
checks the port layer itself (COM names, USB ids of SetupAPI instance
ids, listing, paths that are not serial ports, in-use, unplug/replug). A
real COM port is a hardware check ([docs/DEVICES.md](docs/DEVICES.md)).

Golden files: `tests/radio/gen_sim/golden/windows` and
`tests/radio/yantra_gen/golden/windows` hold the Windows digests. The UCRT's
math functions round some results differently in the last bit from
glibc's (in `tests/radio/gen_sim`: the `floor_change` and `weak_signal` truth
texts; the `tests/radio/yantra_gen` digests equal the Linux ones). The UCRT
also picks CPU-dependent code paths (FMA3) unless told not to, so
`yantra-gen` and every test main call `_set_FMA3_enable(0)` first
(`gen/include/yantra/gen/portable_math.hpp`): the results no longer depend
on the Windows machine. Same seeds and scenarios; the Windows files come
from a run in the test VM: a golden check that fails prints the computed
text, which `LastTest.log` brings back (`--update` in the guest writes
into the guest's copy only; the `update-golden-*` targets run the test
programs on the build machine, so on a Linux build server they work for
the Linux trees only).

Portability lives in a few small interfaces: plugin loading
(`tools/yantra-radio-host/dynlib.cpp`, `LoadLibraryExW` with the plugin's
own folder first), the test helpers `tests/radio/test_platform.hpp`
(environment variables, starting a program without a shell, shell
commands for the `--*-cmd` options, a loopback UDP receiver) and the
GSMTAP socket (Winsock started on first use).

Pitfalls:

- Upstream libraries mark their API `dllexport` on Windows whatever is
  defined (SoapySDR's `Config.h`, `hackrf.h`, `airspy.h`, SoapyRemote).
  SoapySDR is compiled against a copy of its `Config.h` without
  `SOAPY_SDR_DLL` (upstream's static case), and the export directives
  (`.drectve`) are removed from every bundled archive after it is built
  (`llvm-objcopy --remove-section=.drectve`, `tools/deps/sdr.cmake`);
  otherwise a plugin exports hundreds of symbols next to its `.def` entry.
- SoapyRemote 0.5.3 writes its Windows code for MSVC only (`_MSC_VER`);
  four of its files are compiled from copies in the build tree with
  `_MSC_VER` read as `_WIN32` (`tools/deps/sdr.cmake`).
- A plugin is linked with `-static` only; a `-Wl,-Bdynamic` after it
  makes the driver import `libstdc++-6.dll` again.
- `std::system` runs `cmd.exe`, which refuses a network-share current
  folder (`\\wsl.localhost`): tests start programs with
  `yantra::test::run_program()` and give `cmd.exe` absolute native paths.
  `cmd.exe /c` strips the first and last quote of a line, so
  `yantra-radio-host conformance --unplug/--replug` encloses the command
  once more on Windows; the conformance tests use
  `"<cmake>" -E touch/rm -f "<file>"`, the same on Linux and Windows.
- UCRT math functions may round differently from one Windows machine to
  another (CPU-dependent code paths); see the golden files above.
- libstdc++'s `lexically_normal()` shortens a UNC path's leading `\\`.
- Paths written into JSON text use `generic_string()` (backslashes would
  be escapes).
- Never two heavy jobs at once: a Windows ctest run through WSL interop
  uses all cores of the PC as well.

The bundled libraries as a prefix for Windows:

    CMAKE_TOOLCHAIN_FILE=$PWD/tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake \
        tools/deps/build_deps.sh build-win-deps
    cmake --preset windows -DYANTRA_DEPS_PREFIX="$PWD/build-win-deps"

Natively on Windows, in an MSYS2 UCRT64 shell (gcc; `windows-check.yml`,
not run here):

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,python,rust,binutils}
    tools/deps/build_deps.sh build/deps
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DYANTRA_DEPS_PREFIX="$PWD/build/deps"
    cmake --build build
    ctest --test-dir build -LE slow

Open Windows items: the terminal plugin's serial ports (COM ports) and
its fake terminal, and the WinUSB/Zadig driver notes.

## Windows packages (NFR-09)

The portable zip and the NSIS setup .exe are made on Linux from the
Windows tree with Yantra's own script
`packaging/windows/package_windows.py` (makensis from NSIS; nothing is
installed on the build machine):

    ninja -C build-win package-windows

writes into `build-win/package/`:

- `yantra-<version>-windows-x86_64.zip`: one folder with `bin\` (the
  programs `yantra.exe`, `yantra-probe.exe`, `yantra-plugin-test.exe`, the
  radio tools `yantra-hackrf.exe`, `yantra-radio-host.exe` and
  `yantra-gen.exe` (its scenarios in `share\yantra-radios\scenarios`), PDFium and
  the MSYS2 UCRT64 DLLs they import), `lib\yantra\plugins` (TETRA, probe
  and the self-contained radio plugins, PLG-26),
  `share\yantra` (data, fonts with their OFL texts, logo, the Fraunces
  demo, the in-app manual, quick starts, THIRD_PARTY.md), the PDF manual
  `share\doc\yantra\yantra-manual-<version>.pdf` (DOC-02a; the target
  builds it first, `docs-manual`), `include\`
  (the plugin headers), the licence texts (`LICENSE.txt`,
  `share\doc\yantra\LICENSES`, `share\licenses\{yantra-radios,pdfium,msys2}`
  with `msys2\PACKAGES.txt`), `README.txt` and `SOURCE.txt` (the
  corresponding source of the GPL, AGPL and LGPL parts, from
  `YANTRA_SOURCE_URL`; the script refuses an empty one). Yantra's own
  programs and plugins are stripped of debug information. The zip is
  reproducible (sorted, dated `SOURCE_DATE_EPOCH` or the last commit).
- `yantra-<version>-windows-x86_64-setup.exe` (`packaging/windows/yantra.nsi`):
  the same files for the current user (no administrator rights) in
  `%LOCALAPPDATA%\Programs\Yantra`, a Start menu entry, the entry in
  Settings > Apps (publisher Curtat BV, homepage https://curtat.be, the
  source URL) and an uninstaller that removes exactly the installed
  files; silent: `setup.exe /S [/D=<folder>]`, `uninstall.exe /S`.
- `SHA256SUMS`.

Mesa is a test tool and never in a package; a machine without an OpenGL
3.3 driver cannot show the app. Checks:

- `package_windows_check` (label `build_host`, run on Linux with
  `ctest --test-dir build-win -L build_host`): the DLL import check of
  `packaging/windows/check_windows_package.py` on the zip (every `.exe`
  and `.dll` imports only DLLs in the package or Windows system DLLs and
  the UCRT; no Mesa or other system DLL, no unused DLL, no test program),
  the contents and licence texts, the icon resource, no MAK plan
  (DEMO-12), no build-machine folder or builder identity, a package newer
  than the tree's programs; then a self-test with faults it must find.
- On Windows (the test VM, after `ninja -C build-win package-windows`):

      ~/onair/OnAir.Claude/.claude/skills/onair-wintest/wintest.sh test ~/yantra/build-win -R package_windows

  `package_windows_zip` unpacks the zip and `package_windows_setup`
  installs the setup .exe silently into a scratch folder
  (`packaging/windows/smoke_test.py`, Python only); both start the
  packaged `yantra.exe --version`, `yantra-probe.exe` (every plugin
  loaded from the package), `yantra-radio-host.exe info`, `yantra-gen.exe
  --list` (the package's own scenarios), check the PDF manual and run an
  off-screen `yantra.exe --demo --screenshot` (with the test tree's Mesa
  copied next to it for that run only, as the VM has no OpenGL 3 driver);
  the setup test checks the uninstall entry and the Start menu shortcut
  and uninstalls silently. They are part of the full VM run (`--all`).

The native MSYS2 UCRT64 build (gcc, `windows.yml` in CI) stays a second
check; MSYS2 is not installed on the test machine, so it has not been run
locally. In a UCRT64 shell:

    pacman -S git mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,python,rust} \
              mingw-w64-ucrt-x86_64-{sqlite3,zstd,libharu,glfw,curl-winssl}
    git submodule update --init --recursive
    cmake --preset gcc
    cmake --build build-gcc
    ctest --test-dir build-gcc -LE 'slow|timing'

## Radio parts (RAD-141)

The radio parts' build details (RAD-01..05, RAD-131..139). They are
built with the application; `-DYANTRA_RADIO_PARTS_ONLY=ON` (preset
`radio-parts`, tree `build-radios`) builds them alone, without the
application's libraries. `tools/ci/run_linux.sh [build-dir]` runs the CI
steps of that build (static dependency build, warnings as errors, fast
tests, completeness, lint); `YANTRA_CI_COMPILER=gcc` for gcc.

### Bundled libraries, built statically (RAD-132, RAD-136)

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

### Completeness checks (RAD-133)

- `tools/ci/check_completeness.py <dir or .so>`: the NEEDED entries hold
  only the allowed system libraries (glibc parts, libudev), no RPATH, no
  text relocations or executable stack, and only `yantra_radio_entry` is
  exported (version script `cmake/radio_plugin.map`). The file also names
  no folder of the machine that built it (this source tree, the builder's
  home folder): `tools/deps/deps.cmake` and `cmake/RustProto.cmake` make
  source paths relative with `-ffile-prefix-map` and `--remap-path-prefix`,
  so a debugger needs `directory <source tree>`. `package_release.sh` looks
  for the same folders in every file of the package (not in an `asan-ubsan`
  tree, never shipped: AddressSanitizer keeps the full paths). ctests
  `completeness_<plugin>` and `completeness_plugins`.
- `tools/ci/check_shipped_paths.py` (ctest `shipped_paths`, every tree but
  the sanitizer ones; a build-machine check in the Windows trees):
  `cmake --install` into a scratch prefix, then no installed file holds
  the source tree, the build tree, the cross SDK (as given and resolved)
  or the home folder, and no program or library the configured install
  folders (`<prefix>/share/yantra-radios`, `<prefix>/lib/yantra-radios`):
  programs find their data relative to themselves (`yantra-gen`:
  `/proc/self/exe`, `GetModuleFileNameW`), never through a folder of the
  build machine.
- `tools/ci/clean_env.py`: the conformance kit on the plugins in an
  environment without SoapySDR or vendor packages (bubblewrap locally,
  ctest `clean_env_conformance`; a bare ubuntu:24.04 container in CI with
  `--sandbox none`).

### Install of the radio parts alone (RAD-99)

In the radio-parts-only tree, `cmake --install build-radios --prefix /usr`
(or any prefix; `DESTDIR` works): plugins to `lib/yantra-radios/plugins`, tools to `bin`, scenarios to
`share/yantra-radios`, udev rules to `lib/udev/rules.d`, the RTL-SDR DVB-T
blacklist to `lib/modprobe.d`, the licence bundle to
`share/licenses/yantra-radios`, docs to `share/doc/yantra-radios`.
Nothing is written to `/etc` and nothing runs udevadm: for a prefix
other than `/usr`, copy the udev rules to `/etc/udev/rules.d` and reload
udev as root. ctest `install_destdir_radios` checks the result. The
application's build installs the radio plugins into its own layout
(section [Install](#install)).

### Docs

`ninja -C build docs-radio-api`: the radio API reference (Doxygen) in
`build/docs/radio-api/html` (ctest `radio_api_doxygen` fails on any
Doxygen warning). Docs checks: `doc_snippets_radio` (code in docs/RADIO_PLUGINS.md is the
compiled example plugin),
`plugin_docs` (the plugin pages' settings, presets, sensors and self-test
items, and the record schema tables, against the built plugins;
`ninja -C build update-plugin-docs` rewrites the generated parts).

`ninja -C build docs-radios-manual`: the radio plugins' PDF manual
`build/docs/radios-manual/yantra-radios-manual-<version>.pdf` (RAD-97) from the
plugin pages, docs/TETRA_RECORDS.md, docs/RADIO_PLUGINS.md and
docs/radio-manual/overview.md (`tools/docs/build_radio_manual.py`: pandoc, then
WeasyPrint; `apt install pandoc weasyprint fonts-dejavu-core`). The build
fails on a pandoc or WeasyPrint warning, a missing picture, an internal
link or bookmark that does not resolve, text past the page margin or out
of a table cell, or a font that is not embedded. ctest `radios_manual`
builds and checks it, `radios_manual_faults` checks the checker (label
`docs`; Skipped without pandoc or WeasyPrint; on the build machine in the
Windows trees). The build date is `SOURCE_DATE_EPOCH`, else the last
commit's date.

### Release of the radio parts alone

The version is the product version in the file `VERSION` (PLG-29), the
same for the application, the plugins and the packages; the tag
`v<version>` (annotated), the notes the CHANGELOG.md section of the
version, for a pre-release such as `0.3.0-dev` the Unreleased section
(RAD-98, `tools/ci/release_notes.py`). Before tagging: the full set of CONTRIBUTING.md (default
and scalar trees, opt-in suites, both sanitizer trees, reuse lint,
`verify_sources.py`), then

    ninja -C build-radios release        # the radio-parts-only tree

which runs `tools/ci/make_release.sh` into `build-radios/release/v<version>/`:

| File | Contents |
|---|---|
| `yantra-radios-manual-<version>.pdf` | the PDF manual (RAD-97; the `release` target builds it first) |
| `yantra-radios-v<version>-linux-x86_64.tar.gz` | the install tree: plugins (`lib/yantra-radios/plugins/*.so`), tools, scenarios, udev rules, docs (with the PDF manual in `share/doc/yantra-radios/`) and the licence bundle `share/licenses/yantra-radios` (LICENSE, LICENSES/, THIRD_PARTY.md, each bundled library's licence texts and copyright notices, BUNDLE.md) |
| `yantra-radios-v<version>-windows-x86_64.zip` | the Windows tree's install tree (a radio-parts-only Windows tree, or `tools/ci/package_release.sh build-win <out> <tag> windows-x86_64`): `lib/yantra-radios/plugins/*.dll`, `bin/*.exe`, scenarios, docs with the PDF manual and the same licence bundle |
| `yantra-radios-v<version>-source.tar.gz` | the complete corresponding source (RAD-137): the repository at the tag with the pinned submodules (without osmo-tetra's `crypto/`) and `upstream/` with every release archive of `tools/deps/manifest.tsv` and its SHA256SUMS |
| `RELEASE_NOTES.md` | the CHANGELOG.md section |
| `SHA256SUMS` | SHA-256 of the files above |

The packaged plugins pass the completeness check before the folder is
written. The application's packages (.deb, setup .exe) are made by
`packages.yml` (section [CI](#ci)).

## Making a release

A release is an annotated tag `v<version>` on a commit whose `VERSION`
file (the one product version, PLG-29: application, plugins, packages and
the radio-parts release; a development state has a pre-release suffix
such as `0.3.0-dev`, the .deb writes it `0.3.0~dev`) and CHANGELOG entry
carry that version and date, with the release check passing (`YANTRA_RELEASE_CHECK=1`: the TST-52
exceptions list must be empty). The artefacts are made from that commit:

    cmake --preset default && ninja -C build && ctest --test-dir build -j"$(nproc)"
    ninja -C build deb                    # build/package/yantra_<version>-1_amd64.deb
    cmake --preset windows && ninja -C build-win
    ninja -C build-win package-windows    # zip, setup .exe, SHA256SUMS in build-win/package
    ctest --test-dir build-win -L build_host
    # the Windows test VM: the three runs of section "Windows"

then `sha256sum` over the .deb, the zip, the setup .exe and the PDF manual
(`build/docs/manual/yantra-manual-<version>.pdf`, `ninja -C build
docs-manual`, its own release artefact) and `git tag -a v<version>`.
Nothing is pushed by the build; the artefacts stay in the build trees.

