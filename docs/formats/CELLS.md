<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Cell database files (`yantra-cells`, version 1) and the difference log

The cell database (DB-01) lives in the project file (tables `cells`,
`cell_identity`, `cell_parameters`, `cell_neighbours`; project schema 5,
[PROJECT.md](PROJECT.md)). Outside a project it is exchanged as a CSV
file (DB-02): import, export, and the form of a shared database. The
verification (DB-04) writes a second CSV file, the difference log. The
model, matching and verification rules are in
[CELL_DATABASE.md](../CELL_DATABASE.md); this page is the file format.

Code: `core/include/yantra/core/celldb/csv.h` (`import_csv`,
`export_csv`, `read_csv_file`, `write_csv_file`),
`core/include/yantra/core/celldb/verify.h` (`log_header`, `log_csv`,
`DifferenceLog`).

## Cell database CSV

| Item | Value |
| --- | --- |
| Extension | `.csv` |
| First line | `#yantra-cells,1`: format and version (written on export; optional on import) |
| Header | the column names, in any order |
| Rows | one cell each |
| Encoding | UTF-8 (a byte order mark is skipped); RFC 4180 quoting, also line breaks in `notes` |
| Separator | `,` or `;`, detected from the header line; export writes `,` unless asked |
| Line ends | CRLF on export; LF or CRLF on import |
| Limits | 64 MiB, 100 000 cells, 31 neighbours per cell |

Version handling: a file without the first line is read as version 1 (a
table typed in a spreadsheet); a version above 1 is refused as made by a
newer Yantra. Export, import and export again give the same bytes.

| Column | Content |
| --- | --- |
| `id` | the cell's id in the database (required, unique) |
| `label` | display name |
| `technology`, `schema` | technology plugin id and its cell schema (e.g. `tetra`, `tetra_cell`); an import can give defaults for rows without them |
| `site_name`, `sector`, `azimuth_deg`, `colour`, `notes` | planning data (azimuth in degrees, colour `#rrggbb`) |
| `origin` | `manual`, `import` (default on import) or `learned` (DB-03) |
| `plugin_version` | version of the technology plugin the entry was made with |
| `frequency_hz` | main carrier, whole Hz |
| `channel` | the technology's channel number, converted to `frequency_hz` by its plugin; given both, they must agree |
| any other name | a technology field (identity or broadcast parameter, e.g. `mcc`, `mnc`, `la`, `colour_code`, `srt_db`); `field:<name>` for a field named like a column above |
| `neighbours.<field>` | the neighbour list: one value per neighbour, separated by `\|`, aligned across these columns; an empty value is "not given" for that neighbour; `neighbours.channel` is converted like `channel` |

Integers are read in decimal or hexadecimal (`0x64`) in every column; the
export writes the fields chosen as hexadecimal (e.g. `la`) with `0x`. A
row that cannot be used is skipped and reported as `line N: <reason>`; a
file without a header, with a column twice, an open quote or a newer
version is refused as a whole. A neighbour field that the broadcasting
cell also has (MCC, MNC, LA) may be left empty: it is taken from the cell
that broadcasts the list.

Example (two TETRA cells that list each other; a file read without the
TETRA plugin, so carriers are given in Hz):

<!-- example: yantra-cells -->
```csv
#yantra-cells,1
id,label,technology,schema,site_name,sector,azimuth_deg,colour,notes,origin,plugin_version,frequency_hz,mcc,mnc,la,colour_code,neighbours.frequency_hz,neighbours.la
das-ground,DAS ground floor,tetra,tetra_cell,Town hall,G,,#2a9d8f,Indoor DAS,manual,0.1.0,390012500,901,1,0x64,1,390112500,0x65
das-first,DAS first floor,tetra,tetra_cell,Town hall,1,,#e76f51,"Indoor DAS, first floor",manual,0.1.0,390112500,901,1,0x65,1,390012500,0x64
```

## Difference log (DB-04)

The verification of received cells against the database appends its
findings to a CSV file chosen by the user (`DifferenceLog`), one line per
difference. A new file starts with the header; a line equal to one
already in the file (the time aside) is not written again, also after the
file is opened again. RFC 4180 quoting, CRLF line ends, no version line
(the header is the format; a column added later goes at the end).

| Column | Content |
| --- | --- |
| `time_utc` | when the difference was seen, ISO 8601 UTC |
| `technology` | technology plugin id |
| `cell` | the database entry's display name (label, else site and sector, else id), or the received key |
| `identity` | the received cell's key: `name=value;...` of its KEY fields |
| `item` | `cell` (the cell as a whole), a field name, or `neighbour` |
| `state` | `differs`, `new` (received, not in the entry), `missing` (in the entry, not received); for `cell`: `not_in_database`, `ambiguous` or `not_seen` |
| `database` | the stored value; for `ambiguous` the candidate ids, space-separated; a neighbour as `field=value/field=value` |
| `received` | the received value, in the same form |

<!-- example: csv:difference-log -->
```csv
time_utc,technology,cell,identity,item,state,database,received
2026-11-05T09:45:00Z,tetra,DAS ground floor,frequency_hz=390012500;la=100;mcc=901;mnc=1,srt_db,differs,20,18
2026-11-05T09:45:00Z,tetra,DAS ground floor,frequency_hz=390012500;la=100;mcc=901;mnc=1,neighbour,new,,frequency_hz=390212500
2026-11-05T09:45:00Z,tetra,frequency_hz=390312500;la=103;mcc=901;mnc=1,frequency_hz=390312500;la=103;mcc=901;mnc=1,cell,not_in_database,,
```
