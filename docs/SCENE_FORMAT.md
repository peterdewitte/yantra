<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Scene JSON (`yantra-radio-scene`, version 1)

Reference of the scene format of RAD-36. A scene describes one floor for
a simulating radio plugin: its walls with their attenuation, the antennas
and the cells they serve. Yantra keeps the scene with its floor plans and
hands it to plugins with the `YR_CAP_SCENE` capability when they ask for it
through the host callback `get_scene` (RAD-18). Yantra never pushes it: the
plugin pulls it on start and again after a `YR_CONTEXT_SCENE_CHANGED`
notification.

Defined in `include/yantra/radio.h` (`YR_SCENE_FORMAT`, `YR_SCENE_VERSION`,
documentation of `get_scene`). Parser and writer:
`common/include/yantra/radios/scene.hpp` (library `yantra::radios_common`).
Example: `tests/fixtures/scenes/two_rooms_v1.json`. The radio test host
shows a scene to a plugin with `yantra-radio-host stream --scene FILE`;
the conformance kit has a built-in one.

## Example

```json
{
  "format": "yantra-radio-scene",
  "version": 1,
  "floor": { "id": "f1", "name": "Ground floor", "scale_set": true,
             "width_m": 20.0, "height_m": 10.0 },
  "wall_classes": [
    { "id": "concrete", "label": "Concrete", "attenuation_db": 12.0 } ],
  "walls": [
    { "x1_m": 10.0, "y1_m": 0.0, "x2_m": 10.0, "y2_m": 10.0,
      "class": "concrete", "attenuation_db": 12.0 } ],
  "cells": [
    { "id": "c1", "label": "Cell 1", "mcc": 901, "mnc": 1, "la": 100,
      "colour_code": 1, "carrier_hz": 392012500 } ],
  "antennas": [
    { "id": "a1", "cell": "c1", "x_m": 2.0, "y_m": 2.0, "height_m": 2.8,
      "eirp_dbm": 10.0, "pattern": { "type": "omni" } } ]
}
```

## Frame and units

- Positions are metres in the floor's **plan frame**: origin at the top
  left of the plan image, x to the right, y down. The same frame is used
  by the positions of `get_cursor` and `get_walk_position` and by the
  `sim_x_m`/`sim_y_m` fields of simulated records (RAD-19).
- Heights are metres above the floor; levels dB and dBm; frequencies Hz.
- Angles are degrees clockwise from the plan's -y axis ("up" on the plan).

## Members

All members listed are required unless marked optional; unknown members
are ignored at every level.

| Member | Type | Meaning |
|---|---|---|
| `format` | string | `"yantra-radio-scene"` |
| `version` | uint | `1` |
| `floor.id` | string, not empty | Floor id, the same as `yr_floor_plan_info::floor_id` |
| `floor.name` | string, optional | Floor name as shown (context text, never copied into records, RAD-19) |
| `floor.scale_set` | bool | The plan has a scale; without it there are no positions |
| `floor.width_m`, `floor.height_m` | number >= 0 | Plan size in metres (0 without a scale) |
| `wall_classes[]` | array | Wall classes with their default attenuation |
| `wall_classes[].id` | string, unique | Class id |
| `wall_classes[].label` | string, optional | Class name as shown |
| `wall_classes[].attenuation_db` | number >= 0 | Default attenuation of a wall of this class |
| `walls[]` | array | Wall segments |
| `walls[].x1_m` .. `y2_m` | number | End points |
| `walls[].class` | string, optional | `wall_classes[].id`; absent or `""` for a wall without a class |
| `walls[].attenuation_db` | number >= 0 | The effective attenuation (the class value unless overridden) |
| `cells[]` | array | Cells (identity and carrier) |
| `cells[].id` | string, unique | Cell id |
| `cells[].label` | string, optional | Cell name as shown |
| `cells[].mcc`, `mnc`, `la`, `colour_code` | uint | Identity as in the core record fields (RAD-21) |
| `cells[].carrier_hz` | uint > 0 | Main carrier |
| `antennas[]` | array | Antennas, each serving one cell |
| `antennas[].id` | string, unique | Antenna id |
| `antennas[].cell` | string | `cells[].id`; one cell may have several antennas (DAS) |
| `antennas[].x_m`, `y_m` | number | Position |
| `antennas[].height_m` | number | Height above the floor |
| `antennas[].eirp_dbm` | number | EIRP of this antenna in its main direction |
| `antennas[].pattern` | object | See below |

Patterns:

- `{"type": "omni"}`
- `{"type": "sector", "azimuth_deg": A, "beamwidth_deg": B, "front_to_back_db": F}`
  with A in [0, 360), B in (0, 360] and F >= 0;
- `{"type": "table", "azimuth_deg": A, "step_deg": S, "gain_db": [g0, g1, ...]}`:
  gains in dB relative to the main direction A, one per S degrees
  clockwise from A (S > 0, a non-empty array).

## Checks

A reader refuses another `format`, a `version` newer than it knows,
missing required members, wrong types, non-finite numbers, duplicate ids,
a wall class or antenna cell that does not exist and values out of range.
The parser of `scene.hpp` reports the first problem as one line with its
path, for example `antennas[1].cell: unknown cell "c9"`.

## Versioning

Version 1 may gain members compatibly (readers ignore unknown members). A
breaking change raises the version; a reader refuses a newer version. The
versions are listed in CHANGELOG.md.

## Not in the scene

The scene holds no plan image, plan name, file path or example id: those
stay in Yantra, and a plugin that needs a floor reference uses the floor id
(RAD-19). The scenario files of the generator (RAD-71) are a different
format, documented with the generator.
