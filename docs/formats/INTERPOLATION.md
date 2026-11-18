<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Stored interpolation maps (project table `interpolations`)

The point interpolation of a floor (IND-41..45, [INTERPOLATION.md](../INTERPOLATION.md))
is stored in the project file, one row of the table `interpolations` per
floor and key (project schema 8 and later, [PROJECT.md](PROJECT.md)
"`interpolations`"). A row holds three parts:

1. the grid columns and the `cells` BLOB: the map itself;
2. `parameters`: JSON text, what the map was made from (the request and
   the settings);
3. `result`: JSON text, the measured points it used.

Plus the fingerprints `building_sha256`, `points_sha256` and
`settings_sha256`, from which Yantra shows a map as stale when its inputs
change (IND-45). There is no separate map file: maps are shown, exported
as pictures ([EXPORT_FILES.md](EXPORT_FILES.md)) and put in reports.

Code: `core/include/yantra/core/survey/interpolation.h` (`Interpolation`, `store`,
`stored_interpolations`, `to_stored`, `from_stored`),
`core/src/survey/interpolation.cpp`.

## Version

The JSON parts carry `"format": 2` (an integer, the version of the
parameters/result JSON; format 1 was the ray-tracing map of Yantra 0.2 (spec 0.3),
dropped by project schema 8, IND-46). A row whose `parameters` or
`result` has another `format`, misses a member, or whose BLOB size does
not match the grid is refused when read ("damaged" project error naming
the result id); other rows still load. A change of these members raises
the number and adds a project schema step ([PROJECT.md](PROJECT.md)
"Identification and version"). Unlike most JSON formats of Yantra, every
member listed here is required.

## The grid and `cells`

| Column | Meaning |
| --- | --- |
| `origin_x`, `origin_y` | corner of the grid (smallest x and y), plan-frame metres of the floor (x right, y down as on the plan) |
| `cell_m` | edge of a square grid cell, metres (> 0) |
| `cols`, `rows` | grid size (> 0; at most 16 000 000 cells) |
| `cells` | `cols x rows x 8` bytes |

Cell `(col, row)` covers x from `origin_x + col x cell_m` and y from
`origin_y + row x cell_m`, one `cell_m` each; its index is
`row x cols + col` (row by row, from the origin). Each cell is 8 bytes:
two IEEE 754 single-precision floats, little-endian:

| Bytes | Value |
| --- | --- |
| 0..3 | the map value in dBm (IND-41); `-inf` where no measured point is within the search radius |
| 4..7 | confidence 0..1 (IND-43); 0 outside the floor outline |

Which cells hold a measured point follows from `result.points` and the
grid (it is not stored twice).

Reading the map in Python:

```python
import sqlite3, struct
db = sqlite3.connect("survey.yantra")
cols, rows, blob = db.execute("SELECT cols, rows, cells FROM interpolations WHERE id = 1").fetchone()
cells = [struct.unpack_from("<2f", blob, 8 * i) for i in range(cols * rows)]  # (value, confidence)
```

## `key`

What the map is for, unique per floor:
`<value>|<carrier>[:<frequency_hz>][|device:<device>]`, with value
`level`, `c1` or `quality`, carrier `serving`, `strongest` or `frequency`
(then the frequency in Hz follows), and the device key when the metric
names one. Example: `level|serving`.

## `parameters`

| Member | Content |
| --- | --- |
| `format` | `2` |
| `request` | the request, below |
| `params` | the settings the map was made with: `grid_m`, `power`, `radius_m`, `full_confidence_m` |

`request`:

| Member | Content |
| --- | --- |
| `floor_id` | the floor |
| `metric` | `carrier` (`serving`, `strongest`, `frequency`), `value` (`level`, `c1`, `quality`), `frequency_hz` (integer, 0 unless `frequency`), `quality_field`, `device` (`""`: any), `failed_cells` (PP-03: list of `{"frequency_hz", "cell"}`) |
| `sessions` | the session ids whose points were used (`[]`: all of the floor) |
| `settings` | settings of this request, or `null`: the project's (`interpolation_settings`, [PROJECT.md](PROJECT.md)) |

## `result`

| Member | Content |
| --- | --- |
| `format` | `2` |
| `label` | `interpolated`; shown with the map and in reports |
| `floor_cells` | grid cells inside the floor outline |
| `points` | the measured points used, each `[session_id, sequence, x_m, y_m, measured_dbm]` |

## Example

The JSON parts of one stored map (shortened lists marked `...` are not
part of the format):

```json
{"format": 2,
 "request": {"floor_id": "floor-1",
             "metric": {"carrier": "serving", "value": "level", "frequency_hz": 0, "quality_field": "",
                        "device": "", "failed_cells": []},
             "sessions": [], "settings": null},
 "params": {"grid_m": 0.5, "power": 2, "radius_m": 10, "full_confidence_m": 2}}
```

```json
{"format": 2, "label": "interpolated", "floor_cells": 3584,
 "points": [[1, 1, 1.4, 7.0, -61.2], [1, 2, 2.1, 7.0, -62.0], "..."]}
```
