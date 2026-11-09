<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Stored interpolation maps (project table `interpolations`)

A ray-tracing interpolation of a floor (IND-40..45) is stored in the
project file, one row of the table `interpolations` per floor and key
(project schema 6, [PROJECT.md](PROJECT.md) "`interpolations`"). A row
holds three parts:

1. the grid columns and the `cells` BLOB: the map itself;
2. `parameters`: JSON text, what the map was made from (the request and
   the model parameters before and after the fit);
3. `result`: JSON text, what the fit found.

Plus the fingerprints `building_sha256`, `points_sha256` and
`settings_sha256`, from which Yantra shows a map as stale when its inputs
change (IND-45). There is no separate map file: maps are shown, exported
as pictures ([EXPORT_FILES.md](EXPORT_FILES.md)) and put in reports.

Code: `core/include/yantra/core/survey/interpolation.h` (`Interpolation`, `store`,
`stored_interpolations`, `to_stored`, `from_stored`),
`core/src/survey/interpolation.cpp` (`to_stored`, `from_stored`),
`core/include/yantra/core/raytrace/` (the option and parameter
structures). The model: [PROPAGATION.md](../PROPAGATION.md).

## Version

The JSON parts carry `"format": 1` (an integer, the version of the
parameters/result JSON). A row whose `parameters` or `result` has another
`format`, misses a member, or whose BLOB size does not match the grid is
refused when read ("damaged" project error naming the result id); other
rows still load. A change of these members raises the number and adds a
project schema step ([PROJECT.md](PROJECT.md) "Identification and
version"). Unlike most JSON formats of Yantra, every member listed here
is required.

## The grid and `cells`

| Column | Meaning |
| --- | --- |
| `origin_x`, `origin_y` | corner of the grid (smallest x and y), plan-frame metres of the floor (x right, y down as on the plan) |
| `cell_m` | edge of a square grid cell, metres (> 0) |
| `cols`, `rows` | grid size (> 0; at most 16 000 000 cells) |
| `cells` | `cols x rows x 12` bytes |

Cell `(col, row)` covers x from `origin_x + col x cell_m` and y from
`origin_y + row x cell_m`, one `cell_m` each; its index is
`row x cols + col` (row by row, from the origin). Each cell is 12 bytes:
three IEEE 754 single-precision floats, little-endian:

| Bytes | Value |
| --- | --- |
| 0..3 | the map value in dBm: the fitted model plus the interpolated residual (IND-41); `-inf` where no source reaches |
| 4..7 | the fitted model alone in dBm; `-inf` where no source reaches |
| 8..11 | confidence 0..1 (IND-43); 0 outside the floor outline |

Reading the map in Python:

```python
import sqlite3, struct
db = sqlite3.connect("survey.yantra")
cols, rows, blob = db.execute("SELECT cols, rows, cells FROM interpolations WHERE id = 1").fetchone()
cells = [struct.unpack_from("<3f", blob, 12 * i) for i in range(cols * rows)]  # (value, model, confidence)
```

## `key`

What the map is for, unique per floor:
`<value>|<carrier>[:<frequency_hz>][|device:<device>]|<group>`, with
value `level`, `c1` or `quality`, carrier `serving`, `strongest` or
`frequency` (then the frequency in Hz follows), the device key when the
metric names one, and the fitted source group (`cell:<id>`,
`antenna:<id>`) or `auto`. Example: `level|serving|cell:cell-1`.

## `parameters`

| Member | Content |
| --- | --- |
| `format` | `1` |
| `request` | the request, below |
| `frequency_hz` | the frequency the model ran at, Hz |
| `start_params` | model parameters before the fit, below |
| `params` | model parameters after the fit |

`request`:

| Member | Content |
| --- | --- |
| `floor_id` | the floor |
| `metric` | `carrier` (`serving`, `strongest`, `frequency`), `value` (`level`, `c1`, `quality`), `frequency_hz` (integer, 0 unless `frequency`), `quality_field`, `device` (`""`: any), `failed_cells` (PP-03: list of `{"frequency_hz", "cell"}`) |
| `sessions` | the session ids whose points were used (`[]`: all of the floor) |
| `frequency_hz`, `grid_m` | overrides, or `null` (the cells' carrier, the project's grid) |
| `fit` | `eirp` (`unknown`, `all`, `none`), `eirp_dbm` `[lo, hi]` (default `[-30, 60]`), `fit_exponent` (true), `exponent` `[1.5, 6]`, `fit_wall_classes` (true), `wall_class_bounds` (class id to `[lo, hi]`), `fit_floor_loss` (true), `floor_loss_db` `[0, 60]`, `max_iterations` (200), `group_key` (text or `null`: strongest group) |
| `residuals` | `radius_m` (10), `power` (2), `walls_limit` (true), `full_confidence_m` (2), `zero_confidence_m` (10) |
| `sources` | estimated-source search (IND-42): `max_sources` (3), `min_improvement_db` (1), `step_m` (0: automatic), `height_m` (2.7), `eirp_dbm` `[-30, 60]`, `one_cell` (true) |

Model parameters (`start_params`, `params`): `frequency_hz` (or `null`),
`grid_m`, `max_reflections`, `rx_height_m`, `other_floors`,
`path_loss_exponent`, `reference_distance_m`, `reflection_loss_db`,
`floor_loss_db` (or `null`: the building's slabs), `wall_class_loss_db`
(class id to dB), `antenna_eirp_dbm` (antenna id to dBm),
`default_eirp_dbm`. The project-wide settings they start from are the
`propagation` table ([PROJECT.md](PROJECT.md)).

## `result`

| Member | Content |
| --- | --- |
| `format` | `1` |
| `label` | `interpolated`, or `interpolated, estimated sources` (IND-42); shown with the map and in reports |
| `group_key` | the source group fitted |
| `quality` | `points` (used), `rms_db`, `mean_db`, `max_abs_db` (measured minus model after the fit), `start_rms_db` (before) |
| `fitted` | one per fitted parameter: `kind` (`eirp`, `exponent`, `wall_class`, `floor_loss`), `id` (antenna or class id, `""` otherwise), `start`, `value`, `bounds` `[lo, hi]`, `at_bound` |
| `not_fitted` | texts: parameters left at their start value and why, e.g. `wall class 'glass': no measured point behind such a wall` |
| `iterations`, `stop_reason` | the solver's run |
| `estimated_sources` | true when sources were added (IND-42) |
| `sources` | the estimated sources: `id`, `name`, `x`, `y` (plan-frame metres), `height_m`, `eirp_dbm` (or `null`), `cell` |
| `floor_cells` | grid cells inside the floor outline |
| `points` | the measured points used, each `[session_id, sequence, x_m, y_m, measured_dbm, model_dbm, residual_db]` |

## Example

The JSON parts of one stored map (shortened lists marked `...` are not
part of the format):

```json
{"format": 1,
 "request": {"floor_id": "floor-1",
             "metric": {"carrier": "serving", "value": "level", "frequency_hz": 0, "quality_field": "",
                        "device": "", "failed_cells": []},
             "sessions": [], "frequency_hz": null, "grid_m": null,
             "fit": {"eirp": "unknown", "eirp_dbm": [-30, 60], "fit_exponent": true, "exponent": [1.5, 6],
                     "fit_wall_classes": true, "wall_class_bounds": {}, "fit_floor_loss": true,
                     "floor_loss_db": [0, 60], "max_iterations": 200, "group_key": "cell:cell-1"},
             "residuals": {"radius_m": 10, "power": 2, "walls_limit": true,
                           "full_confidence_m": 2, "zero_confidence_m": 10},
             "sources": {"max_sources": 3, "min_improvement_db": 1, "step_m": 0, "height_m": 2.7,
                         "eirp_dbm": [-30, 60], "one_cell": true}},
 "frequency_hz": 390012500,
 "start_params": {"frequency_hz": 390012500, "grid_m": 0.25, "max_reflections": 2, "rx_height_m": 1.5,
                  "other_floors": true, "path_loss_exponent": 2, "reference_distance_m": 1,
                  "reflection_loss_db": 6, "floor_loss_db": null,
                  "wall_class_loss_db": {"concrete": 12.6}, "antenna_eirp_dbm": {"antenna-1": 10},
                  "default_eirp_dbm": 10},
 "params": {"frequency_hz": 390012500, "grid_m": 0.25, "max_reflections": 2, "rx_height_m": 1.5,
            "other_floors": true, "path_loss_exponent": 2.31, "reference_distance_m": 1,
            "reflection_loss_db": 6, "floor_loss_db": null,
            "wall_class_loss_db": {"concrete": 10.9}, "antenna_eirp_dbm": {"antenna-1": 10},
            "default_eirp_dbm": 10}}
```

```json
{"format": 1, "label": "interpolated", "group_key": "cell:cell-1",
 "quality": {"points": 48, "rms_db": 1.4, "mean_db": 0.02, "max_abs_db": 3.9, "start_rms_db": 4.8},
 "fitted": [{"kind": "exponent", "id": "", "start": 2, "value": 2.31, "bounds": [1.5, 6], "at_bound": false},
            {"kind": "wall_class", "id": "concrete", "start": 12.6, "value": 10.9, "bounds": [0, 35.2], "at_bound": false}],
 "not_fitted": [],
 "iterations": 14, "stop_reason": "converged: step below 1e-9", "estimated_sources": false, "sources": [],
 "floor_cells": 3584,
 "points": [[1, 1, 1.4, 7.0, -61.2, -60.5, -0.7], [1, 2, 2.1, 7.0, -62.0, -61.1, -0.9], "..."]}
```
