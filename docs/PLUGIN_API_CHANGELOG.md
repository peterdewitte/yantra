<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plugin API changelog

Every change of the plugin API (`include/yantra/plugin.h`) per version
(DOC-18), marked **compatible** (minor version: plugins built for an
earlier minor version keep loading) or **breaking** (major version:
plugins must be rebuilt), with deprecations and a migration note. The
rules are in [PLUGIN_API.md](PLUGIN_API.md#versioning-plg-04). The
documentation of an API version stays available while Yantra supports
plugins for it.

## 1.0 (first version)

First stable version of the plugin API.

- Entry point `yantra_plugin_entry` exchanging the host table
  (`yantra_host_api`) and the plugin descriptor
  (`yantra_plugin_descriptor`, frozen prefix `struct_size`,
  `api_version`, `kinds`).
- Plugin kinds technology, feature and exporter, each with its table;
  a UI table for windows and menu entries.
- Common data model: schemas, fields, data categories, records,
  provenance.
- Technology rules: channel mapping, C1, reselection, parameter
  comparison, neighbour cross-check links.
- Host services: log, notify, settings, time stamps, timers,
  `emit_record`, window control.
- Toolkit-neutral immediate-mode UI drawing table and translations.
- Status codes, constants and frozen layouts shared with the radio API.

Deprecations: none. Migration: plugins built against the unversioned
placeholder header of earlier development builds (0.1) are not
supported; rebuild them against 1.0.
