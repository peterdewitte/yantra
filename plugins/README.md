<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plugins

One folder per plugin, each with its own `CMakeLists.txt`, README and
tests (label `plugin:<folder>`):

- radio plugins, on the radio API (`radio-api/`) and `radio-common/`:
  `hackrf`, `soapy`, `pei`, `simulator` and the tutorial example
  `example`;
- Yantra's plugins, on the plugin API (`plugin-api/`): the TETRA
  technology plugin `tetra`, the feature plugin `probe` and the
  tutorial example `beacon`.

No plugin uses `app/` or `core/` (PLG-30, ctest `boundary_lint`). The
table of all plugins is in the [README](../README.md#architecture); how
to add one: [docs/PLUGINS.md](../docs/PLUGINS.md#add-a-plugin).
