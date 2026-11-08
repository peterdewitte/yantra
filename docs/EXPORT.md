<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Export

How a floor's measured points are exported as CSV, DXF and an image, and
how the recordings of a folder tree are exported in one batch (EXP-01,
EXP-03, EXP-04, EXP-08, PLG-02, TST-33). Code: `core/export/` (namespace
`yantra::exports`). The export and report dialogs of the app use these
APIs; the reports (EXP-05, EXP-06) are described in [REPORT.md](REPORT.md).

## Rows: the project window's points (IND-30)

`floor_table(project, floor, TableOptions)` (table.h) gives one row per
point of the floor's shared point list (`survey::point_list`,
[SURVEY.md](SURVEY.md)): the same count, order, plan positions, metres,
values and colours as the project window, for the same metric, colour
scale and sessions. Each row adds what the stored sample of the point
holds:

- the serving carrier: level and unit, C1, quality values, frequency, the
  key fields of its cell identity (the technology's cell schema, e.g.
  `la`, `colour_code`; the core knows no technology), the channel number
  when `TableOptions.technologies` is given;
- the neighbours: the measured neighbours by level (strongest first), then
  the serving cell's neighbour-list entries by entry number;
- `serving_change`: the serving cell differs from the previous point of
  the same session (`survey::serving_changes`);
- `marker`: the markers of the session's recording files (REC-06), each on
  the first row at or after its time (the last row for a later marker),
  as `M<n> <label>`, several joined with `; `;
- `antenna_correction_db`: the recording header's antenna entry of the
  serving radio, else its entry for all radios: -gain + cable loss +
  attenuator (HW-09). Unknown without a recording.

## CSV (EXP-01)

`write_csv(path, rows, CsvOptions)` (csv.h). RFC 4180 text, a header line
(optional), one line per row.

| Option | Values |
| --- | --- |
| `columns` | any order of the column ids below, each once |
| `separator` | `,` `;` tab `\|` |
| `local_time` | false: `time_utc` `2026-10-26T07:33:21.000Z`; true: `time_local` with its offset `+01:00` |
| `neighbours` | 0..32: neighbour columns `nb1_*`..`nbN_*` |
| `cell_fields`, `quality_fields` | the cell identity and quality columns; empty: every field found in the rows, in first-seen order |

| Column id | CSV columns |
| --- | --- |
| `time` | `time_utc` or `time_local` |
| `floor` | `floor` (name) |
| `position` | `x_m`, `y_m` (metres, plan axes) |
| `plan_position` | `x_plan`, `y_plan` (plan units) |
| `cell` | one per cell field |
| `channel`, `frequency` | `channel`, `frequency_hz` |
| `level` | `rssi`, `rssi_unit` |
| `c1` | `c1_db` |
| `quality` | one per quality field |
| `serving_change` | `serving_change` (0/1) |
| `neighbours` | `nb<i>_<cell field>`, `nb<i>_channel`, `nb<i>_frequency_hz` (with `frequency`), `nb<i>_rssi`, `nb<i>_c1_db` |
| `marker` | `marker` (empty when none) |
| `antenna_correction` | `antenna_correction_db` |
| `label`, `kind`, `session` | point label, `walk`/`manual`/`simulated`/`cycle`, session id |

Every missing value is `-999` (`kInvalidValue`). Numbers are written in
their shortest form that reads back to the same double, so positions and
levels survive a round trip exactly (TST-33). `read_csv` reads the text
back (tests and tools).

## DXF (EXP-03)

`floor_drawing(project, table, DxfOptions)` and `write_dxf` (dxf.h),
Yantra's own writer: AutoCAD R12 ASCII (`AC1009`), units metres, x to the
right and y up (y = -plan y in metres, so the drawing is not mirrored). A
floor without a scale is refused. Layers:

| Layer | Content |
| --- | --- |
| `POINTS_<i>` | points in colour band i of the scale (1 = lowest) |
| `POINTS_OUTSIDE`, `POINTS_NO_VALUE` | points outside every band, or without a value |
| `VALUES` | the value of each point as text (optional) |
| `WALLS_<CLASS>` | each wall as a polyline, per wall class |
| `ANTENNAS`, `ANTENNA_NAMES` | antenna positions and names |
| `LABEL` | an optional label (e.g. the demo banner) |

The layer descriptions (band limits, unit) are written as comments at the
top. `read_dxf` reads the entities back (TST-33).

## Floor image (EXP-08)

`floor_image(project, table, ImageOptions)` (image.h) draws the floor plan
(trimmed as in the project; a plan that cannot be rendered is left out
with a warning; a licence-restricted reference plan such as a MAK sheet
is never drawn here, only by the report from the user's checked cached
copy, see [REPORT.md](REPORT.md)), the
walls, every point in its colour, the antennas, a title, an optional
banner, and a legend with the count per band. `write_image_file` writes
PNG or JPEG (by extension) with stb_image_write. Text uses the Inter font
(`data/fonts/`, installed in `share/yantra/fonts/`), passed as a path;
without a font the text is left out with a warning. For the report's
interpolated map (IND-43) `ImageOptions::map` lays a grid of ARGB cells
over the plan rectangle it covers, each cell a flat square in its exact
colour (alpha blended), and `points = false` leaves the points out.

## Batch export (EXP-04)

`batch_export(BatchOptions)` (batch.h) exports every recording
(`*.yrec`, `*.yrec.zst`) below a folder, merged into one file or one file
per recording (`<output>/<session name>.<ext>`).

- Files are grouped by recording session (one recording may span several
  files) and ordered by file number. Other files are skipped with a
  warning.
- With a project: each recording is imported, exactly as the viewer
  imports it (`survey::import_recordings`), into a scratch copy of the
  project without its sessions (`Project::write_copy`); its rows are the
  project window's rows of that walk. The project itself is not changed;
  the copy is deleted afterwards. A recording that cannot be placed gives
  its cycles instead, with a warning.
- Without a project: one row per replayed measurement cycle, without a
  position (`-999`), the floor from the recording's floor lines.
- Output: CSV with the chosen columns, or an exporter plugin's format.

## Exporter plugins (PLG-02)

`exporter_formats(loader)` (plugin_export.h) lists the formats of the
enabled exporter plugins that have `YANTRA_EXPORT_BATCH`; these are the
formats offered in batch export. `run_exporter` hands the rows to the
plugin through the public C API 1.0 (`begin`, `write_table`, `finish`,
`abort` on failure): one table with schema `yantra_export_rows` (version
1), one optional field per CSV column with its type and unit
(`rows_schema`), provenance plugin `yantra`. A `-999` value is absent.
The test exporter `tests/export/plugin/rows_exporter.c` is an example.
