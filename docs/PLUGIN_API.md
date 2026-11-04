<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Yantra plugin API 1.0

This page describes the C interface between Yantra and its plugins
(DOC-17), declared in [`include/yantra/plugin.h`](../include/yantra/plugin.h)
with C++ helpers in [`include/yantra/plugin.hpp`](../include/yantra/plugin.hpp).
The header is the reference: every function, struct, field and callback is
documented there, with units, ownership and the thread that calls it.
`ninja -C build docs-plugin-api` turns it into HTML in
`build/docs/plugin-api/html`. Changes per version are listed in
[PLUGIN_API_CHANGELOG.md](PLUGIN_API_CHANGELOG.md) (DOC-18).

Radio plugins are not covered here: they use the radio API of
yantra-radios (`yantra/radio.h`, documented there, RAD-97).

## Plugin kinds (PLG-02)

A plugin is a shared library (`.so` on Linux, `.dll` on Windows) that
exports one function, `yantra_plugin_entry` (`YANTRA_PLUGIN_ENTRY_SYMBOL`).
It returns a `yantra_plugin_descriptor` whose `kinds` bits say which
tables it fills; one library may combine kinds.

| Kind | Table | What it adds |
|---|---|---|
| `YANTRA_KIND_TECHNOLOGY` | `yantra_technology_api` | A radio technology: bands and channel raster, the cell, scan and neighbour schemas, and its rules (channel mapping, C1, reselection, parameter comparison) |
| `YANTRA_KIND_FEATURE` | `yantra_feature_api` | Behaviour on top of the measurements: sees every record, may add RECORDING fields, windows and menu entries |
| `YANTRA_KIND_EXPORTER` | `yantra_exporter_api` | Export formats for stored measurements |

Every kind may also declare a UI (`yantra_plugin_ui_api`): panels,
windows, dialogs and menu entries (PLG-06).

The core of Yantra knows no technology (PLG-01): what a TETRA cell is,
which fields it has and how its channel numbers work comes from the
technology plugin only.

## Life cycle

1. **Load.** Yantra opens the library, looks up `yantra_plugin_entry` and
   calls it with its host table. The entry point only returns the
   descriptor (it may return NULL for a host it cannot work with); it
   has no other side effects.
2. **Check.** Yantra reads the frozen prefix (`struct_size`,
   `api_version`, `kinds`) and refuses a plugin built for another major
   version or a newer minor version before it reads anything else. It
   then checks the whole descriptor: ids, licence, kinds against their
   tables, schemas, the scope boundary, the UI tables and translations.
   A refused plugin is listed in the plugin manager with every reason.
3. **Init.** `init(host, &instance)` creates the plugin's state. The host
   table stays valid until `shutdown` returns.
4. **Start and process.** A feature gets `start` when it is enabled, then
   `on_record` for every record; technology rules are called by the
   measurement pipeline; exporters get `begin`, `write_table` per table
   and `finish` or `abort`; UI windows get `draw` once per frame while
   open.
5. **Stop.** `stop` when a feature is disabled: it stops its threads and
   timers.
6. **Unload.** `shutdown(instance)` after every export job ended and every
   timer stopped; then the library is closed.

## Threading (PLG-07)

| Function | Thread | Rule |
|---|---|---|
| entry point, `init`, `shutdown`, `start`, `stop` | main | may take time, but not long |
| `on_record`, technology rules from the pipeline | measurement thread of the radio | never block, no I/O, no UI; radios run in parallel |
| technology rules (`compute_c1`, `reselect`, ...) | any | pure: no state, thread-safe |
| exporter functions | worker | one job is used from one thread at a time |
| `on_timer` | worker | return quickly |
| UI functions (`draw`, `menu_selected`, ...) | UI (main) | never block |

Host services (`yantra_host_api`) are thread-safe and do not block. A
service called where it is not allowed returns `YANTRA_ERR_BUSY`.

## Data model (PLG-05, PLG-12)

A plugin describes its data with schemas (`yantra_schema`): an id, a
kind, a version and a list of fields (`yantra_field_def`). Each field has
a machine name, a type (BOOL, INT, UINT, DOUBLE, STRING, ENUM), exactly
one data category, a unit, a label and a help text, and flags:

- `YANTRA_FIELD_KEY`: part of the cell identity (neither OPTIONAL nor
  DOUBLE). A `YANTRA_RECORD_NO_SYNC` record (level only) may leave its
  KEY fields empty; every other non-OPTIONAL field is always set.
- `YANTRA_FIELD_DEFAULT`: shown by default.
- `YANTRA_FIELD_OPTIONAL`: may be absent.
- `YANTRA_FIELD_VERIFY`: compared with the cell database (DB-04).

LEVEL fields are in dBm (a level difference such as C1 in dB): Yantra
calibrates radio levels before anyone sees them. QUALITY fields need a
unit (`"1"` when dimensionless).

Records (`yantra_record`) are a time stamp, flags and one value per
field. Yantra stores every record with its provenance
(`yantra_provenance`: technology id, plugin id and version, schema id and
version), so a file made with a plugin opens read-only in generic views
when that plugin is missing or older.

A schema changes compatibly by appending non-KEY fields and raising its
version; existing fields keep their name, type, unit, category and KEY
flag.

### Radio records and technology schemas

Radio plugins deliver records under the radio API. The link is data
only: the technology plugin's `technology_id` equals the radio's
`yr_descriptor::technology_id`, and each radio schema has a technology
schema with the same id and kind, the same field names and types, and
every enum value. A radio may report levels in dBFS; the technology
schema says dBm. Technology fields a radio does not deliver are
OPTIONAL. Yantra checks this when it connects a radio
(`yantra::plugin::check_radio_schema` in the core).

## Scope boundary (PLG-08)

Yantra measures broadcast and system information, levels and quality.
The API has no place for subscriber identities or addresses, messages,
speech, key material or raw signal data (IQ, samples, bursts, PDUs):

- every field has one of the categories SYSTEM_INFO, LEVEL, QUALITY,
  DEVICE or PLAN_POSITION, and a plugin declares the categories it uses;
- field and schema names that name such data (for example `issi`,
  `sds_text`, `iq_samples`) are refused;
- string values are at most `YANTRA_MAX_STRING_VALUE_BYTES` (255) bytes
  of UTF-8; a longer record is refused with `YANTRA_ERR_SCOPE`.

## Errors

Functions return `yantra_status`: `YANTRA_OK` (0) or a negative
`YANTRA_ERR_*` code, with the same values as the radio API. Plugins never
let a C++ exception cross the boundary (`yantra::plugin_api::guarded`
turns one into a status). Yantra logs every failure with the plugin's
name.

## Settings, logging and notifications

- `settings_get` / `settings_set` store small text values per plugin
  (main thread for `settings_set`).
- `log` writes to Yantra's log with the plugin's name; `notify` shows a
  short message to the user.
- `monotonic_ns` and `now` give time stamps; `timer_start` /
  `timer_stop` drive `on_timer`.
- `emit_record` adds a RECORDING record of a feature to the measurement.

## User interface (PLG-06)

The UI interface is toolkit-neutral and immediate-mode: a plugin declares
its windows (`yantra_ui_window_def`) and menu entries
(`yantra_ui_menu_item`), and its `draw` callback describes a window's
content each frame through the host's drawing table (`yantra_ui_api`:
headings, values, buttons, inputs, sections, plots and tables). Yantra
draws it in its own style. Every control has a tooltip, and a disabled
control says why (DOC-07). Texts are English; translations to Dutch,
French and German come in the descriptor's `yantra_translation` table,
keyed by the English text (UI-12).

## Versioning (PLG-04)

The API version is `YANTRA_PLUGIN_API_VERSION_MAJOR.MINOR`, here 1.0.

- A plugin loads when its major equals Yantra's and its minor is not
  newer than Yantra's.
- A minor version only adds: fields appended to a versioned struct,
  new functions at the end of a table, new constants. Every versioned
  struct starts with `struct_size`; use `YANTRA_HAS_FIELD` before reading
  a field newer than 1.0 from a struct the other side filled. Yantra
  reads an older plugin's struct with the missing fields as zero.
- A major version may change anything except the frozen descriptor
  prefix, so Yantra can always say why a plugin does not load.

## Licence (PLG-10)

A plugin runs inside Yantra and forms one program with it. Yantra is
GPL-3.0-or-later, so a plugin declares a GPL-3.0-compatible licence as an
SPDX expression in `licence` (for example `GPL-3.0-or-later` or `MIT`).
Plugins with another or no licence are not loaded.

## C++ helpers

`yantra/plugin.hpp` (header-only, C++20) adds `refusal_reason`,
`copy_text` (the buffer rules), `guarded` (no exceptions across the
boundary), `status_name` and `version_text`. Nothing in it crosses the
ABI.
