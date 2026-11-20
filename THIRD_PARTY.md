<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Third-party components and data

Yantra itself is licensed under GPL-3.0-or-later (see `LICENSE`). This file
lists every third-party library, tool, font and data set that Yantra uses or
ships, with its licence, its source and how Yantra uses it (NFR-01, DOC-14).
Per-file licence information is machine-readable through SPDX headers and
`REUSE.toml`; the licence texts are in `LICENSES/`.

Rules for adding an entry:

- Only the libraries and tools of spec section 16.2 are used.
- Each entry is checked against the component's own LICENSE file when it
  is added, and its licence text goes into `LICENSES/` under its SPDX
  identifier.
- Vendored code lives under `third_party/<name>/` at a pinned release
  fetched from upstream, with the version or commit stated here.
- Data that may not be redistributed or adapted is never put in the
  repository (for example the MAK floor plans of DEMO-12; the app downloads
  them on first use).

## Libraries

Each entry was checked against the component's own licence file (DOC-14
review in release 0.1.0). "Vendored" = the files are in this repository
under `third_party/`; "fetched" = configure downloads a pinned, hash-checked
archive into the build tree; "system" = a library of the operating system,
found at configure time and linked dynamically.

| Component | Version | Licence | Source | Used for |
|---|---|---|---|---|
| nlohmann/json | 3.12.0 (single header, vendored unchanged in `third_party/nlohmann_json/`, SHA-256 in its README) | MIT | https://github.com/nlohmann/json | All of Yantra's JSON: the building model and scene export (`yantra-building`, RAD-36), record files of yantra-radios (RAD-65, TST-10), recordings in JSON Lines (REC-03), the calibration table, settings, configuration profiles, plugin state, acceptance profiles, demo descriptions, the remote API and its WebSocket messages (API-01/02), the plugin test kit's reports and golden files (PLG-13); header-only, compiled into Yantra |
| CivetWeb | 1.16 (release tarball `v1.16.tar.gz`, sha256 `f0e471c1bf4e7804a6cfb41ea9d13e7d623b2bcc7bc1e2a4dd54951a24d60285`; only `civetweb.c`, `civetweb.h`, the `.inl` files it includes and its `LICENSE.md`, vendored in `third_party/civetweb/`, unchanged but for one marked line in `STOP_FLAG_ASSIGN` that reads the stop flag atomically) | MIT; its `md5.inl` Zlib, its `sha1.inl` public domain | https://github.com/civetweb/civetweb (tag `v1.16`) | HTTP + WebSocket server of the remote API (API-02, spec 16.2) in the probe feature plugin `probe.so` (PLG-24), built with `NO_CGI`, `NO_FILES`, `NO_CACHING`, `USE_WEBSOCKET`, `USE_IPV6`, `STOP_FLAG_NEEDS_LOCK` and `NO_SSL` (no Lua, Duktape, HTTP/2, zlib, file serving or TLS; HTTPS through a reverse proxy, docs/API.md) |
| SQLite | 3.37 or newer, system library (`libsqlite3-dev`; tested with 3.46.1), linked into the core | Public domain (blessing) | https://sqlite.org | The indoor project file `*.yantra` (IND-01, IND-03, IND-04; docs/formats/PROJECT.md), which also holds the cell database (DB-01), the walk tests and measured points, stored interpolation maps and acceptance profiles |
| zstd | 1.4 or newer, system library (`libzstd-dev`, found with pkg-config; tested with 1.5.7), linked into the core | BSD-3-Clause (or GPL-2.0-only, dual) | https://facebook.github.io/zstd/ | Optional compression of recordings `*.yrec.zst` (REC-03; docs/formats/RECORDING.md) |
| libharu | 2.3.0 or newer, system library (`libhpdf-dev`; tested with 2.3.0; no pkg-config file), linked into the core; it brings zlib (Zlib) and libpng (libpng-2.0) as its own dependencies | Zlib (zlib/libpng licence) | https://github.com/libharu/libharu | The PDF coverage and acceptance reports (EXP-05, spec 16.2; `core/src/report/pdf.cpp`): A4 pages, the TrueType fonts embedded as subsets through its UTF-8 encoder, RGB images, vector charts and link annotations. Its ToUnicode map of UTF-8 fonts (2.3.0) is replaced by Yantra's own (docs/REPORT.md) |
| doctest | 2.4.12 (single header, vendored unchanged in `third_party/doctest/`, SHA-256 in its README) | MIT | https://github.com/doctest/doctest | C++ unit tests under ctest (TST-01); tests only, never shipped |
| stb_image, stb_image_write | stb_image 2.30, stb_image_write 1.16 (stb at commit d14d46f), vendored unchanged in `third_party/stb/` (SHA-256 in its README) | MIT OR Unlicense (public domain), the author's choice (its `LICENSE` and the end of each header) | https://github.com/nothings/stb | Floor plan images (IND-02): every format stb_image reads (PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR, PIC), decoded from memory in `core/src/plan/` (static library `yantra_stb_image`). stb_image_write writes the exported floor images, PNG and JPEG (EXP-08), in `core/src/export/stb_export.c` with static linkage, and the image fixtures of the tests. |
| miniaudio | 0.11.25, release tag `0.11.25` of https://github.com/mackron/miniaudio (release tarball SHA-256 `b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274`), the single header `miniaudio.h` and its `LICENSE` vendored unchanged in `third_party/miniaudio/` | Unlicense OR MIT-0 (public domain or MIT No Attribution, the user's choice; its `LICENSE`) | https://github.com/mackron/miniaudio | Sound in the desktop app (spec 16.2): voice-note recording and playback (REC-06), later the audible events (UI-09, UI-10); built into the `yantra` executable only (`app/src/miniaudio_impl.c`); the audio back-ends (PulseAudio, ALSA, JACK) are loaded at run time, so there is no build dependency |
| Dear ImGui (with its GLFW and OpenGL 3 backends) | 1.92.9b, release tag `v1.92.9b` of https://github.com/ocornut/imgui, vendored unchanged in `third_party/imgui/` (the core files, `imstb_*.h`, `LICENSE.txt`, `backends/imgui_impl_{glfw,opengl3}.*` with `imgui_impl_opengl3_loader.h`; SHA-256 in its README) | MIT (its `LICENSE.txt`); `imstb_*.h` MIT OR Unlicense (stb); the OpenGL loader generated by gl3w: public domain (Unlicense) | https://github.com/ocornut/imgui | The desktop app's UI (UI-13, OA-17), `app/`: built into the `yantra` executable and the headless UI tests only, never into the core or the plugins. Its `imstb_truetype.h` (stb_truetype 1.26) also draws the text of the exported floor images in the core (`core/src/export/stb_export.c`, static linkage, unchanged) |
| GLFW | 3.3 or newer, system library (`libglfw3-dev`; tested with 3.4.0) | Zlib | https://www.glfw.org | Window, OpenGL context, input and window icon of the desktop app (`app/src/main.cpp`); X11 or Wayland (NFR-10) |
| libcurl | 7.85 or newer, system library (`libcurl4-openssl-dev`; tested with 8.18.0) | curl (MIT-style, `curl` licence) | https://curl.se/libcurl/ | Uses only on the user's request (spec 16.2): IND-47's OpenStreetMap map and Find on map (below, "OpenStreetMap"); the guided tour's one-time download of a MAK plan sheet after the user agrees (DEMO-12): one plain GET with the client name "Yantra/<version>" and nothing else (`app/net/curl_downloader.cpp`); and the optional sending of exports and reports (EXP-07): HTTP(S) PUT per file or one SMTP(S) mail to a server the user enters (`app/net/curl_sender.cpp`). Library `yantra_app_net`, linked into the `yantra` executable and its tests only; the tests use file:// and loopback servers, no network. TLS comes from the system's libcurl (with its own TLS library); Yantra does not link OpenSSL itself |
| OpenGL 3 | system library (e.g. Mesa `libgl1`, `libgl1-mesa-dri`); no separate loader library (Dear ImGui's bundled loader) | - (API) | https://www.khronos.org/opengl/ | Rendering of the desktop app; Mesa llvmpipe software rendering in the launch tests (TST-04) |
| PDFium (prebuilt by pdfium-binaries) | 158.0.8125.0, release `chromium/8125` of https://github.com/bblanchon/pdfium-binaries, asset `pdfium-linux-x64.tgz`, SHA-256 `e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659`; built without V8 and XFA. Not in git: configure fetches the pinned archive into `<build>/_deps/` and checks the hash (`cmake/YantraPdfium.cmake`; offline: `-DYANTRA_PDFIUM_ARCHIVE=<file>`) | PDFium BSD-3-Clause (`licenses/pdfium.txt` in the archive); the pdfium-binaries build scripts MIT; bundled in `libpdfium.so` under their own licences, all in the archive's `licenses/`: FreeType (FTL), libjpeg-turbo (IJG, BSD-3-Clause, zlib), libpng, zlib, lcms (MIT), OpenJPEG (BSD-2-Clause), AGG 2.3, abseil (Apache-2.0), ICU (Unicode), HarfBuzz (MIT), Highway (Apache-2.0), fast_float (MIT), simdutf (MIT), dragonbox (Apache-2.0 with LLVM exception, or BSL-1.0), LLVM libc++ and libc (Apache-2.0 with LLVM exception). All are compatible with GPL-3.0-or-later; `install` puts the library in `lib/yantra/` and the archive's `licenses/` in `share/licenses/pdfium/`. | https://pdfium.googlesource.com/pdfium (prebuilt: https://github.com/bblanchon/pdfium-binaries) | PDF floor plans (IND-02): page list, page sizes, thumbnails and rendering of any page or part of a page at a chosen resolution (`core/src/plan/pdf_source.cpp`). Linked as a shared library after the C++ runtime (it exports its own `operator new`); every PDFium call holds one process-wide lock (PDFium is not thread-safe). On Windows the asset `pdfium-win-x64.tgz` of the same release (`pdfium.dll` next to the programs, OA-02). |
| OpenSSL | not used | (Apache-2.0) | https://www.openssl.org | Optional in spec 16.2 for HTTPS/wss of the remote API; not linked: the probe plugin's CivetWeb is built with `NO_SSL` (HTTPS through a reverse proxy, docs/API.md). Only the system libcurl may use it internally for its own TLS. |
| Rust standard library | the toolchain's (through the radio parts) | MIT OR Apache-2.0 | https://www.rust-lang.org | Linked statically into the TETRA radio plugins (their protocol crate `yantra-proto` in `radio-common/receiver/rust`); nothing of Rust is linked into the application's binaries |

## Radio plugins: bundled libraries (RAD-04, RAD-136)

The radio plugins (`plugins/hackrf`, `plugins/soapy`, `plugins/pei`,
`plugins/simulator`; shipped in `lib/yantra/plugins/radios/`, PLG-29) are
self-contained: these libraries are built into them statically from
pinned, vendored sources (RAD-132, RAD-136). Yantra's own code never
links them.

Every bundled library is pinned under `third_party/` (git submodule, or
release archive with its SHA-256) and built statically into the plugins
with only the features used, by the scripts in `tools/deps/` (RAD-132,
RAD-136). `tools/deps/manifest.tsv` is the machine-readable list; the
licence bundle that ships next to the plugins
(`share/licenses/yantra-radios`, RAD-137) is made from it by
`tools/ci/licence_bundle.py`, which also checks it against this file, and
every release publishes the corresponding source with the upstream
archives ([BUILD.md](BUILD.md)). Entries marked *planned* are not in the repository yet; their
version, source and checksum are filled in when they are added.


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
| SoapyRemote 0.5.3 | BSL-1.0 | SoapySDR module of network radios (RAD-50): the client, built in; the server side only in the loopback tests (`tests/radio/soapy_remote`), never in a plugin | vendored subset of the release archive https://github.com/pothosware/SoapyRemote/archive/refs/tags/soapy-remote-0.5.3.tar.gz (SHA-256 de5bdf209dc93bf7c341077e1e21353037fc387e7c6a9ee41eace3390669dcc1) in `third_party/soapyremote/`, files unchanged (`tools/deps/verify_sources.py`); `client/LogAcceptor.cpp` is built from a copy with an atomic stop flag (`tools/deps/soapyremote/LogAcceptor.cpp`, BSL-1.0, the change marked); on Windows `common/SoapyRPCSocket.cpp`, `common/SoapySocketDefs.in.hpp`, `common/SoapyInfoUtils.in.cpp` and `client/DiscoverServers.cpp` are compiled from copies in the build tree with `_MSC_VER` read as `_WIN32` (its Windows code is written for MSVC only; `tools/deps/sdr.cmake`); licence `LICENSE_1_0.txt`; without Avahi (mDNS) and without its multicast discovery: the plugins list the servers named in `YANTRA_SOAPY_REMOTE` |
| SoapyPlutoSDR + libiio/libad9361; S: SoapyLMS7 (LimeSuite), SoapyUHD (UHD) | per module | further SoapySDR modules of RAD-50 | *planned*: not built yet. libiio needs libxml2, which RAD-04 does not list (owner decision); UHD needs Boost (not in RAD-04); LimeSuite is S |
| pocketfft (C++ header-only), branch `cpp` at 4d865cb | BSD-3-Clause | FFT, spectrum and channelizer of the receiver (RAD-100); later the generator channel model | vendored unchanged in `third_party/pocketfft/` from https://github.com/mreineck/pocketfft (commit 4d865cba24359b05b37da4e75aee24498c3b78e7, 2026-10-18; `pocketfft_hdronly.h` SHA-256 518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d, `LICENSE.md`); header-only, compiled into the receiver library with `POCKETFFT_NO_MULTITHREADING` |
| nlohmann/json 3.12.0 | MIT | record files (RAD-65), scene JSON (RAD-36), later SigMF metadata and scenario files | vendored single header in `third_party/nlohmann_json/` from the v3.12.0 release (`json.hpp`, SHA-256 aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63), unchanged; header-only, compiled into what includes it |
| serde, serde_json (Rust crates) | MIT OR Apache-2.0 | allowed by RAD-04 for JSON in the Rust protocol crate | not used: the protocol crate (`radio-common/receiver/rust`, crate `yantra-proto`) has no external crates (`Cargo.lock` lists only itself; `cargo-deny` checks it) |
| Rust standard library | MIT OR Apache-2.0 | linked statically through the Rust protocol crate `yantra-proto` (`radio-common/receiver/rust`, static library in the TETRA decoder, RAD-101, RAD-132) | Rust toolchain |


On Windows (RAD-131) the bundled libraries are static and stay inside the
plugin: SoapySDR is compiled against a copy of its `include/SoapySDR/Config.h`
without `#define SOAPY_SDR_DLL` (upstream's static-library case), and the
export directives (`.drectve` sections, from the `dllexport` marks of
SoapySDR, libhackrf, libairspy and SoapyRemote) are removed from every
bundled archive after it is built, so a plugin exports only its entry
point (RAD-133). The vendored files themselves are unchanged.
The terminal plugin's serial ports use no library (RAD-04): its own code
on POSIX termios, and on Windows the Win32 communications API with the
SetupAPI and the configuration manager (`setupapi.dll`, `cfgmgr32.dll`,
`advapi32.dll`: Windows system DLLs, allowed by RAD-132).

## Windows packages (NFR-09)

On Windows the app links the libraries above as the DLLs of pinned MSYS2
UCRT64 binary packages (`tools/windows/msys2.lock`; the GCC runtime from
`tools/sdk/msys2.lock`, OA-02). The Windows zip and setup
.exe ship exactly the DLLs the programs and plugins import, next to the
programs in `bin\`, with each package's licence texts in
`share\licenses\msys2\<package>\` and the list in
`share\licenses\msys2\PACKAGES.txt`
(`packaging/windows/package_windows.py`); `SOURCE.txt` names the
corresponding source (https://github.com/msys2/MINGW-packages).

| DLL | MSYS2 package | Licence | Brought in by |
|---|---|---|---|
| `libstdc++-6.dll`, `libgcc_s_seh-1.dll` | libstdc++, libgcc 16.2.0 | GPL-3.0-or-later WITH GCC-exception-3.1 | the C++ runtime of every program and plugin |
| `libwinpthread-1.dll` | libwinpthread 14.0.0 | MIT AND BSD-3-Clause | the C++ runtime (threads) |
| `glfw3.dll` | glfw 3.5.1 | Zlib | window and input |
| `libsqlite3-0.dll` | sqlite 3.54.1 | blessing (public domain) | project file |
| `libzstd.dll` | zstd 1.5.7 | BSD-3-Clause OR GPL-2.0-or-later | recording compression |
| `libhpdf.dll`, `libpng16-16.dll`, `zlib1.dll` | libharu 2.4.6, libpng 1.6.59, zlib 1.4 | Zlib, libpng-2.0, Zlib | PDF reports |
| `libcurl-4.dll` | curl-winssl 8.23.0 (TLS by Windows' own Schannel) | curl | download and sending (EXP-07, DEMO-12) |
| `libbrotlicommon.dll`, `libbrotlidec.dll`, `libidn2-0.dll`, `libpsl-5.dll`, `libssh2-1.dll`, `libunistring-5.dll`, `libiconv-2.dll`, `libintl-8.dll` | brotli 1.2.1, libidn2 2.3.8, libpsl 0.21.5, libssh2-wincng 1.11.1, libunistring 1.4.2, libiconv 1.19.1, gettext-runtime 1.0 | MIT, GPL-2.0-or-later OR LGPL-3.0-or-later, MIT, BSD-3-Clause, LGPL-3.0-or-later OR GPL-2.0-or-later, LGPL-2.1-or-later, LGPL-2.1-or-later | libcurl's own dependencies |
| `pdfium.dll` | (not MSYS2) PDFium 158.0.8125.0 for Windows, see Libraries | BSD-3-Clause and the licences in `share\licenses\pdfium` | PDF floor plans |

The libidn2 package carries no licence file; the packages add the GNU
licence texts it names (GPL-2.0, LGPL-3.0, GPL-3.0). Mesa's
`opengl32.dll` (llvmpipe), which the Windows test tree uses to render
off-screen in the test machine, is a test tool and never shipped: the
installed app uses the system's OpenGL driver.

## Build, test and packaging tools

These tools are never linked into Yantra and do not affect its licence.

| Tool | Licence | Used for |
|---|---|---|
| CMake, Ninja | BSD-3-Clause, Apache-2.0 | build |
| gcc / clang | GPL-3.0-or-later WITH GCC-exception-3.1 / Apache-2.0 WITH LLVM-exception | C++20 and C11 compilers; clang's libFuzzer for the fuzz targets (`-DYANTRA_FUZZ=ON`, TST-12) |
| cargo / rustc | MIT OR Apache-2.0 | the Rust protocol crate of the radio plugins (RAD-101, `radio-common/receiver/rust`); rustc from the cross SDK below |
| cargo-deny, cargo-fuzz (optional) | MIT OR Apache-2.0 | licence check and fuzzing of the Rust crate |
| Python 3 (standard library) | PSF-2.0 | check, test and screenshot scripts (`tools/ci`, `tools/screenshots`, `tests/`) |
| Doxygen | GPL-2.0-only | plugin API reference from `plugin-api/include/yantra/plugin.h` (DOC-17; target `docs-plugin-api`, ctest `plugin_api_doxygen`) and the radio API reference from `radio-api/include/yantra/radio.h` (RAD-97; target `docs-radio-api`, ctest `radio_api_doxygen`) |
| pandoc | GPL-2.0-or-later | manual, quick starts and notes from Markdown to HTML (DOC-02, DOC-20) |
| WeasyPrint | BSD-3-Clause | HTML to PDF for the manual (DOC-20) and the PDF user manual `yantra-manual-<version>.pdf` (DOC-02a, `tools/docs/build_manual.py`, through its Python API), run on pandoc's HTML; the radio plugins manual (RAD-97, `tools/docs/build_radio_manual.py`) |
| DejaVu fonts (system package, optional) | Bitstream Vera Fonts licence and public domain (DejaVu changes) | the fonts of the radio plugins manual: WeasyPrint embeds subsets of DejaVu Sans and DejaVu Sans Mono in the PDF, which their licence allows |
| bubblewrap (optional, local only) | LGPL-2.0-or-later | the clean-environment conformance run on a developer machine (`tools/ci/clean_env.py`, RAD-133) |
| oxipng | MIT | lossless size reduction of the PNGs in `docs/img` and `docs/samples` (SCR-03) |
| ffmpeg (program only) | GPL-2.0-or-later / LGPL-2.1-or-later | short animated captures (SCR-06); no FFmpeg library is linked |
| binutils (readelf, objdump), `strings` | GPL-3.0-or-later | completeness and build-folder checks of the installed binaries and plugins (RAD-133, NFR-09) |
| dpkg | GPL-2.0-or-later | the Linux package (NFR-09) |
| NSIS (makensis) | Zlib; its LZMA decompressor in the setup .exe CPL-1.0 with the special exception for NSIS installers | the Windows setup .exe, made on Linux (NFR-09; `packaging/windows/yantra.nsi`) |
| the cross SDK in `tools/sdk` (below) | Apache-2.0 WITH LLVM-exception; the packages' own | the default compiler, the Windows cross build, the icon and version resources and the stripped programs of the packages (OA-02, RAD-139) |
| reuse | Apache-2.0, CC0-1.0, CC-BY-SA-4.0, GPL-3.0-or-later | REUSE compliance check (NFR-01) |
| PyYAML | MIT | YAML syntax check of the CI workflows in `tools/ci/lint.sh` (TST-02, RAD-134); CI only |
| Xvfb (X.Org) | MIT/X11 | Private off-screen X display of the app launch tests and screenshots (NFR-10, TST-04, SCR-01); `tests/app/with_xvfb.sh` |
| Mesa (llvmpipe) | MIT | OpenGL 3.1 software rendering in the app launch tests (TST-04); on Windows its `opengl32.dll` from the MSYS2 package `mesa` in the test tree only (never packaged) |

## Cross SDK (build tools and the Windows runtime; RAD-139)

`tools/sdk/yantra_sdk.py make` unpacks these into the git-ignored SDK
folder (`sdk/` or `$YANTRA_SDK_DIR`); nothing is installed in the system.
Every file is pinned by name, version and SHA-256 in `tools/sdk/*.lock`
and checked against its publisher's signature ([BUILD.md](BUILD.md)).
The SDK is never shipped. Of its contents only the Windows GCC runtime
and the Windows Rust standard library end up in an artefact: linked
statically into the Windows `.dll` and `.exe` (RAD-132), the GCC runtime
under the GCC Runtime Library Exception 3.1, which allows that whatever
the program's licence.

| Component | Licence | Used for | How it is obtained (`tools/sdk/*.lock`) |
|-----------|---------|----------|------------------------------------------|
| LLVM 23.1.5 (clang, lld, llvm-ar, llvm-rc, llvm-objdump, llvm-strip, llvm-symbolizer, compiler-rt) | Apache-2.0 WITH LLVM-exception | the compiler of every tree but `gcc`, for Linux x86-64 and Windows x64; the sanitizer and libFuzzer runtimes of the Linux sanitizer and fuzz trees (never in a release) | official release archive `LLVM-23.1.5-Linux-X64.tar.zst` from https://github.com/llvm/llvm-project/releases (`llvm.lock`), signature checked with https://releases.llvm.org/release-keys.asc |
| MSYS2 UCRT64: mingw-w64 headers, crt, winpthreads, libwinpthread 14.0.0.r598 | headers ZPL-2.1 AND LGPL-2.1-or-later, crt ZPL-2.1, winpthreads MIT AND BSD-3-Clause (the packages' licences) | Windows target headers, the UCRT and system import libraries, the thread library of libstdc++ (linked statically into Windows artefacts) | binary packages from https://repo.msys2.org/mingw/ucrt64 (`msys2.lock`), each checked against its `.sig` with the keys of the pinned `msys2-keyring` package; licence texts in `sysroots/x86_64-w64-mingw32/share/licenses/` |
| MSYS2 UCRT64: gcc, libgcc, libstdc++ 16.2.0 | GPL-3.0-or-later WITH GCC-exception-3.1 | libstdc++ headers and libraries and libgcc of the Windows target (linked statically into Windows artefacts, RAD-132); the compiler programs of the package are left out | as above (`msys2.lock`) |
| MSYS2 UCRT64: tzdata 2026f | Public Domain | dependency of the libstdc++ package (`std::chrono` time zones); unused | as above (`msys2.lock`) |
| Rust 1.93.1: rustc, rust-std for x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu | MIT OR Apache-2.0 | the Rust protocol crate for Linux and Windows with one compiler (the Windows rust-std only works with the official rustc build of the same version) | official dist tarballs from https://static.rust-lang.org/dist (`rust.lock`), signatures checked with the Rust release key |

## Fonts

Fonts under the SIL OFL 1.1 are shipped as separate files and loaded at
run time, never compiled into the binary (NFR-01).

| Font | Version | Files | Licence | Source | Used for |
|---|---|---|---|---|---|
| Inter | 4.1 (copied unchanged from the earlier Yantra implementation's `data/fonts/`) | `data/fonts/Inter-Regular.ttf`, `data/fonts/Inter-SemiBold.ttf`, `data/fonts/Inter-OFL.txt` (its licence) | OFL-1.1 | https://github.com/rsms/inter | Text of the exported floor images (EXP-08, `core/src/export/image.cpp`), of the reports and of the PDF user manual (DOC-02a, embedded as subsets); installed in `share/yantra/fonts/` |
| JetBrains Mono | 2.304 (release tag `v2.304` of https://github.com/JetBrains/JetBrainsMono, `fonts/ttf/JetBrainsMono-Regular.ttf` unchanged, SHA-256 `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f`; the same file as the earlier Yantra implementation's `data/fonts/`) | `data/fonts/JetBrainsMono-Regular.ttf`, `data/fonts/JetBrainsMono-OFL.txt` (its licence) | OFL-1.1 | https://github.com/JetBrains/JetBrainsMono | Numeric tables of the PDF reports (EXP-05, `core/src/report/pdf.cpp`) and code in the PDF user manual (DOC-02a), embedded as subsets; installed in `share/yantra/fonts/` |

## Data

| Item | Files | Licence | Source | Notes |
|---|---|---|---|---|
| OpenStreetMap (online service, not in the repository or packages) | none: map tiles are fetched at run time into the user's cache (`<user cache>/tiles/osm/<z>/<x>/<y>.png`) | `ODbL-1.0` (map data and the rendered tiles; attribution "© OpenStreetMap contributors" required) | https://www.openstreetmap.org/copyright; tiles https://tile.openstreetmap.org, search https://nominatim.openstreetmap.org | IND-47, the About building tab (`core/src/about/osm.cpp`, `app/model/src/osm_map.cpp`). Tile usage policy (https://operations.osmfoundation.org/policies/tiles/): tiles only while the map is shown, one connection, the client name "Yantra/<version>" (no personal data), a disk cache kept for 7 days before a refetch, a failed tile not asked again for 5 minutes, no prefetching or bulk download; offline the cached tiles or a placeholder. Nominatim usage policy (https://operations.osmfoundation.org/policies/nominatim/): one search per press of Find on map (at most one a second), never automatic or while typing, the same client name. The attribution "© OpenStreetMap contributors" is always shown on the map and under the report's map (which uses only cached tiles). |
| curtat.be logo | `data/branding/curtat_logo.svg`, `data/branding/curtat_logo_{light,dark}_{64,256,1024}.png` | `LicenseRef-Curtat-Trademark` (trademark of Curtat BV, all rights reserved; not GPL) | Curtat BV, https://curtat.be | Shown in the About dialog and in reports (BRD-01..04). Forks must remove or replace it. |
| Yantra logo | `data/logo/*.svg`, `data/logo/png/yantra_logo_*.png` | `GPL-3.0-or-later` (part of Yantra) | Supplied by the project owner | Application, window and desktop icon, README, manual, report favicon (BRD-05..07). Forks may keep it. |
| Fraunces Tavern floor plans | `data/demo/fraunces/fraunces_tavern_{first,second}_floor.png` | `LicenseRef-Public-Domain` (work of the United States federal government) | Historic American Buildings Survey, Library of Congress: HABS NY-6321 sheets 3 and 4, https://www.loc.gov/pictures/item/ny1654.sheet.00003a/ and .../ny1654.sheet.00004a/ (unchanged, SHA-256 in `data/demo/fraunces/README.md`) | The plans of the demo project (DEMO-05, DEMO-06, DEMO-08; docs/DEMO.md), bundled and shipped inside the generated demo project. Credit: "Historic American Buildings Survey, Library of Congress". |
| MAK floor plans (not in the repository or packages) | none: `data/demo/mak/<id>.json` holds only a description per sheet (URL, SHA-256, size, credit) | `CC BY-NC-ND 4.0` (MAK - Museum of Applied Arts, Vienna) | Three sheets of the MAK online collection (spec section 0 [MAK]): Beer house Vienna KI 23126-36, Haus Claeson Falsterbo KI 23497-359, Villa Bunzl Vienna KI 23126-41 (Josef Frank, Oskar Wlach) | The example buildings of the tour (DEMO-12): the app downloads the unaltered image on first use after the user agrees, checks its SHA-256 and keeps it in the user's cache; projects reference it by SHA-256 (IND-03). Never committed, packaged, adapted or shown in screenshots, docs or samples. The generated example projects and walk recordings (DEMO-13) contain no image data. |

## Excluded

| Component | Reason |
|-----------|--------|
| OnAir code and libraries | excluded source; no code of it is used or shipped (its `dect2cli` is not used either) |
| osmo-tetra `crypto/` | decryption is out of scope |
| Proprietary survey software, file formats and dongles | excluded source |
| SoapySDRPlay3 and the SDRplay API | the SDRplay API is proprietary and cannot be built into a GPL plugin (RAD-50, RAD-132) |
| SoapyBladeRF and libbladeRF 2.6.0 (bladeRF 2025.10) | libbladeRF (LGPL-2.1) cannot be built without the Analog Devices no-OS AD9361 driver (git submodule of bladeRF, pinned 0bba46e; its own sources include its headers even for the bladeRF 1): that driver's licence says "use of the software ... must be run on or directly connected to an Analog Devices Inc. component", a use restriction that GPL-3.0 section 10 does not allow on top of it. Not built in (RAD-132) until the owner decides (a bladeRF-1-only build with own stub headers, a licence review, or no bladeRF) |
| Avahi (mDNS discovery of SoapyRemote) | not in RAD-04; SoapyRemote is built with its "none" mDNS back end |
