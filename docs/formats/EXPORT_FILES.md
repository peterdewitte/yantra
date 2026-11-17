<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Export files: CSV, DXF, floor images, batch, reports

The files Yantra writes for other programs (EXP-01..05, EXP-08). The
options, the dialog and the plugin route are in
[EXPORT.md](../EXPORT.md); the report engine in [REPORT.md](../REPORT.md).
This page fixes what the files look like. None of these files is read
back by Yantra in normal use (the readers `read_csv` and `read_dxf` exist
for tests, TST-33), so they carry no format line of their own; what
changes them is listed in the [CHANGELOG](../../CHANGELOG.md).

Code: `core/include/yantra/core/export/` (`csv.h`, `dxf.h`, `image.h`,
`batch.h`, `table.h`).

## Points CSV (EXP-01)

UTF-8 text, RFC 4180: one header line (can be switched off), then one
line per measured point of a floor, in the order of the project window's
point list (IND-30). Lines end with `\n`. The separator is `,` (default),
`;`, a tab or `|`; a value holding the separator, a quote or a line break
is quoted with `"` (quotes doubled).

| Column(s) | Content |
| --- | --- |
| `time_utc` | `YYYY-MM-DDTHH:MM:SS.mmmZ` (UTC); or `time_local` with the PC's offset, `2026-11-05T10:45:01.000+01:00` |
| `floor` | floor name |
| `x_m`, `y_m` | plan-frame metres (x right, y down as on the plan) |
| `x_plan`, `y_plan` | plan units (optional) |
| one column per cell identity field | the serving cell's key fields as the technology names them (e.g. `mcc`, `mnc`, `la`) |
| `channel`, `frequency_hz` | serving channel (technology raster), carrier in Hz (optional) |
| `rssi`, `rssi_unit` | serving level and its unit (`dBm`; `dBFS` uncalibrated) |
| `c1_db` | path-loss criterion C1 |
| one column per quality field | e.g. `mer_db` (technology fields) |
| `serving_change` | `1` when the serving cell differs from the previous point of the session, else `0` |
| `nb<i>_<cell field>`, `nb<i>_channel`, (`nb<i>_frequency_hz`,) `nb<i>_rssi`, `nb<i>_c1_db` | neighbour i = 1..N (N = 0..32, default 6): measured neighbours by level, then broadcast list entries |
| `marker` | recording markers at that point, `M<n> <label>`, several joined by `; ` |
| `antenna_correction_db` | -gain + cable loss + attenuator of the recording (HW-09) |
| `label`, `kind`, `session` | point label, `walk`/`manual`/`simulated`/`cycle`, session id (optional) |

Numbers are written in the shortest form that reads back to the same
double. A missing or invalid value is `-999`. The column groups and their
order are chosen in the export dialog; each group at most once.

Example: the default columns with one neighbour (N = 1), cell fields
`mcc`, `mnc`, `la` and the quality field `mer_db`:

<!-- example: csv:points -->
```csv
time_utc,floor,x_m,y_m,mcc,mnc,la,channel,rssi,rssi_unit,c1_db,mer_db,serving_change,nb1_mcc,nb1_mnc,nb1_la,nb1_channel,nb1_rssi,nb1_c1_db,marker,antenna_correction_db
2026-11-05T09:45:01.000Z,Ground floor,1.4,7,901,1,100,33600,-61.2,dBm,51.8,28.5,0,901,1,101,33608,-78.4,34.6,M1 stairs,-2
2026-11-05T09:45:02.000Z,Ground floor,2.1,7,901,1,100,33600,-91.5,dBm,21.5,17,0,-999,-999,-999,-999,-999,-999,,-2
```

The cell database CSV (`#yantra-cells,1`, DB-02) and the difference log
of the verification (DB-04) are on [CELLS.md](CELLS.md).

## DXF drawing (EXP-03)

AutoCAD R12 ASCII DXF (`$ACADVER` `AC1009`), written by Yantra's own
code. One floor per file; a floor without a scale is refused. Lines end
with `\n`; every group is a code line and a value line.

- Units: metres in the floor's plan frame with y turned up: drawing
  `(x, y)` = plan metres `(x, -y)`, so the drawing is not mirrored. `z`
  is always 0.
- Comments (group 999) at the top: the project and floor name, the units,
  the metric and the point count, the optional label, then one
  `<layer>: <description>` per layer (band limits and unit).
- `HEADER` with `$EXTMIN`/`$EXTMAX`, `TABLES` with one `LAYER` entry per
  layer (colour = nearest AutoCAD colour index of the band colour,
  linetype `CONTINUOUS`), `ENTITIES`, `EOF`.

| Layer | Entities |
| --- | --- |
| `POINTS_<i>` | `POINT` per measured point in colour band i (1 = the lowest band) |
| `POINTS_OUTSIDE`, `POINTS_NO_VALUE` | `POINT`s outside every band, or without a value |
| `VALUES` | `TEXT` with the point's value, rounded to 0.1 (optional, on by default) |
| `LABEL` | `TEXT` with the optional label (e.g. the demo label, DEMO-07) |

Text height is 0.2 m by default (label 0.4 m).

<!-- example: dxf -->
```dxf
999
Town hall survey - Ground floor
999
units: metres in the floor's plan frame, x right, y up (y = -plan y)
999
points: Level, serving carrier, 2 measured points
999
POINTS_1: >= -95 dBm
999
POINTS_2: >= -85 dBm
999
POINTS_OUTSIDE: outside every band
999
POINTS_NO_VALUE: no value
999
VALUES: point values (dBm)
0
SECTION
2
HEADER
9
$ACADVER
1
AC1009
9
$EXTMIN
10
1.4
20
-7
30
0
9
$EXTMAX
10
2.1
20
-7
30
0
0
ENDSEC
0
SECTION
2
TABLES
0
TABLE
2
LAYER
70
5
0
LAYER
2
POINTS_1
70
0
62
2
6
CONTINUOUS
0
LAYER
2
POINTS_2
70
0
62
3
6
CONTINUOUS
0
LAYER
2
POINTS_OUTSIDE
70
0
62
1
6
CONTINUOUS
0
LAYER
2
POINTS_NO_VALUE
70
0
62
8
6
CONTINUOUS
0
LAYER
2
VALUES
70
0
62
7
6
CONTINUOUS
0
ENDTAB
0
ENDSEC
0
SECTION
2
ENTITIES
0
POINT
8
POINTS_2
10
1.4
20
-7
30
0
0
POINT
8
POINTS_1
10
2.1
20
-7
30
0
0
TEXT
8
VALUES
10
1.5
20
-6.9
30
0
40
0.2
1
-61.2
0
TEXT
8
VALUES
10
2.2
20
-6.9
30
0
40
0.2
1
-91.5
0
ENDSEC
0
EOF
```

## Floor image (EXP-08)

A picture of one floor: the trimmed plan, the points in their colours,
a title, an optional banner and a legend with the count per
band; or, for reports, an interpolated map (IND-43) laid over the plan.
PNG (8-bit RGBA) or JPEG (quality 90), chosen by the file extension
(`.png`, `.jpg`, `.jpeg`); written with stb_image_write. The size is
chosen in the dialog. A licence-restricted plan (DEMO-12) is never drawn
into an exported image.

## Batch export (EXP-04)

Every recording (`*.yrec`, `*.yrec.zst`, [RECORDING.md](RECORDING.md))
below a folder, exported as the points CSV above (or an exporter plugin's
format, PLG-02):

- separate: one file per recording session, `<output folder>/<session
  name>.csv` (or the plugin format's extension);
- merged: one file with the rows of every session, in session order.

With a project the rows are the project window's points of each walk
(the recordings are imported into a scratch copy
`yantra-batch-<process>-<n>.yantra`, deleted afterwards); without one,
one row per replayed measurement cycle without a position (`-999`).

## Exporter plugins (PLG-02)

An exporter plugin gets the rows through the plugin API as one table of
schema `yantra_export_rows` version 1 (one optional field per CSV column,
with type and unit; `-999` values are absent) and writes its own format
([EXPORT.md](../EXPORT.md) "Exporter plugins", [PLUGIN_API.md](../PLUGIN_API.md)).
Such formats are documented by their plugin.

## Reports (EXP-05)

The coverage and acceptance report as PDF (libharu, fonts embedded as
subsets, PDF 1.x with document information) and as one self-contained
HTML file (inline CSS, pictures as `data:` URIs) with the same content;
the HTML file has the PDF's name with `.html`. Creation time is the
caller's clock or `SOURCE_DATE_EPOCH`, so a report can be rebuilt byte
for byte. Layout and content: [REPORT.md](../REPORT.md); sample:
[docs/samples](../samples).
