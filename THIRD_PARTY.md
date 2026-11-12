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
| yantra-radios | v0.3.1 (commit 9c6d3c9), git submodule `third_party/yantra-radios` | GPL-3.0-or-later; its plugins contain AGPL-3.0 parts (osmo-tetra) and the libraries of its own THIRD_PARTY.md | The yantra-radios project (its README names the public source) | Radio API header `yantra/radio.h` and the radio plugins with their SDR processing and TETRA decoder (PLG-26); its components and licence bundle are listed in its own THIRD_PARTY.md |
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
| libcurl | 7.85 or newer, system library (`libcurl4-openssl-dev`; tested with 8.18.0) | curl (MIT-style, `curl` licence) | https://curl.se/libcurl/ | Two uses, both only on the user's request (spec 16.2): the guided tour's one-time download of a MAK plan sheet after the user agrees (DEMO-12): one plain GET with the client name "Yantra/<version>" and nothing else (`app/net/curl_downloader.cpp`); and the optional sending of exports and reports (EXP-07): HTTP(S) PUT per file or one SMTP(S) mail to a server the user enters (`app/net/curl_sender.cpp`). Library `yantra_app_net`, linked into the `yantra` executable and its tests only; the tests use file:// and loopback servers, no network. TLS comes from the system's libcurl (with its own TLS library); Yantra does not link OpenSSL itself |
| OpenGL 3 | system library (e.g. Mesa `libgl1`, `libgl1-mesa-dri`); no separate loader library (Dear ImGui's bundled loader) | - (API) | https://www.khronos.org/opengl/ | Rendering of the desktop app; Mesa llvmpipe software rendering in the launch tests (TST-04) |
| PDFium (prebuilt by pdfium-binaries) | 158.0.8125.0, release `chromium/8125` of https://github.com/bblanchon/pdfium-binaries, asset `pdfium-linux-x64.tgz`, SHA-256 `e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659`; built without V8 and XFA. Not in git: configure fetches the pinned archive into `<build>/_deps/` and checks the hash (`cmake/YantraPdfium.cmake`; offline: `-DYANTRA_PDFIUM_ARCHIVE=<file>`) | PDFium BSD-3-Clause (`licenses/pdfium.txt` in the archive); the pdfium-binaries build scripts MIT; bundled in `libpdfium.so` under their own licences, all in the archive's `licenses/`: FreeType (FTL), libjpeg-turbo (IJG, BSD-3-Clause, zlib), libpng, zlib, lcms (MIT), OpenJPEG (BSD-2-Clause), AGG 2.3, abseil (Apache-2.0), ICU (Unicode), HarfBuzz (MIT), Highway (Apache-2.0), fast_float (MIT), simdutf (MIT), dragonbox (Apache-2.0 with LLVM exception, or BSL-1.0), LLVM libc++ and libc (Apache-2.0 with LLVM exception). All are compatible with GPL-3.0-or-later; `install` puts the library in `lib/yantra/` and the archive's `licenses/` in `share/licenses/pdfium/`. | https://pdfium.googlesource.com/pdfium (prebuilt: https://github.com/bblanchon/pdfium-binaries) | PDF floor plans (IND-02): page list, page sizes, thumbnails and rendering of any page or part of a page at a chosen resolution (`core/src/plan/pdf_source.cpp`). Linked as a shared library after the C++ runtime (it exports its own `operator new`); every PDFium call holds one process-wide lock (PDFium is not thread-safe). On Windows the asset `pdfium-win-x64.tgz` of the same release (`pdfium.dll` next to the programs, OA-02). |
| OpenSSL | not used | (Apache-2.0) | https://www.openssl.org | Optional in spec 16.2 for HTTPS/wss of the remote API; not linked: the probe plugin's CivetWeb is built with `NO_SSL` (HTTPS through a reverse proxy, docs/API.md). Only the system libcurl may use it internally for its own TLS. |
| Rust standard library | the toolchain's (through yantra-radios) | MIT OR Apache-2.0 | https://www.rust-lang.org | Linked statically into the TETRA radio plugins of yantra-radios (its protocol crate `yantra-proto`); nothing of Rust is linked into Yantra's own binaries |

### Inside the shipped radio plugins (yantra-radios v0.3.1)

The radio plugins Yantra ships (`lib/yantra/plugins/radios/*.so`, PLG-26)
are self-contained: yantra-radios builds these libraries into them
statically from pinned, vendored sources (RAD-132, RAD-136). Yantra's own
code never links them; the details, the corresponding source and the licence
bundle (installed in `share/licenses/yantra-radios/`, RAD-137) are in
yantra-radios' `THIRD_PARTY.md` and `tools/deps/manifest.tsv`.

| Component | Version | Licence | Used for (in the plugins) |
|---|---|---|---|
| osmo-tetra | commit 59fb603 | AGPL-3.0-or-later | TETRA lower layers of the decoder (RAD-101); its `crypto/` is never built or used. GPL-3.0 section 13 allows the combination; the source link in About and the API status reply is the AGPL source offer (NFR-01) |
| libosmocore | 1.11.0 (subset) | GPL-2.0-or-later | bit and convolutional-code helpers of osmo-tetra |
| talloc | 2.4.3 (subset) | LGPL-3.0-or-later | memory allocator of libosmocore |
| SoapySDR | 0.8.1 | BSL-1.0 | the SDR front layer (RAD-50) |
| SoapyHackRF + libhackrf | 0.3.6 + 2026.01.3 | MIT + BSD-3-Clause | HackRF |
| SoapyRTLSDR + librtlsdr | 0.3.4 + 2.0.3 | MIT + GPL-2.0-or-later | RTL-SDR |
| SoapyAirspy + libairspy | 0.2.1 + 1.0.10 | MIT + BSD-3-Clause | Airspy |
| SoapyRemote | 0.5.3 | BSL-1.0 | network radios (client) |
| libusb | 1.0.30 | LGPL-2.1-or-later | USB access of the vendor libraries |
| pocketfft (C++) | commit 4d865cb | BSD-3-Clause | FFT and channelizer of the receiver |
| nlohmann/json | 3.12.0 | MIT | record files and scene JSON |
| Rust standard library | the toolchain's | MIT OR Apache-2.0 | the protocol crate `yantra-proto` |

## Windows packages (NFR-09)

On Windows the app links the libraries above as the DLLs of pinned MSYS2
UCRT64 binary packages (`tools/windows/msys2.lock`; the GCC runtime from
yantra-radios' `tools/sdk/msys2.lock`, OA-02). The Windows zip and setup
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
| cargo / rustc | MIT OR Apache-2.0 | the Rust crate of the yantra-radios build (ExternalProject) |
| Python 3 (standard library) | PSF-2.0 | check, test and screenshot scripts (`tools/ci`, `tools/screenshots`, `tests/`) |
| Doxygen | GPL-2.0-only | plugin API reference from `include/yantra/plugin.h` (DOC-17; target `docs-plugin-api`, ctest `plugin_api_doxygen`) |
| pandoc | GPL-2.0-or-later | manual, quick starts and notes from Markdown to HTML (DOC-02, DOC-20) |
| WeasyPrint | BSD-3-Clause | HTML to PDF for the manual (DOC-20), run on pandoc's HTML |
| oxipng | MIT | lossless size reduction of the PNGs in `docs/img` and `docs/samples` (SCR-03) |
| ffmpeg (program only) | GPL-2.0-or-later / LGPL-2.1-or-later | short animated captures (SCR-06); no FFmpeg library is linked |
| binutils (readelf, objdump), `strings` | GPL-3.0-or-later | completeness and build-folder checks of the installed binaries and plugins (RAD-133, NFR-09) |
| dpkg | GPL-2.0-or-later | the Linux package (NFR-09) |
| NSIS (makensis) | Zlib; its LZMA decompressor in the setup .exe CPL-1.0 with the special exception for NSIS installers | the Windows setup .exe, made on Linux (NFR-09; `packaging/windows/yantra.nsi`) |
| the cross SDK of yantra-radios (LLVM clang, lld, llvm-rc, llvm-strip; MSYS2 UCRT64 packages) | Apache-2.0 WITH LLVM-exception; the packages' own | the Windows cross build, the icon and version resources and the stripped programs of the packages (OA-02, RAD-139) |
| reuse | Apache-2.0, CC0-1.0, CC-BY-SA-4.0, GPL-3.0-or-later | REUSE compliance check (NFR-01) |
| PyYAML | MIT | YAML syntax check of the CI workflows in `tools/ci/lint.sh` (TST-02); CI only, as in yantra-radios |
| Xvfb (X.Org) | MIT/X11 | Private off-screen X display of the app launch tests and screenshots (NFR-10, TST-04, SCR-01); `tests/app/with_xvfb.sh` |
| Mesa (llvmpipe) | MIT | OpenGL 3.1 software rendering in the app launch tests (TST-04); on Windows its `opengl32.dll` from the MSYS2 package `mesa` in the test tree only (never packaged) |

## Fonts

Fonts under the SIL OFL 1.1 are shipped as separate files and loaded at
run time, never compiled into the binary (NFR-01).

| Font | Version | Files | Licence | Source | Used for |
|---|---|---|---|---|---|
| Inter | 4.1 (copied unchanged from the earlier Yantra implementation's `data/fonts/`) | `data/fonts/Inter-Regular.ttf`, `data/fonts/Inter-SemiBold.ttf`, `data/fonts/Inter-OFL.txt` (its licence) | OFL-1.1 | https://github.com/rsms/inter | Text of the exported floor images (EXP-08, `core/src/export/image.cpp`) and of the reports; installed in `share/yantra/fonts/` |
| JetBrains Mono | 2.304 (release tag `v2.304` of https://github.com/JetBrains/JetBrainsMono, `fonts/ttf/JetBrainsMono-Regular.ttf` unchanged, SHA-256 `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f`; the same file as the earlier Yantra implementation's `data/fonts/`) | `data/fonts/JetBrainsMono-Regular.ttf`, `data/fonts/JetBrainsMono-OFL.txt` (its licence) | OFL-1.1 | https://github.com/JetBrains/JetBrainsMono | Numeric tables of the PDF reports (EXP-05, `core/src/report/pdf.cpp`), embedded as a subset; installed in `share/yantra/fonts/` |

## Data

| Item | Files | Licence | Source | Notes |
|---|---|---|---|---|
| curtat.be logo | `data/branding/curtat_logo.svg`, `data/branding/curtat_logo_{light,dark}_{64,256,1024}.png` | `LicenseRef-Curtat-Trademark` (trademark of Curtat BV, all rights reserved; not GPL) | Curtat BV, https://curtat.be | Shown in the About dialog and in reports (BRD-01..04). Forks must remove or replace it. |
| Yantra logo | `data/logo/*.svg`, `data/logo/png/yantra_logo_*.png` | `GPL-3.0-or-later` (part of Yantra) | Supplied by the project owner | Application, window and desktop icon, README, manual, report favicon (BRD-05..07). Forks may keep it. |
| Fraunces Tavern floor plans | `data/demo/fraunces/fraunces_tavern_{first,second}_floor.png` | `LicenseRef-Public-Domain` (work of the United States federal government) | Historic American Buildings Survey, Library of Congress: HABS NY-6321 sheets 3 and 4, https://www.loc.gov/pictures/item/ny1654.sheet.00003a/ and .../ny1654.sheet.00004a/ (unchanged, SHA-256 in `data/demo/fraunces/README.md`) | The plans of the demo project (DEMO-05, DEMO-06, DEMO-08; docs/DEMO.md), bundled and shipped inside the generated demo project. Credit: "Historic American Buildings Survey, Library of Congress". |
| MAK floor plans (not in the repository or packages) | none: `data/demo/mak/<id>.json` holds only a description per sheet (URL, SHA-256, size, credit) | `CC BY-NC-ND 4.0` (MAK - Museum of Applied Arts, Vienna) | Three sheets of the MAK online collection (spec section 0 [MAK]): Beer house Vienna KI 23126-36, Haus Claeson Falsterbo KI 23497-359, Villa Bunzl Vienna KI 23126-41 (Josef Frank, Oskar Wlach) | The example buildings of the tour (DEMO-12): the app downloads the unaltered image on first use after the user agrees, checks its SHA-256 and keeps it in the user's cache; projects reference it by SHA-256 (IND-03). Never committed, packaged, adapted or shown in screenshots, docs or samples. The generated example projects and walk recordings (DEMO-13) contain no image data. |
