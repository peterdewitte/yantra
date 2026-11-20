<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Application

The desktop application `yantra` (Dear ImGui on GLFW and OpenGL 3.3):
`model/` (application state, settings, translations, the manual, the
camera), `gui/` (every window), `audio/` (miniaudio), `net/` (libcurl
downloads), `src/main.cpp`. It uses `core/` and the two APIs
(`radio-api/`, `plugin-api/`) only, never a plugin or `radio-common/`
(PLG-30, ctest `boundary_lint`).

- Documentation: [docs/APP.md](../docs/APP.md), [ARCHITECTURE.md](../ARCHITECTURE.md), the [user manual](../docs/manual/README.md).
- Tests: `tests/app/`, `tests/about/`, `tests/headless/` (label `app`).
