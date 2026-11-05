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

Entries are added together with the code that first uses each library.

| Component | Version | Licence | Source | Used for |
|---|---|---|---|---|
| yantra-radios | v0.2.2 (commit a78c5c1), git submodule `third_party/yantra-radios` | GPL-3.0-or-later; its plugins contain AGPL-3.0 parts (osmo-tetra) and the libraries of its own THIRD_PARTY.md | The yantra-radios project (its README names the public source) | Radio API header `yantra/radio.h` and the radio plugins with their SDR processing and TETRA decoder (PLG-26); its components and licence bundle are listed in its own THIRD_PARTY.md |
| nlohmann/json | 3.12.0 (single header, vendored unchanged in `third_party/nlohmann_json/`, SHA-256 in its README) | MIT | https://github.com/nlohmann/json | Reading yantra-radios record files (RAD-65, TST-10), the plugin test kit's reports and golden files (PLG-13); header-only, compiled into Yantra |
| SQLite | 3.37 or newer, system library (`libsqlite3-dev`; tested with 3.46.1), linked into the core | Public domain (blessing) | https://sqlite.org | The indoor project file `*.yantra` (IND-01, IND-03, IND-04; docs/formats/PROJECT.md); later the cell database and survey store (DB-01) |
| doctest | 2.4.12 (single header, vendored unchanged in `third_party/doctest/`, SHA-256 in its README) | MIT | https://github.com/doctest/doctest | C++ unit tests under ctest (TST-01); tests only, never shipped |
| stb_image, stb_image_write | stb_image 2.30, stb_image_write 1.16 (stb at commit d14d46f), vendored unchanged in `third_party/stb/` (SHA-256 in its README) | MIT OR Unlicense (public domain), the author's choice (its `LICENSE` and the end of each header) | https://github.com/nothings/stb | Floor plan images (IND-02): every format stb_image reads (PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR, PIC), decoded from memory in `core/src/plan/` (static library `yantra_stb_image`). stb_image_write writes the image fixtures of the tests only (later: report images). |
| PDFium (prebuilt by pdfium-binaries) | 158.0.8125.0, release `chromium/8125` of https://github.com/bblanchon/pdfium-binaries, asset `pdfium-linux-x64.tgz`, SHA-256 `e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659`; built without V8 and XFA. Not in git: configure fetches the pinned archive into `<build>/_deps/` and checks the hash (`cmake/YantraPdfium.cmake`; offline: `-DYANTRA_PDFIUM_ARCHIVE=<file>`) | PDFium BSD-3-Clause (`licenses/pdfium.txt` in the archive); the pdfium-binaries build scripts MIT; bundled in `libpdfium.so` under their own licences, all in the archive's `licenses/`: FreeType (FTL), libjpeg-turbo (IJG, BSD-3-Clause, zlib), libpng, zlib, lcms (MIT), OpenJPEG (BSD-2-Clause), AGG 2.3, abseil (Apache-2.0), ICU (Unicode), HarfBuzz (MIT), Highway (Apache-2.0), fast_float (MIT), simdutf (MIT), dragonbox (Apache-2.0 with LLVM exception, or BSL-1.0), LLVM libc++ and libc (Apache-2.0 with LLVM exception). All are compatible with GPL-3.0-or-later; `install` puts the library in `lib/yantra/` and the archive's `licenses/` in `share/licenses/pdfium/`. | https://pdfium.googlesource.com/pdfium (prebuilt: https://github.com/bblanchon/pdfium-binaries) | PDF floor plans (IND-02): page list, page sizes, thumbnails and rendering of any page or part of a page at a chosen resolution (`core/src/plan/pdf_source.cpp`). Linked as a shared library after the C++ runtime (it exports its own `operator new`); every PDFium call holds one process-wide lock (PDFium is not thread-safe). Linux x86-64 only for now (Windows DEFERRED). |

## Build, test and packaging tools

These tools are never linked into Yantra and do not affect its licence.

| Tool | Licence | Used for |
|---|---|---|
| reuse | Apache-2.0, CC0-1.0, CC-BY-SA-4.0, GPL-3.0-or-later | REUSE compliance check (NFR-01) |
| PyYAML | MIT | YAML syntax check of the CI workflows in `tools/ci/lint.sh` (TST-02); CI only, as in yantra-radios |

## Fonts

None yet. Fonts under the SIL OFL 1.1 are shipped as separate files and
loaded at run time, never compiled into the binary (NFR-01).

## Data

| Item | Files | Licence | Source | Notes |
|---|---|---|---|---|
| curtat.be logo | `data/branding/curtat_logo.svg`, `data/branding/curtat_logo_{light,dark}_{64,256,1024}.png` | `LicenseRef-Curtat-Trademark` (trademark of Curtat BV, all rights reserved; not GPL) | Curtat BV, https://curtat.be | Shown in the About dialog and in reports (BRD-01..04). Forks must remove or replace it. |
| Yantra logo | `data/logo/*.svg`, `data/logo/png/yantra_logo_*.png` | `GPL-3.0-or-later` (part of Yantra) | Supplied by the project owner | Application, window and desktop icon, README, manual, report favicon (BRD-05..07). Forks may keep it. |
