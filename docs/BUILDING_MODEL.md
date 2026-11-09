<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Building model

The building of an indoor project (spec section 21): floors with their plan
source and trim, scale and alignment point, walls with wall classes, antennas
and the cells they serve. The model is plain data in the core library
(`core/include/yantra/core/building/model.h`, no UI); what the scale,
reference, wall and antenna tools do with it is in
`core/include/yantra/core/building/edit.h` (the windows come in a later
phase and call it); the project file stores all of it (schema 3).

Users of the model:

- the ray-tracing propagation model (IND-40) and its wall geometry
  (`building/geometry.h`: wall segments in metres, walls crossed by a path);
- the scene export for radio plugins with the SCENE capability (RAD-36,
  `radio/scene_export.h`), such as the simulator of yantra-radios;
- the context callbacks of the radio plugins (PLG-27,
  `context/building_context.h`): floor plan shown, cursor and walk
  position in metres, scene of every floor;
- the project file (IND-01, NFR-05), which stores the model in its tables
  ([formats/PROJECT.md](formats/PROJECT.md); `project/project.h`
  `Project::building()`).
- the plan import (IND-02, IND-05..07, IND-10; [PLANS.md](PLANS.md),
  `plan/import.h`), which renders a floor from its source, page and trim
  (`plan/trim.h`, `plan::Trim` is `building::Trim`).

The core is technology-neutral (PLG-01): wall classes and antennas are
generic RF data, and a cell's identity is a set of named key values of a
technology schema that only the technology plugin interprets.

## Frames and units

| Frame | Origin and axes | Used for |
|---|---|---|
| page units | top left of the source page (PDF points or image pixels), x right, y down | trim outlines (IND-05, IND-07) |
| plan units | top left of the trimmed floor plan, x right, y down, the page's unit length | plan size, walls, antennas, scale points |
| plan frame, metres | plan units x metres per unit | context positions and scenes (RAD-18, RAD-36) |
| building frame, metres | (plan units - reference point) x metres per unit | the same point on every aligned floor (IND-09, IND-44) |

Walls and antennas are kept in plan units so they stay on the plan when the
scale changes, and so a floor without scale can still be drawn on (IND-08:
allowed, but marked "not to scale"). Angles are degrees clockwise from the
plan's -y axis ("up" on the plan), as in the scene format. Heights are
metres above the floor, levels dB and dBm, wall-class frequencies MHz,
carriers Hz.

## Scale (IND-08)

A floor's scale is one of:

- `two_points`: two plan points and their real distance in metres;
- `scale_bar`: the two ends of a scale bar and the length it stands for;
- `factor`: metres per plan unit given directly;
- `shared`: the scale of another floor (floors trimmed from one sheet);
  that floor must have its own scale (no chains);
- `none`: no scale. The floor has no positions in metres: the context
  callbacks answer NO_POSITION and the scene has no walls or antennas.

### Setting the scale

The scale tool asks for two points on the plan and their real distance in
metres (`building::two_point_scale`), or the two ends of a scale bar and
the length it stands for (`scale_bar`). A floor cut from the same sheet as
another (same source and page, `same_sheet`) can share that floor's scale
(`Project::share_scale`); when the scale of that floor changes, the
sharing floors follow. Lengths and areas are given in metres, lengths to
the centimetre and areas to 0.1 m² (`format_length`, `format_area`); a
floor without scale gives "not to scale". Plan units and metres are kept
as doubles, without rounding, in the model and the project file.

## Alignment (IND-09)

Each floor may have a reference point: the same building feature (for
example a staircase corner) clicked on every floor. A floor with a scale
and a reference point is aligned; a point of one aligned floor maps to
the same building point on another (`point_on_floor`), whatever their
sources and scales. Alignment is a translation: floors are turned alike by
their trims (quarter turns and deskew), so no rotation is stored. The
scene export puts the antennas of other aligned floors above or below the
shown floor at their building position (IND-44).

## Wall classes (IND-11)

A wall class has an attenuation table: the loss of one crossing at one or
more frequencies, ascending. At another frequency the loss is linear in
log(frequency) between the two neighbouring entries, and the first or last
entry's value outside the table. A wall may have its own attenuation, which
holds at every frequency. The table is editable per project; a project that
gives its table replaces the defaults.

Default table (estimates for one crossing of a typical wall, 400 MHz for
the 380-470 MHz TETRA bands, 900 MHz for the 800/900 MHz bands):

| id | name | dB at 400 MHz | dB at 900 MHz |
|---|---|---|---|
| `concrete` | Concrete | 12 | 23 |
| `brick` | Brick | 8 | 10 |
| `plasterboard` | Plasterboard | 3 | 6 |
| `glass` | Glass | 2 | 4.5 |
| `wood` | Wood | 3 | 6.5 |
| `door` | Door | 4 | 9 |
| `metal` | Metal | 25 | 25 |
| `coated_glass` | Coated glass | 20 | 20 |

They are estimates, not measurements: the values are in the range of the
indoor wall losses published for the multi-wall model (COST 231 final
report) and ITU-R P.1238, and the 900 MHz values follow the frequency trend
of the material conductivity in ITU-R P.2040 (concrete, plasterboard, glass
and wood lossier with frequency, brick nearly flat, metal and metallised
glass taken as frequency-independent). Real walls differ by thickness,
reinforcement and moisture; the interpolation fits per-class losses to the
measurements (IND-41). A class may also give the loss of a specular
reflection (`reflection_loss_db`, IND-40); without it the propagation
model's default applies.

### Drawing and editing walls

A wall is a polyline in plan units (at least two points) with a class of
the project's table or its own attenuation. The wall tool picks the wall
nearest to a click within a tolerance (`nearest_wall`, the wall drawn last
wins a tie), and edits it: add, move and remove corners (a wall keeps two
points), move the whole wall, split it at a corner (`split_wall`). The
project stores each change at once (`Project::add_wall`, `add_walls` for
accepted proposals, `update_wall`, `remove_wall`; ids `wall-<n>`). The
table is edited with `Project::set_wall_class` (add or change a class),
`remove_wall_class` (only when no wall uses it) and `reset_wall_classes`
(the defaults back to their default values, the project's own classes
kept).

### Walls proposed from the plan

`plan::propose_walls` (`core/plan/wall_detect.h`, IND-11 S) looks at the
floor's raster, rendered through its trim, so turns and deskew are respected
and what lies outside the outline is ignored. The raster is bounded (at most
6 Mpx, at most 50 pixels per metre, at most the plan's own detail).
`detect_dark_lines` is deterministic and has no library beyond the plan
sources:

1. dark pixels: luminance below 110, alpha at least one half;
2. connected groups (8-neighbours) smaller than half the minimum length are
   removed: noise and text;
3. a Hough transform (0.5 degrees, 1 pixel) gives the strongest lines first;
   the dark run along each, with small breaks bridged, becomes a segment on
   the centre of its drawn width, and its pixels stop voting. A width above
   the maximum is a filled area, not a wall;
4. collinear segments merge across small gaps (a door stays open), and two
   parallel faces of one wall become their centre line;
5. segments below the minimum length (default 1.5 m) are dropped.

Tests hold a found wall within 3 pixels of the drawn one at each end and
within 1 degree. It is an image operation on the drawing, with no radio data:
a dimension line or a stair can be proposed, which is why a person decides.

`WallProposals` (app model) runs it as a job and keeps the result in memory;
proposals along a wall that is already there are not offered. Accepting calls
`Project::add_walls` through `FloorEditor` as one undo step, with the class
for new walls.

## Antennas and cells (IND-12)

An antenna has an id (unique in the building), name, model text, type
(`indoor_das` or `outdoor_donor`), plan position, height, gain, pattern,
optional EIRP, optional feed power with cable and splitter losses, the cell
it serves and, for an outdoor donor direction, the bearing and elevation
towards the outdoor site. Its effective EIRP is the EIRP when known, else
feed power - cable loss - splitter loss + gain, else unknown (IND-42
estimates it from the measurements).

Patterns, the same as in the scene format:

- `omni`: 0 dB in every direction;
- `sector`: `-min(12 (off / beamwidth)^2, front_to_back)` dB, `off` the
  angle from the azimuth;
- `table`: gains relative to the main direction, one per `step_deg`
  clockwise from the azimuth, linear in between and from the last entry
  back to the first at 360 degrees.

A cell has an id, label, technology id, schema id, main carrier and its
identity as named integer key values of the technology schema (for a TETRA
cell `mcc`, `mnc`, `la`, `colour_code`). One cell may have several antennas
(DAS).

### Placing antennas

An antenna is placed at a plan point of a floor (`Project::add_antenna`,
id `antenna-<n>`), moved on the floor or to another floor
(`move_antenna`), and aimed by clicking where it points
(`aim_antenna`): a directional antenna (`directional_pattern`: sector
with azimuth, beamwidth and front-to-back ratio) gets its azimuth, an
outdoor donor direction its bearing. Cells are added once
(`Project::add_cell`, id `cell-<n>`) and chosen per antenna; a cell an
antenna serves cannot be removed.

## Plan sources and trims (IND-02, IND-03, IND-05..07)

A plan source is data only: id, original file name (no folder), SHA-256,
and whether the project keeps a copy of the file or only a reference to a
cached licence-restricted file by its SHA-256 (IND-03). A floor names its
source and page and keeps its trim: shape (`none`, `rectangle` with 4
corners, `polygon` with 3 or more points) as an outline in page units,
quarter turns (0..3, clockwise) and a fine deskew (-45..45 degrees). The
plan image is rendered from the source each time and never stored. A floor
of a bundled example plan carries its stable example id (DEMO-05, RAD-18).

## JSON form (`yantra-building`, version 1)

All file formats: [formats/README.md](formats/README.md).

<!-- example: yantra-building -->
```json
{
  "format": "yantra-building",
  "version": 1,
  "name": "Example office",
  "floor_attenuation_db": 15.0,
  "wall_classes": [
    {"id": "concrete", "name": "Concrete",
     "bands": [{"frequency_mhz": 400.0, "attenuation_db": 12.0},
               {"frequency_mhz": 900.0, "attenuation_db": 23.0}]}
  ],
  "cells": [
    {"id": "T1", "label": "Indoor DAS", "technology": "tetra", "schema": "tetra_cell",
     "carrier_hz": 390000000,
     "identity": {"colour_code": 1, "la": 100, "mcc": 901, "mnc": 1}}
  ],
  "sources": [{"id": "plan-1", "file_name": "office.png", "embedded": true}],
  "floors": [
    {"id": "f0", "name": "Ground floor", "level": 0, "height_m": 3.2,
     "source": "plan-1", "page": 0,
     "trim": {"shape": "rectangle", "outline": [[10, 10], [410, 10], [410, 210], [10, 210]],
              "quarter_turns": 0, "deskew_deg": 0.0},
     "plan_size": [400.0, 200.0],
     "scale": {"method": "two_points", "a": [0.0, 0.0], "b": [400.0, 0.0], "distance_m": 20.0},
     "reference_point": [0.0, 0.0],
     "walls": [{"id": "w1", "class": "concrete", "points": [[200.0, 0.0], [200.0, 200.0]]}],
     "antennas": [{"id": "a1", "type": "indoor_das", "position": [60.0, 100.0],
                   "height_m": 2.8, "gain_dbi": 2.0, "pattern": {"type": "omni"},
                   "eirp_dbm": 10.0, "cell": "T1"}]}
  ]
}
```

The complete example is `tests/fixtures/building/example_office.building.json`.

| Member | Meaning | Default |
|---|---|---|
| `format`, `version` | `"yantra-building"`, 1 | required |
| `name` | building name; plan names are "<name>, <floor name>" | `""` |
| `floor_attenuation_db` | default slab attenuation per floor crossed (IND-44) | 15 |
| `wall_classes[]` | `id`, `name` (default the id), `bands[]` (`frequency_mhz` > 0 ascending, `attenuation_db` >= 0), optional `reflection_loss_db` | the default table |
| `cells[]` | `id`, `label`, `technology`, `schema`, `carrier_hz` (0: unknown), `identity` (name -> integer) | none |
| `sources[]` | `id`, `file_name`, optional `sha256` (64 lower-case hex digits; required when not embedded), `embedded` | none; `embedded` true |
| `floors[].id`, `name`, `level` | unique id, name (default the id), unique level (0 ground, -1 basement) | id and level required |
| `floors[].height_m` | floor-to-floor height | 3 |
| `floors[].source`, `page`, `example_id`, `trim` | plan source, page (0-based), bundled example id, trim (`shape`, `outline`, `quarter_turns`, `deskew_deg`) | none, 0, `""`, none |
| `floors[].plan_size` | `[width, height]` in plan units | `[0, 0]` |
| `floors[].scale` | `method` `none`, `two_points`/`scale_bar` (`a`, `b`, `distance_m`), `factor` (`metres_per_unit`), `shared` (`floor`) | none |
| `floors[].reference_point` | alignment point in plan units (IND-09) | none ((0, 0) for the building frame) |
| `floors[].slab_attenuation_db` | slab between this floor and the one above | `floor_attenuation_db` |
| `floors[].walls[]` | optional `id`, `class` (may be empty only with `attenuation_db`), `points` (>= 2 `[x, y]`), optional `attenuation_db` | |
| `floors[].antennas[]` | `id`, `name`, `model`, `type`, `position`, `height_m` (2.7), `gain_dbi` (2), `pattern` (omni), `eirp_dbm`, `feed_power_dbm`, `cable_loss_db`, `splitter_loss_db`, `cell`, `direction_deg`, `elevation_deg` | |

Reading refuses another format, a newer version, missing required members,
wrong types and values out of range, and validates the model (unique ids
and levels, known wall classes, cells, sources and shared floors, polylines
with two points or more, valid scales, trims and patterns). The first
problem is reported as one line with its place, e.g.
`floors[0].walls[2]: unknown wall class "steel"`. Unknown members are
ignored at every level, so version 1 may gain members compatibly; a
breaking change raises the version. Writing and reading again gives an
equal model.

## Scene export (RAD-36)

`export_scene(building, floor_id)` writes the floor as scene JSON in the
format defined by yantra-radios (`yantra-radio-scene` version 1.1,
`YR_SCENE_FORMAT` in `yantra/radio.h`, documented in the submodule's
`docs/SCENE_FORMAT.md`; `"minor_version": 1` whenever a 1.1 member is
written, so a scene without walls and other floors stays 1.0). The radio
host answers `get_scene` with it.

- `floor`: id, name, `scale_set` (a scale and a plan size above 0), plan
  size in metres, `slab_attenuation_db` = `floor_attenuation_db`,
  `storey_height_m` = the floor's height (both for 1.0 readers and for
  floors without a storey).
- `floor.storeys` (1.1): every other floor with antennas in the scene,
  with `floor_offset` = level difference, `level_m` = its level above
  this floor (the floor-to-floor heights of the floors in between,
  negative below; a level without a floor counts this floor's height) and
  `slab_attenuation_db` = the model's slab sum in between
  (`inter_floor_attenuation_db`, IND-44).
- `wall_classes`: every class, with its attenuation at the scene frequency.
- `walls`: one entry per polyline segment, in plan-frame metres, with the
  class and the effective attenuation at the scene frequency (what a 1.0
  reader uses), the `polyline` id (1.1: the wall id, else
  `<floor id>/<index>`, the same on every segment of the wall) and, for a
  wall of a class without its own loss, the class table as `bands` (1.1:
  frequencies in Hz, the same log-f interpolation as
  `WallClass::attenuation_at_db`). A wall with its own loss has no bands:
  that loss holds at every frequency.
- Scene frequency: the mean carrier of the exported cells (or an explicit
  option); without cells the classes' first band.
- `antennas`: the indoor antennas of the floor with a cell and an EIRP
  (or `SceneExportOptions::default_eirp_dbm`, a nominal value, for those
  without). Antennas of other floors are added with `floor_offset` =
  level difference when both floors have a scale and a reference point,
  with their own EIRP: their storey gives the height and the slabs.
- `cells`: the cells of the exported antennas, identity by the radio API's
  core field names `mcc`, `mnc`, `la`, `colour_code`, and the carrier.

Left out, with a warning each: outdoor donor directions, antennas without a
cell or without EIRP (unless a default EIRP is given), cells without a
carrier or without the four identity values, and the walls and antennas of
a floor without scale. Numbers are rounded to 1e-6 so the text is the same
on every platform. The golden scenes
`tests/fixtures/building/example_office.f0.scene.json` and `.f1.scene.json`
are also read by yantra-radios' own scene parser (ctests
`scene_radios_check_*`, which also check that it refuses a broken 1.0 and
a broken 1.1 member), and the simulator's levels are checked against the
model end to end (`tests/scene_sim`: a wall, a storey slab, a per-band
wall at the simulated carrier and a path through a polyline vertex).

Known differences between the model and the scene:

- Outdoor donor directions have no place in the scene: outdoor coverage
  is the simulator scenario's outdoor site (yantra-radios RAD-74).
- An unknown EIRP (IND-12) has no place in the scene either: such an
  antenna is left out, or written with the nominal
  `default_eirp_dbm`; for a bundled example plan the simulator's scenario
  replaces antenna EIRPs by antenna id. Estimating it is Yantra's job on
  its own model (IND-41, IND-42).
- A 1.0 reader (yantra-radios 0.1.0) ignores the 1.1 members: it applies
  the uniform `slab_attenuation_db` and `storey_height_m` to other floors,
  one attenuation per wall at the scene frequency, and both segments of a
  polyline crossed exactly at a vertex.

## Context from the model (PLG-27)

`BuildingContext` publishes the snapshot the radio host reads (OA-03):

- `get_floor_plan`: plan name "<building>, <floor>", the floor's example
  id, floor id and name, plan size in metres and the scale state;
- `get_cursor`: the mouse over the shown floor, converted from plan units
  to plan-frame metres with the floor scale (exactly plan units x metres
  per unit); none without a scale (NO_POSITION);
- `get_walk_position`: the walk position (GEO-02) on its floor, likewise;
- `get_scene`: the scene of every floor.

A model edit re-exports the scenes; an edit of the shown floor's scene
gives SCENE_CHANGED, another floor shown PLAN_CHANGED, and the cursor and
walk positions keep their plan points, so their metres follow a new scale.
