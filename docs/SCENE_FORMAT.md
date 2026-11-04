<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Scene JSON (`yantra-radio-scene`, version 1.1)

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
Examples: `tests/fixtures/scenes/two_rooms_v1.json` (1.0) and
`tests/fixtures/scenes/multiwall_v1_1.json` (1.1). The radio test host
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
| `version` | uint | `1` (the major version) |
| `minor_version` | uint, optional | `1` for a scene that uses the 1.1 members; absent (0) in a 1.0 scene. Readers read any minor version of major 1 |
| `floor.id` | string, not empty | Floor id, the same as `yr_floor_plan_info::floor_id` |
| `floor.name` | string, optional | Floor name as shown (context text, never copied into records, RAD-19) |
| `floor.scale_set` | bool | The plan has a scale; without it there are no positions |
| `floor.width_m`, `floor.height_m` | number >= 0 | Plan size in metres (0 without a scale) |
| `floor.slab_attenuation_db` | number >= 0, optional | Attenuation of one floor slab, for antennas on other floors (`antennas[].floor_offset`); the multi-wall model uses 15 dB when absent |
| `floor.storey_height_m` | number > 0, optional | Floor-to-floor height for the distance to antennas on other floors; 3 m when absent |
| `floor.storeys[]` | array, optional (1.1) | Other floors with their own level and slab sum, for antennas on them (see "Other floors") |
| `floor.storeys[].floor_offset` | integer -200..200, not 0, unique | The floor this many storeys above (> 0) or below (< 0) |
| `floor.storeys[].level_m` | number, sign of `floor_offset` | Level of that floor above this floor's level in metres (negative below) |
| `floor.storeys[].slab_attenuation_db` | number >= 0 | Total attenuation of the slabs between this floor and that one |
| `wall_classes[]` | array | Wall classes with their default attenuation |
| `wall_classes[].id` | string, unique | Class id |
| `wall_classes[].label` | string, optional | Class name as shown |
| `wall_classes[].attenuation_db` | number >= 0 | Default attenuation of a wall of this class |
| `wall_classes[].reflection_loss_db` | number >= 0, optional | Loss of a specular reflection off a wall of this class (multi-wall model with reflections); the model's default when absent |
| `walls[]` | array | Wall segments |
| `walls[].x1_m` .. `y2_m` | number | End points |
| `walls[].class` | string, optional | `wall_classes[].id`; absent or `""` for a wall without a class |
| `walls[].attenuation_db` | number >= 0 | The effective attenuation (the class value unless overridden); with `bands`, the value for readers of 1.0 and for no frequency |
| `walls[].polyline` | string, optional (1.1) | Id of the wall polyline this segment belongs to; segments with the same non-empty id are one wall (see "Polylines"); absent or `""`: a wall of its own |
| `walls[].bands[]` | array, optional (1.1), not empty | Attenuation per frequency: `{"frequency_hz": uint > 0, "attenuation_db": number >= 0}`, frequencies strictly ascending (see "Per-band attenuation") |
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
| `antennas[].floor_offset` | integer -200..200, optional | The antenna is on the floor this many storeys above (> 0) or below (< 0) this floor, at `x_m`/`y_m` of this floor's plan frame; 0 (this floor) when absent |

Patterns:

- `{"type": "omni"}`
- `{"type": "sector", "azimuth_deg": A, "beamwidth_deg": B, "front_to_back_db": F}`
  with A in [0, 360), B in (0, 360] and F >= 0;
- `{"type": "table", "azimuth_deg": A, "step_deg": S, "gain_db": [g0, g1, ...]}`:
  gains in dB relative to the main direction A, one per S degrees
  clockwise from A (S > 0, a non-empty array).

The optional members (`slab_attenuation_db`, `storey_height_m`,
`reflection_loss_db`, `floor_offset`) were added compatibly within version
1 for the generator's indoor multi-wall model (RAD-73,
docs/CHANNEL_MODEL.md): a reader that does not know them ignores them, and
the writer leaves them out when they are not set. A host that knows only
the floor's own antennas leaves `floor_offset` out.

## Version 1.1 members

Version 1.1 (`"minor_version": 1`) adds three optional members so a scene
can carry a building model such as Yantra's (Yantra IND-11, IND-44)
without folding. A 1.0 reader ignores them and still reads the scene, with
the 1.0 meaning of the required members; a scene without them gives the
same levels as before.

### Per-band attenuation

A wall's `bands` give its attenuation at several frequencies (for
example the class table of a building model at 400 and 900 MHz). The
multi-wall model takes the value at the carrier of the cell it computes:
linear in log f between the two bands around the carrier, the first
band's value below the first and the last band's value above the last.
`attenuation_db` stays required: it is the value at the writer's main
frequency, used without a frequency and by 1.0 readers. Example:

```json
{ "x1_m": 30.0, "y1_m": 0.0, "x2_m": 30.0, "y2_m": 20.0,
  "class": "glass", "attenuation_db": 4.0,
  "bands": [ { "frequency_hz": 380000000, "attenuation_db": 4.0 },
             { "frequency_hz": 450000000, "attenuation_db": 9.0 } ] }
```

gives 4.77 dB at 390 MHz and 9 dB at 450 MHz.

### Other floors

`floor.storeys` lists other floors as seen from this one, each with the
level of its floor above this floor (`level_m`) and the total slab
attenuation in between (`slab_attenuation_db`). An antenna whose
`floor_offset` is listed uses them; an antenna on a floor that is not
listed uses `floor_offset` x `storey_height_m` and `|floor_offset|` x
`slab_attenuation_db` as in 1.0. So floors of different heights and slabs
of different attenuation need no folding into the EIRP:

```json
"storeys": [ { "floor_offset": 1, "level_m": 4.5, "slab_attenuation_db": 12.0 },
             { "floor_offset": -1, "level_m": -3.5, "slab_attenuation_db": 20.0 } ]
```

### Polylines

A wall drawn as a polyline is written as one segment per side, each with
the same `polyline` id. A path through a vertex of the polyline crosses
that wall once, whatever the angle (in 1.0 the two segments were two walls
and both counted, which over-counted a path through a corner of one wall,
RAD-73). A path that crosses the polyline at two different points crosses
it twice. Segments without an id (all segments of a 1.0 scene) are walls
of their own, with the 1.0 rules: through the joint of two collinear
walls once, through the corner of two walls at an angle twice.

### Not in 1.1

No "EIRP unknown" and no outdoor donor directions: the scene stays the
simulator's ground truth with an EIRP per antenna (RAD-18). A host whose
model has an antenna without EIRP (Yantra IND-12) writes a nominal value;
for a bundled example plan the scenario's antenna EIRPs replace it by
antenna id (RAD-61, docs/SCENARIO_FORMAT.md). Outdoor coverage is the
scenario's outdoor site (RAD-74), not part of the scene.

## Checks

A reader refuses another `format`, a `version` (major) newer than it
knows, missing required members, wrong types, non-finite numbers, duplicate ids,
a wall class or antenna cell that does not exist and values out of range.
The parser of `scene.hpp` reports the first problem as one line with its
path, for example `antennas[1].cell: unknown cell "c9"`.

## Versioning

Version 1 may gain members compatibly (readers ignore unknown members);
such an addition raises `minor_version`, and a reader reads any minor
version of the major version it knows. A breaking change raises `version`;
a reader refuses a newer `version` (for example 2.0). The writer of
`scene.hpp` writes `minor_version` 1 when a 1.1 member is set and leaves it
out otherwise, so a 1.0 scene read and written again stays 1.0. The
versions:

| Version | yantra-radios | Change |
|---|---|---|
| 1.0 | 0.1.0 | First version (with the optional multi-wall members) |
| 1.1 | 0.2.0 | `minor_version`, `floor.storeys`, `walls[].polyline`, `walls[].bands` |

They are also listed in CHANGELOG.md.

## Not in the scene

The scene holds no plan image, plan name, file path or example id: those
stay in Yantra, and a plugin that needs a floor reference uses the floor id
(RAD-19). The scenario files of the generator (RAD-74) are a different
format that refers to scenes (docs/SCENARIO_FORMAT.md).
