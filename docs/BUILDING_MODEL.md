<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Building model

The building of an indoor project (spec section 21): floors with their plan
source and trim, scale and alignment point, and the building's cells. The
model is plain data in the core library
(`core/include/yantra/core/building/model.h`, no UI); what the scale and
reference tools do with it is in `core/include/yantra/core/building/edit.h`;
the project file stores all of it.

Users of the model:

- the context callbacks of the radio plugins (PLG-27,
  `context/building_context.h`): floor plan shown, cursor and walk
  position in metres, and the fixed demo scene of an example floor;
- the project file (IND-01, NFR-05), which stores the model in its tables
  ([formats/PROJECT.md](formats/PROJECT.md); `project/project.h`
  `Project::building()`);
- the plan import (IND-02, IND-05..07, IND-10; [PLANS.md](PLANS.md),
  `plan/import.h`), which renders a floor from its source, page and trim
  (`plan/trim.h`, `plan::Trim` is `building::Trim`);
- the walk tests and the coverage map, whose points are in the plan frame
  of their floor ([SURVEY.md](SURVEY.md), [INTERPOLATION.md](INTERPOLATION.md)).

Yantra has no walls, wall classes, antennas or slab attenuations any more
(spec 0.4): the coverage map is an interpolation of the measured points
(IND-41). A project of Yantra 0.2 or earlier (schema 7 or lower) opens
with them dropped (IND-46, [formats/PROJECT.md](formats/PROJECT.md)
version 8). The simulator's walls
and antennas of a bundled example floor are in its fixed demo scene
([DEMO.md](DEMO.md) "Fixed demo scenes"), never in the model.

The core is technology-neutral (PLG-01): a cell's identity is a set of
named key values of a technology schema that only the technology plugin
interprets.

## Frames and units

| Frame | Origin and axes | Used for |
|---|---|---|
| page units | top left of the source page (PDF points or image pixels), x right, y down | trim outlines (IND-05, IND-07) |
| plan units | top left of the trimmed floor plan, x right, y down, the page's unit length | plan size, scale points, reference point, measured points |
| plan frame, metres | plan units x metres per unit | context positions (RAD-18), points in metres |
| building frame, metres | (plan units - reference point) x metres per unit | the same point on every aligned floor (IND-09) |

Points are kept in plan units so they stay on the plan when the scale
changes, and so a floor without scale can still be measured on (IND-08:
allowed, but marked "not to scale"). Heights are metres above the floor,
carriers Hz.

## Scale (IND-08)

A floor's scale is one of:

- `two_points`: two plan points and their real distance in metres;
- `scale_bar`: the two ends of a scale bar and the length it stands for;
- `factor`: metres per plan unit given directly;
- `shared`: the scale of another floor (floors trimmed from one sheet);
  that floor must have its own scale (no chains);
- `none`: no scale. The floor has no positions in metres: the context
  callbacks answer NO_POSITION.

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
their trims (quarter turns and deskew), so no rotation is stored.

## Cells

A cell has an id, label, technology id, schema id, main carrier and its
identity as named integer key values of the technology schema (for a TETRA
cell `mcc`, `mnc`, `la`, `colour_code`). Cells are added once
(`Project::add_cell`, id `cell-<n>`); they are the project's cell database
([CELL_DATABASE.md](CELL_DATABASE.md)), and the demo names the cell of its
coverage map by its id.

## Plan sources and trims (IND-02, IND-03, IND-05..07)

A plan source is data only: id, original file name (no folder), SHA-256,
and whether the project keeps a copy of the file or only a reference to a
cached licence-restricted file by its SHA-256 (IND-03). A floor names its
source and page and keeps its trim: shape (`none`, `rectangle` with 4
corners, `polygon` with 3 or more points) as an outline in page units,
quarter turns (0..3, clockwise) and a fine deskew (-45..45 degrees). The
plan image is rendered from the source each time and never stored. A floor
of a bundled example plan carries its stable example id (DEMO-05, RAD-18).
When a trim is edited, the scale points, the reference point and the
measured points move with it so they stay at the same place on the sheet
(`plan::reanchor_floor`; the project moves the points,
[PLANS.md](PLANS.md)).

## JSON form (`yantra-building`, version 1)

All file formats: [formats/README.md](formats/README.md).

<!-- example: yantra-building -->
```json
{
  "format": "yantra-building",
  "version": 1,
  "name": "Example office",
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
     "reference_point": [0.0, 0.0]}
  ]
}
```

The complete example is `tests/fixtures/building/example_office.building.json`.

| Member | Meaning | Default |
|---|---|---|
| `format`, `version` | `"yantra-building"`, 1 | required |
| `name` | building name; plan names are "<name>, <floor name>" | `""` |
| `cells[]` | `id`, `label`, `technology`, `schema`, `carrier_hz` (0: unknown), `identity` (name -> integer) | none |
| `sources[]` | `id`, `file_name`, optional `sha256` (64 lower-case hex digits; required when not embedded), `embedded` | none; `embedded` true |
| `floors[].id`, `name`, `level` | unique id, name (default the id), unique level (0 ground, -1 basement) | id and level required |
| `floors[].height_m` | floor-to-floor height | 3 |
| `floors[].source`, `page`, `example_id`, `trim` | plan source, page (0-based), bundled example id, trim (`shape`, `outline`, `quarter_turns`, `deskew_deg`) | none, 0, `""`, none |
| `floors[].plan_size` | `[width, height]` in plan units | `[0, 0]` |
| `floors[].scale` | `method` `none`, `two_points`/`scale_bar` (`a`, `b`, `distance_m`), `factor` (`metres_per_unit`), `shared` (`floor`) | none |
| `floors[].reference_point` | alignment point in plan units (IND-09) | none ((0, 0) for the building frame) |

Reading refuses another format, a newer version, missing required members,
wrong types and values out of range, and validates the model (unique ids
and levels, known sources and shared floors, valid scales and trims). The
first problem is reported as one line with its place, e.g.
`floors[0].trim.outline[1]: expected [x, y]`. Unknown members are ignored
at every level, so version 1 may gain members compatibly; a breaking
change raises the version. The members a building of 0.3 may still hold
(`floor_attenuation_db`, `wall_classes`, `floors[].slab_attenuation_db`,
`floors[].walls`, `floors[].antennas`) are unknown members now and are
ignored (IND-46). Writing and reading again gives an equal model.

## Context from the model (PLG-27)

`BuildingContext` publishes the snapshot the radio host reads (OA-03):

- `get_floor_plan`: plan name "<building>, <floor>", the floor's example
  id, floor id and name, plan size in metres and the scale state;
- `get_cursor`: the mouse over the shown floor, converted from plan units
  to plan-frame metres with the floor scale (exactly plan units x metres
  per unit); none without a scale (NO_POSITION);
- `get_walk_position`: the walk position (GEO-02) on its floor, likewise;
- `get_scene`: the scene of a floor from the context's `SceneSource`. In
  the app that is `context::demo_scenes(folders)` (DEMO-05, PLG-27): a
  floor with an example id gets the fixed demo scene
  `<data folder>/demo/scenes/<example id>.json` byte for byte (only its
  floor id is set to the floor's when they differ, e.g. a tour floor
  trimmed by the user); a user's own floor has no scene and `get_scene`
  answers NOT_FOUND, "no scene, use the defaults" (the simulator then
  uses its scenario's own levels). Scenes are never made from the model.
  Tests that need a simulator scene serve a checked-in scene file
  (`tests/fixture_scene.hpp`, `tests/fixtures/building/office_wall.scene.json`).

`set_building` takes every floor's scene from the source again; a
changed scene of the shown floor gives SCENE_CHANGED, another floor
shown PLAN_CHANGED, and the cursor and walk positions keep their plan
points, so their metres follow a new scale.
