<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plugin API

Yantra's own plugin API (PLG-03): `include/yantra/plugin.h` (plain C ABI)
and `plugin.hpp` (C++ helpers), for technology, feature and exporter
plugins such as `plugins/tetra`, `plugins/probe` and `plugins/beacon`.
CMake target `yantra::plugin_api`, nothing to link. Radio plugins use the
radio API in [`radio-api/`](../radio-api/README.md) instead; the two APIs
do not include each other.

- Guide: [docs/PLUGINS.md](../docs/PLUGINS.md), summary
  [docs/PLUGIN_API.md](../docs/PLUGIN_API.md), changes
  [docs/PLUGIN_API_CHANGELOG.md](../docs/PLUGIN_API_CHANGELOG.md).
- Reference: `ninja docs-plugin-api` (Doxygen).
- Tests: `tests/plugin_api/` (header tests), `tools/yantra-plugin-test`
  (the plugin test kit).
