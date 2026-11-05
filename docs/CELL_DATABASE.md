<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Cell database

The site/cell database (DB-01), its CSV import and export (DB-02),
auto-learn (DB-03), BS parameter verification with the difference log
(DB-04), the neighbour cross-check (DB-05) and the cell reselection
simulator (DB-08). Code: `core/celldb/` (namespace `yantra::celldb`);
tests: `tests/celldb/`, `tests/project/test_project_celldb.cpp` and,
against simulator walks, `tests/measure_host/test_measure_celldb.cpp`.

The core gives the fields no meaning of its own (PLG-01). What a cell is,
which fields identify it, how a neighbour-list entry names a cell, how a
received parameter is compared with a stored one and how reselection works
all come from the technology plugin (`yantra_technology_api` in
[PLUGIN_API.md](PLUGIN_API.md); for TETRA [plugins/TETRA.md](plugins/TETRA.md)).
`celldb::Technology` collects that part of a loaded plugin.

The editor (the "database editor" of DB-02), the colours of the
verification view and the subscriber-class calculator of DB-07 are UI
(phase 9); the channel/frequency part of DB-07 is the plugin's raster
(`Technology::channel_of_frequency`, `frequency_of_channel`). DB-06 (alias
database of SSI/GSSI) is out of Yantra's scope (no subscriber addresses)
and is not built.

## The model (DB-01)

A `CellDatabase` is a list of `CellEntry`. An entry is the project's cell
(`building::Cell`, IND-12: id, label, technology, schema, main carrier,
the identity fields) plus:

| Part | Meaning |
| --- | --- |
| `site_name`, `sector` | text |
| `azimuth_deg` | sector azimuth 0..<360 (degrees from plan north), optional; indoor only, no position |
| `colour` | graph colour `#rrggbb` |
| `notes` | text, up to 4096 bytes |
| `origin` | `manual`, `import` (CSV) or `learned` (DB-03) |
| `plugin_version` | the plugin that imported or learned it |
| `parameters` | every other field by the cell schema's names: reselection parameters (TETRA `srt_db`, `srh_db`, `frt_db`, `frh_db`, `rxlev_access_min_dbm`, `ms_txpwr_max_cell_dbm`, `access_parameter_dbm`), `subscriber_class`, ... Integer, real or text |
| `neighbours` | up to 31 references; each names a cell by cell-schema field names (TETRA `frequency_hz` and `la`, DB-01 "LA/channel") |

The fields of an entry (`CellEntry::fields()`) are the union of
`frequency_hz` (the main carrier), the identity and the parameters; the
identity fields `mcc`, `mnc`, `la`, `colour_code` are kept in the cell's
identity, the others as parameters.

**Ids.** An entry added without an id gets `cell-<n>`. In a project the
cell database *is* the project's cells (one list, schema 5,
[formats/PROJECT.md](formats/PROJECT.md)): `Project::cell_database()` and
`Project::set_cell_database()`. A cell that an antenna serves cannot be
dropped from it.

**Match fields** (DB-01 "identified by LA, by channel, or by both"): per
technology the database names the fields a received cell is paired with
an entry by (`set_match_fields("tetra", {"la"})`); without them the cell
schema's KEY fields are used (TETRA: `frequency_hz`, `mcc`, `mnc`, `la`).

**Validation** refuses: names that are not identifiers or that name data
outside Yantra's scope (PLG-08, e.g. `ssi`), text that is not UTF-8 or
too long, non-finite numbers, an azimuth outside 0..<360, a colour that
is not `#rrggbb`, more than 31 neighbours, an empty neighbour reference.
`merge()` adds or updates several entries all or nothing.

**Shared database.** Outside a project the database is a CSV file (below);
`fingerprint()` (SHA-256 of its canonical CSV) and `describe()` give the
text for the recording header's `cell_db` field (REC-03).

## CSV (DB-02, TST-33)

```
#yantra-cells,1
id,label,technology,schema,site_name,sector,azimuth_deg,colour,notes,origin,plugin_version,frequency_hz,channel,mcc,mnc,la,colour_code,srt_db,neighbours.frequency_hz,neighbours.channel,neighbours.la
site-a,Site A,tetra,tetra_cell,Tower,1,120.0,#ff0000,,manual,0.2.2,390000000,3600,206,1,0x64,1,20,390100000|390300000,3604|3612,|0xC8
```

- The first line `#yantra-cells,1` names the format and its version; a
  higher version is refused, a file without it is read as version 1 (a
  file typed in a spreadsheet).
- Separator `,` or `;` (detected from the header); RFC 4180 quoting (also
  line breaks in notes); UTF-8, a byte order mark is skipped; CRLF on
  export.
- `frequency_hz` is the main carrier in Hz; `channel` is the technology's
  channel number, converted by its plugin (for TETRA the nominal carrier
  without the offset: give `frequency_hz` too for a carrier with an
  offset). Given both, they must agree.
- Every other column is a field by name (`field:<name>` for a name that is
  one of the columns above).
- `neighbours.<name>` columns hold the neighbour list: one value per
  neighbour, separated by `|`, aligned across the columns; an empty value
  means "not given" for that neighbour. `neighbours.channel` is converted
  like `channel`.
- Integers are read in decimal or hexadecimal (`0x12C`) in every column
  ("LA in hex or decimal"); the export writes the fields of
  `CsvOptions::hex_fields` in hexadecimal.
- A row that cannot be used (a value of the wrong kind, an unknown
  origin, a channel that is not a channel of the band, a frequency and a
  channel that do not agree, more values than columns, an id used by an
  earlier row, an invalid entry) is skipped and reported as
  `line N: <reason>`; a column whose name is not usable is ignored and
  reported. The whole file is refused when it has no header, a column
  twice, a quote that is not closed, a newer format version, more than
  100000 cells or more than 64 MiB.

Export, import and export again gives the same bytes; the round trip is
tested with every column, both separators, hex and the channel columns.

## Survey cells and neighbour lists

`Observations` collects from mapped records or measurement cycles
(`core/measure`) the cells received: a record of the plugin's cell schema
that is not NO_SYNC is a cell, identified by its KEY fields; its latest
fields, levels and whether it served are kept. Records of the neighbour
schema (radio API `entry_index`, `entry_count`) build the neighbour list
of the cell that served on that carrier; entries at or above the latest
`entry_count` are dropped.

A list entry becomes a reference by the plugin's links (TETRA:
`neighbour_frequency_hz` -> `frequency_hz`). The KEY fields without a
link are taken from the broadcasting cell (plugin.h `yantra_field_link`)
and marked **inherited**.

**Matching a reference to a cell.** A reference names a cell when every
named (not inherited) field it shares with the cell agrees and at least
one named field is shared; agreeing inherited fields only make the match
better. When two cells match equally well the reference is ambiguous.
Inherited fields are a guess (a TETRA neighbour may lie in another
location area, and D-NWRK-BROADCAST sends its LA only when it chooses to),
so they never refuse a match.

## Verification and the difference log (DB-04)

`verify(db, cells, technologies)` pairs each received cell with the
entries of its technology by the match fields: none -> *not in the
database*; more than one -> *ambiguous*. A paired cell is compared over the
KEY and VERIFY fields of the cell schema that the database uses:

| State | Meaning |
| --- | --- |
| `match` | received and stored agree (the plugin's `values_match`; TETRA compares dBm parameters sent as codes by their code) |
| `differs` | received and stored differ |
| `new` | received, not in the entry |
| `missing` | in the entry, not received |

The received neighbour list is compared with the entry's when both are
known: `match`, `new` (received only), `missing` (stored only). Entries
no received cell paired with are *not seen*.

The difference log is CSV with the header
`time_utc,technology,cell,identity,item,state,database,received`: one line
per difference (a field or neighbour that is not `match`, a cell not in
the database or ambiguous, optionally the entries not seen).
`DifferenceLog` appends to a file and writes each difference once: a line
equal to one already in the file (time aside) is skipped, also after the
file is opened again.

## Auto-learn (DB-03)

`learn(db, cells, technologies)` adds received cells that pair with no
entry (origin `learned`, the plugin's version, the KEY and VERIFY fields
received, the neighbour list when it was received), gives paired entries
the fields they lack and their neighbour list when they have none. It
never overwrites a value (a difference stays a difference for DB-04) and
leaves cells that pair with several entries alone. Learning twice changes
nothing.

## Cross-check (DB-05)

`cross_check(views)` checks every neighbour relation of a set of cells:
the database (`database_views`), a survey (`observed_views`) or both
(`combined_views`: the entries with the survey's lists where the survey
saw them, then the cells received that are in no entry).

| Finding | Meaning |
| --- | --- |
| `not_mutual` | A lists B, B's list is known and does not list A |
| `list_unknown` | A lists B, B's list is not known (never seen serving, an entry without a list) |
| `not_seen` | an entry of A's list names no known cell |
| `ambiguous` | an entry of A's list names more than one cell |

A survey only knows the lists of the cells that served, so a survey alone
gives `list_unknown` for the others; combined with the database it gives
the full check.

## Reselection simulator (DB-08)

`simulate(technology, serving, neighbours, ms_power)` puts one question to
the plugin's `reselect` (and `compute_c1` for the C1 of each cell): the
serving-cell state (TETRA: exclusive, superior, usable, improvable,
relinquishable, radio link failure), the neighbour it would reselect to
and the plugin's reason. With database entries the neighbours are the
entry's list resolved against the database (`neighbour_entries`); a cell
without a level gets -200 dBm.

`simulate_walk(db, technologies, steps, options)` follows a walk: levels
per entry over time (simulated, or from measurement cycles with
`walk_steps`: fresh, synced, calibrated samples of the cell schema whose
cell resolves to one entry), the serving cell starting at the strongest
(or `options.start`) and changing when the plugin chose the same
neighbour for `hold_s` seconds (0: at once). The plugin's rule has no
timers of its own (docs/plugins/TETRA.md).

## Hooks

- UI (phase 9): the database editor, the verification view (match/new/
  missing colours), the cross-check list, the reselection simulator form
  and the subscriber-class calculator (DB-07).
- Recording header `cell_db` (REC-03): `describe(db, name)`.
- Reports: the difference log and cross-check findings as tables.
