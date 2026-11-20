<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Plugin API changelog

Every change of the plugin API (`plugin-api/include/yantra/plugin.h`) per version
(DOC-18), marked **compatible** (minor version: plugins built for an
earlier minor version keep loading) or **breaking** (major version:
plugins must be rebuilt), with deprecations and a migration note. The
rules are in [PLUGIN_API.md](PLUGIN_API.md#versioning-plg-04). The
documentation of an API version stays available while Yantra supports
plugins for it.

## 1.2

**Compatible.** Plugins built for 1.0 and 1.1 load unchanged.

- `YANTRA_PLUGIN_API_VERSION_MINOR` is 2.
- Neighbour link rule (`yantra_field_link`, PLG-05): a linked field is
  the neighbour entry's own value when the entry gives it, else the
  broadcasting cell's value of the linked cell field (until 1.1 a linked
  field the entry lacked left the cell unknown). So a technology may link
  OPTIONAL neighbour fields such as a neighbour's location area or
  network code, which a radio sends only when they differ from the
  broadcaster's (yantra-radios RAD-22). Values taken from the
  broadcasting cell are presumed (inherited): they rank a match in the
  cell database but never refuse one. An entry without any linked value
  of its own names no cell. No layout change.
- The TETRA technology plugin links `neighbour_la`, `neighbour_mcc` and
  `neighbour_mnc` besides `neighbour_frequency_hz`
  ([plugins/TETRA.md](plugins/TETRA.md)).

Deprecations: none. Migration: a plugin that links OPTIONAL neighbour
fields declares `api_version` 1.2 (a 1.1 host refuses it, as it would
leave such entries unknown); one that links only fields its radios always
send keeps working on every 1.x host.

## 1.1

**Compatible.** Plugins built for 1.0 load unchanged.

- `YANTRA_PLUGIN_API_VERSION_MINOR` is 1.
- `yantra_host_api::control(host, request, reply, reply_size,
  out_length)` (new member at the end of the host table): runs a command of the application's
  control set (API-01/02: record, monitoring, play, sound, status, quit)
  and returns its JSON reply; used by the probe feature plugin (PLG-24).
  Refused (`YANTRA_ERR_BUSY`) on the main/UI thread and on a measurement
  thread; `YANTRA_ERR_BUFFER_TOO_SMALL` with the needed length, and a
  repeated call returns the same reply without running the command twice;
  `YANTRA_ERR_UNSUPPORTED` on a host without control. The commands are
  listed in [API.md](API.md#commands).
- `yantra_host_api::data_folder(host, buffer, buffer_size, out_length)`
  (new member at the end of the host table): the plugin's own folder for
  its files (per user and plugin id, created owner-only); any thread.
- Behaviour now as documented in 1.0: a feature plugin's `start` runs
  after `init` (a failing start disables it), its `stop` before
  `shutdown`, and `on_record` gets every record of the measurement
  pipeline (live and replay) while it is enabled.

Deprecations: none. Migration: a plugin that uses the new services
checks `YANTRA_HAS_FIELD(host, yantra_host_api, control)` (or
`data_folder`) and declares `api_version` 1.1; one that does not keep
1.0 loads on 1.0 and 1.1 hosts.

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
- Clarified before the first release (no layout change): LEVEL fields are
  the radio's level readings and get the calibration and antenna
  correction; broadcast values in dBm (radio groups BROADCAST and
  NEIGHBOURS) are SYSTEM_INFO, and a LEVEL category there is refused by
  the schema check (HW-09).

Deprecations: none. Migration: plugins built against the unversioned
placeholder header of earlier development builds (0.1) are not
supported; rebuild them against 1.0.

## Reference documentation per version

The reference of each API version is generated from the comments of
`plugin-api/include/yantra/plugin.h` and `plugin.hpp` by Doxygen (build target
`docs-plugin-api`, output `<build>/docs/plugin-api/html`; the ctest
`plugin_api_doxygen` fails on any undocumented item). Every release
publishes that output with its documentation, so the reference of 1.0,
1.1 and 1.2 stays available from the releases that shipped them while Yantra
loads plugins built for them. Within a major version the newest reference
covers the older minor versions too: members added later are marked with
their version (for example "API 1.1" in the host table).
