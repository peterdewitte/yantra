<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# nlohmann/json 3.12.0 (vendored)

The single-header release `json.hpp` and `LICENSE.MIT` of nlohmann/json
v3.12.0 (MIT), unchanged.

- Source: https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
- SHA-256 of `include/nlohmann/json.hpp`: aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
- `LICENSE.MIT` from the v3.12.0 tag, SHA-256 46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603

Used for the record files (RAD-65), the scene JSON (RAD-36) and later the
SigMF metadata and scenario files (RAD-04). Header-only: it is compiled
into the code that includes it (CMake target `yantra::json`), so plugins
stay self-contained (RAD-132). It is vendored rather than taken from the
system so every build uses the same pinned version.
