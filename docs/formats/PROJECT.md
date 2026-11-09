<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Indoor project file (`*.yantra`)

An indoor project is one building: its floors, the plan files they are cut
from and, in later versions, everything drawn and measured on them (spec
section 21). It is saved as one file with the extension `.yantra`: an
[SQLite 3](https://sqlite.org) database in the layout below (IND-01,
NFR-05, DOC-12). Any SQLite tool can read it; Yantra itself changes it
only through the project API.

Code: `core/include/yantra/core/project/project.h` (API),
`core/src/project/schema.cpp` (tables and migrations),
`core/src/project/project.cpp`. Tests: `tests/project/`. The in-memory
form is the building model of [BUILDING_MODEL.md](../BUILDING_MODEL.md)
(`Project::building()`, `Project::replace_building()`).

## Identification and version

| Item | Value |
| --- | --- |
| `PRAGMA application_id` | `0x594E5452` ("YNTR"); other files are refused as "not a Yantra project" |
| `PRAGMA user_version` | schema version, now **7** |
| `meta.format` | `yantra-project` |

| Version | Change |
| --- | --- |
| 1 | `meta`, `project`, `building`, `sources`, `floors` |
| 2 | floor trims (IND-05..07): `floors` columns `trim_shape`, `quarter_turns`, `deskew_deg` and the table `floor_outline`; the page sizes of each source (IND-02): `source_pages`. Existing floors get the whole page (`trim_shape` `none`) |
| 3 | the rest of the building model: `floors` scale columns (`scale_*`, IND-08) and alignment point (`reference_x/y`, IND-09); the project's wall-class table `wall_classes` + `wall_class_bands` with the default classes (IND-11); `walls` + `wall_points` (IND-11); `cells` + `cell_identity`, `antennas` + `antenna_pattern_gains` (IND-12); meta counters `next_wall_number`, `next_antenna_number`, `next_cell_number`. Existing floors get no scale and no reference point |
| 4 | the project's recordings (`recordings`, REC-02/03), its walk tests and manual-point sets (`sessions`, IND-20, GEO-02, GEO-05) and their measured points (`points`), the one point list of the project window and the report (IND-30). Existing projects get no sessions |
| 5 | the cell database in the project's cells (DB-01): `cells` columns `site_name`, `sector`, `azimuth_deg`, `colour`, `notes`, `origin`, `plugin_version`; the tables `cell_parameters` and `cell_neighbours`; meta keys `celldb_match:<technology>`. Existing cells become manual entries without parameters or neighbours |
| 6 | the propagation model settings (`propagation`, IND-40/41) and the stored interpolation results with their parameters and input fingerprints (`interpolations`, IND-41..45). Existing projects get the default settings and no results |
| 7 | the project's acceptance profiles (`acceptance_profiles`, EXP-06), used by the report. Existing projects get none (the report then uses the default profile) |

Rules:

- **Older files** (a lower version) are migrated when they are opened,
  one step per version, each step in its own transaction (a failed step
  leaves the file at the version before it). Before the first step Yantra
  writes a backup copy `<name>.yantra.schema<old>.bak` (written whole,
  never over an existing backup), which the older Yantra can still open.
  The `meta` key `schema_<n>_by` records the Yantra version and the time
  of each step.
- **Newer files** (a higher version) are refused with "saved by a newer
  Yantra (project schema N, this version reads up to M)"; the file is not
  changed in any way.
- A version never changes once it is released: a later Yantra adds a
  step with new tables or columns (new columns get defaults, so the step
  needs no rewrite of existing rows) and a row in the table above.

### Planned versions (hooks)

Version 1 stores the project-level and floor-level fields of the building
model and the source files; version 2 adds the floor trims and the source
page sizes; version 3 the rest of the building model (scale, alignment,
wall classes, walls, cells, antennas), so the whole model of
[BUILDING_MODEL.md](../BUILDING_MODEL.md) is stored; version 4 the
recordings, walk tests and measured points ([SURVEY.md](../SURVEY.md)); version 5 the
cell database ([CELL_DATABASE.md](../CELL_DATABASE.md)); version 6 the
propagation settings and the interpolation results
([PROPAGATION.md](../PROPAGATION.md)); version 7 the acceptance profiles
([SURVEY.md](../SURVEY.md) "Acceptance profiles"). Later data come as new versions in
the steps that add them; until then the API has no call that would store
them, so nothing is ever dropped silently.

## Storage rules

- **Every change is stored at once**, in its own transaction, with a
  write-ahead log (`PRAGMA journal_mode = WAL`, `synchronous = FULL`). A
  crash or power loss loses at most the change in progress; the file
  always opens with every change committed before. While the project is
  open a `<name>.yantra-wal` file sits next to it; Save and Close fold it
  into the project file, so a closed project is one file that can be
  copied to another computer. There are no unsaved changes to lose and no
  "discard changes" (undo, when added, is the application's).
- **One Yantra at a time**: the file is locked while it is open
  (`PRAGMA locking_mode = EXCLUSIVE`, an operating-system lock). A second
  Yantra, or the same one in another window, gets "open in another Yantra
  window or program". The lock ends with the process, also after a
  crash, so there are no stale lock files.
- **New files and copies are written whole**: New project and Save as
  write a complete file under a temporary name next to the target
  (`<name>.tmp-<process>-<n>`), flush it to disk and then give it its
  name, so the name never holds half a file. New project never replaces a
  file. Save as replaces an existing file only when asked to (the
  application asks the user), never the open project itself and never a
  project open in another Yantra. A crash in the middle can leave a
  temporary file, never a broken file under a project name.
- **Damaged files** give an error with the reason, never a crash: open
  runs SQLite's quick check before it writes anything, the connection uses
  SQLite's defensive mode and cell size checks, and every source has its
  SHA-256. The project check (`Project::check()`) lists every problem it
  finds (integrity, foreign keys, building model, source hashes).
- Times are UTC in ISO 8601 (`2026-11-05T09:30:00Z`). Units are those of
  the building model: heights in metres, plan sizes in plan units,
  attenuations in dB.

## Tables (version 7)

### `meta`

Key/value pairs of the file itself.

| Column | Type | Meaning |
| --- | --- | --- |
| `key` | TEXT, primary key | name |
| `value` | TEXT | value |

Keys: `format` (`yantra-project`), `next_floor_number`,
`next_source_number` and, from version 3, `next_wall_number`,
`next_antenna_number`, `next_cell_number` (the number of the next
generated id `floor-<n>`, `source-<n>`, `wall-<n>`, `antenna-<n>`,
`cell-<n>`; ids are never reused, also not after a removal),
`schema_<n>_by` (who migrated to version n, and when) and, from version 5,
`celldb_match:<technology>` (the cell database's match fields of a
technology, comma-separated, e.g. `la,frequency_hz`; DB-01 "by LA, by
channel, or by both"; no key: the technology's KEY fields).

### `project`

One row (`id` = 1): what File > New indoor project asks for (IND-01).

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER, primary key | always 1 |
| `name` | TEXT, not empty | project name |
| `site` | TEXT | site name |
| `operator` | TEXT | network operator |
| `notes` | TEXT | free notes |
| `created_utc`, `modified_utc` | TEXT | creation and last change |
| `created_with`, `modified_with` | TEXT | `Yantra <version>` that created it and made the last change |

### `building`

One row (`id` = 1): the building model's own fields.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER, primary key | always 1 |
| `name` | TEXT | building name (plan names are "<name>, <floor name>"); at creation the site name, or the project name without a site |
| `floor_attenuation_db` | REAL >= 0 | default slab attenuation between floors (IND-44), 15 |

### `sources`

The plan source files (IND-02, IND-03). The project keeps a copy of each
file, byte for byte; for licence-restricted files (such as the MAK plans
of DEMO-12) only a reference: the cached file's path when it was added
and its SHA-256, which identifies it on any computer. A file is stored
once: adding the same content again gives the existing source. The MAK
example projects (DEMO-13) are made without the file: their reference
holds the described name, size and SHA-256 and the file name as the
hint (`Project::add_source_reference(SourceReference)`), and their page
size comes from the description (`source_pages`); the app reads the
image from its cache with `read_source_from`.

| Column | Type | Meaning |
| --- | --- | --- |
| `row_id` | INTEGER, primary key | internal (blob access) |
| `id` | TEXT, unique | source id of the building model, `source-<n>` |
| `file_name` | TEXT | original file name, without folders |
| `media_type` | TEXT | sniffed from the content: `application/pdf`, `image/png`, `image/jpeg`, `image/gif`, `image/bmp`, `image/vnd.adobe.photoshop`, `image/vnd.radiance`, `image/x-portable-anymap`, `image/x-tga` or `application/octet-stream` |
| `size` | INTEGER | bytes (at most 1 GB) |
| `sha256` | TEXT, unique | 64 lower-case hex digits of the content |
| `storage` | TEXT | `embedded` or `reference` |
| `content` | BLOB | the file (`embedded` only; NULL for references) |
| `reference` | TEXT | `reference` only: the cached file's absolute path when it was added, or the described file name (a hint) |
| `added_utc` | TEXT | when it was added |

A source used by a floor cannot be removed.

### `floors`

One row per floor (IND-04), the building model's `Floor` fields. The
project window shows them ordered by `level`; every floor has its own
level. Reordering hands the same set of levels out in the new order.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | TEXT, primary key | floor id, `floor-<n>` when generated (stable: context callbacks, scenes) |
| `name` | TEXT, not empty | e.g. "Ground floor" |
| `level` | INTEGER, unique, -1000..1000 | 0 ground floor, 1 first floor, -1 basement |
| `height_m` | REAL > 0 | floor-to-floor height in metres |
| `source` | TEXT or NULL | `sources.id` of its plan source; NULL without a plan |
| `page` | INTEGER >= 0 | page of the source (0-based) |
| `example_id` | TEXT | id of a bundled example plan's floor (DEMO-05, DEMO-12), else empty |
| `plan_width`, `plan_height` | REAL >= 0 | trimmed plan size in plan units |
| `slab_attenuation_db` | REAL or NULL | attenuation of the slab to the floor above (IND-44); NULL: the building's default |
| `created_utc` | TEXT | when it was added |
| `trim_shape` | TEXT | the floor's trim on its source page (IND-05, IND-07): `none` (the whole page, or no source), `rectangle` (4 corners) or `polygon` (3 or more points); the outline is in `floor_outline`. Version 2 |
| `quarter_turns` | INTEGER 0..3 | rotation of the floor plan by quarter turns, clockwise. Version 2 |
| `deskew_deg` | REAL -45..45 | fine rotation (deskew) in degrees, clockwise, on top of the quarter turns. Version 2 |
| `scale_method` | TEXT | how the floor's scale is set (IND-08): `none` (not to scale: allowed, the report says "not to scale"), `two_points` (two plan points and their real distance), `scale_bar` (the ends of a scale bar and the length it stands for), `factor` (metres per plan unit given) or `shared` (the scale of `scale_shared_floor`, a floor of the same sheet with its own scale). Version 3 |
| `scale_ax`, `scale_ay`, `scale_bx`, `scale_by` | REAL | `two_points`, `scale_bar`: the two points in plan units, else 0. Version 3 |
| `scale_distance_m` | REAL >= 0 | `two_points`, `scale_bar`: their distance in metres (> 0), else 0. Version 3 |
| `scale_metres_per_unit` | REAL >= 0 | `factor`: metres per plan unit (> 0), else 0. Version 3 |
| `scale_shared_floor` | TEXT | `shared`: the `floors.id` whose scale this floor uses, else empty. Version 3 |
| `reference_x`, `reference_y` | REAL or NULL | the alignment point in plan units (IND-09), both NULL without one. Version 3 |

A floor stores its source, page and trim, never a new image: the plan is
rendered from the unchanged source each time (non-destructive, IND-07).
`plan_width` and `plan_height` are the size of the trimmed and turned
outline (the trim's bounding box in the turned frame), so walls, antennas
and points (plan units) stay on the plan. The page of a floor must exist
when the source's pages are recorded (`source_pages`). When a trim is
edited, the floor's walls, antennas, scale points and reference point are
moved with it so they stay at the same place on the sheet
([PLANS.md](../PLANS.md)).

Scale and alignment (version 3): metres are plan units times the floor's
metres per unit; building metres subtract the reference point first, so
the same building point has the same building metres on every aligned
floor (a floor with a scale and a reference point). A `shared` scale is
only offered between floors of one sheet (same source and page), and a
floor whose scale others share keeps a scale of its own. A trim only
turns, so the metres per unit never change with a trim.

### `floor_outline`

The outline of a floor's trim (version 2): its points in drawing order,
closed implicitly, in page units of the source page (PDF points or image
pixels, origin top left, y down). A rectangle has 4 points, a polygon 3
or more and must not cross itself; `none` has no points. Removed with
its floor.

| Column | Type | Meaning |
| --- | --- | --- |
| `floor` | TEXT | `floors.id` |
| `idx` | INTEGER >= 0 | position in the outline, from 0 |
| `x`, `y` | REAL | the point in page units |

### `source_pages`

The pages of a plan source (version 2, IND-02), recorded when the plan
import first opens the source, so the page list and the page sizes are
known without reading the file. No rows: not recorded yet. Removed with
its source.

| Column | Type | Meaning |
| --- | --- | --- |
| `source` | TEXT | `sources.id` |
| `page` | INTEGER >= 0 | page number, from 0 |
| `width`, `height` | REAL > 0 | page size in page units: PDF points as displayed (after the page's /Rotate), image pixels (after EXIF orientation) |
| `units_per_inch` | REAL >= 0 | 72 for PDF; for images the resolution stored in the file, 0 when unknown |

### `wall_classes`

The project's editable wall-class table (version 3, IND-11). A new or
migrated project has the default classes of
[BUILDING_MODEL.md](../BUILDING_MODEL.md) ("Wall classes"); the user may
change their values, add classes and remove classes no wall uses.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | TEXT, primary key | class id used by walls, e.g. `concrete` |
| `position` | INTEGER | place in the table (display order), from 0 |
| `name` | TEXT | display name |
| `reflection_loss_db` | REAL >= 0 or NULL | loss of a specular reflection (IND-40); NULL: the propagation model's default |

### `wall_class_bands`

The attenuation table of a class: the loss of one crossing at one or more
frequencies, ascending (version 3). Between two entries the loss is linear
in log(frequency); outside the table the first or last value holds.
Removed with its class.

| Column | Type | Meaning |
| --- | --- | --- |
| `class` | TEXT | `wall_classes.id` |
| `idx` | INTEGER >= 0 | position in the table, from 0 (ascending frequency) |
| `frequency_mhz` | REAL > 0 | frequency in MHz |
| `attenuation_db` | REAL >= 0 | loss of one crossing at that frequency |

### `walls`

The walls of a floor in drawing order (version 3, IND-11): polylines on
the plan with a wall class or their own attenuation. Removed with their
floor; a class in use by a wall cannot be removed.

| Column | Type | Meaning |
| --- | --- | --- |
| `row_id` | INTEGER, primary key | internal (points) |
| `floor` | TEXT | `floors.id` |
| `position` | INTEGER | drawing order on the floor, from 0 |
| `id` | TEXT | wall id, `wall-<n>` when generated, unique on its floor; may be empty for walls of an imported model |
| `wall_class` | TEXT or NULL | `wall_classes.id`; NULL only with `attenuation_db` |
| `attenuation_db` | REAL >= 0 or NULL | the wall's own loss at every frequency instead of its class's table |

### `wall_points`

The points of a wall's polyline (at least 2), in plan units of its floor,
in drawing order. Removed with the wall.

| Column | Type | Meaning |
| --- | --- | --- |
| `wall` | INTEGER | `walls.row_id` |
| `idx` | INTEGER >= 0 | position along the polyline, from 0 |
| `x`, `y` | REAL | the point in plan units |

### `cells`

The cells the antennas serve (version 3, IND-12). The identity is a set
of named integer key values of the technology's schema (for TETRA `mcc`,
`mnc`, `la`, `colour_code`) that only the technology plugin interprets.
A cell an antenna serves cannot be removed.

| Column | Type | Meaning |
| --- | --- | --- |
| `row_id` | INTEGER, primary key | internal |
| `id` | TEXT, unique | cell id, `cell-<n>` when generated |
| `position` | INTEGER | place in the list, from 0 |
| `label` | TEXT | display text |
| `technology_id` | TEXT | the technology plugin's id, empty when not set |
| `schema_id` | TEXT | the schema whose key fields the identity holds |
| `carrier_hz` | INTEGER >= 0 | main carrier in Hz, 0 when not known |
| `site_name` | TEXT | site name (version 5, DB-01) |
| `sector` | TEXT | sector name or number |
| `azimuth_deg` | REAL 0..<360 or NULL | sector azimuth, degrees from plan north; NULL when not known |
| `colour` | TEXT | graph colour `#rrggbb`, empty for the default |
| `notes` | TEXT | notes (up to 4096 bytes) |
| `origin` | TEXT | `manual`, `import` (CSV, DB-02) or `learned` (DB-03) |
| `plugin_version` | TEXT | version of the technology plugin that learned or imported it |

The project's cells are its cell database ([CELL_DATABASE.md](../CELL_DATABASE.md)):
one list, edited as cells (IND-12) or as database entries (DB-01..03).
Building edits keep the database columns; removing a cell removes its
parameters and neighbours.

### `cell_identity`

| Column | Type | Meaning |
| --- | --- | --- |
| `cell` | TEXT | `cells.id` |
| `name` | TEXT, not empty | key field name, e.g. `la` |
| `value` | INTEGER | its value |

### `cell_parameters`

The other fields of a cell database entry (version 5, DB-01): broadcast
and reselection parameters by the technology schema's field names (for
TETRA e.g. `rxlev_access_min_dbm`, `srt_db`, `subscriber_class`). Only
the technology plugin interprets them.

| Column | Type | Meaning |
| --- | --- | --- |
| `cell` | TEXT | `cells.id` |
| `name` | TEXT, not empty | field name |
| `value` | INTEGER, REAL or TEXT | its value, of the type it was given |

### `cell_neighbours`

The neighbour list of an entry (version 5, DB-01: up to 31 entries), one
row per field of an entry; an entry names a cell by cell-schema field
names (for TETRA `frequency_hz` and, when known, `la`).

| Column | Type | Meaning |
| --- | --- | --- |
| `cell` | TEXT | `cells.id` |
| `idx` | INTEGER 0..30 | position in the list |
| `name` | TEXT, not empty | field name |
| `value` | INTEGER, REAL or TEXT | its value |
| `inherited` | INTEGER 0/1 | 1: taken from the broadcasting cell, not sent for the neighbour (never refuses a match; [CELL_DATABASE.md](../CELL_DATABASE.md)) |

### `antennas`

Indoor DAS antennas and outdoor donor/macro directions placed on a floor
(version 3, IND-12), in placement order. Removed with their floor.

| Column | Type | Meaning |
| --- | --- | --- |
| `row_id` | INTEGER, primary key | internal (pattern gains) |
| `id` | TEXT, unique | antenna id, `antenna-<n>` when generated, unique in the building |
| `floor` | TEXT | `floors.id` |
| `position` | INTEGER | order on the floor, from 0 |
| `name` | TEXT | display name |
| `model` | TEXT | antenna type or product, free text |
| `type` | TEXT | `indoor_das` or `outdoor_donor` |
| `x`, `y` | REAL | position in plan units of the floor |
| `height_m` | REAL | height above the floor in metres |
| `gain_dbi` | REAL | antenna gain in dBi |
| `pattern` | TEXT | `omni`, `sector` (directional) or `table` |
| `azimuth_deg` | REAL | `sector`, `table`: main direction, degrees clockwise from "up" on the plan, [0, 360) |
| `beamwidth_deg` | REAL | `sector`: half-power beamwidth, (0, 360] |
| `front_to_back_db` | REAL | `sector`: cap of the pattern loss, >= 0 |
| `step_deg` | REAL | `table`: angle between the gains of `antenna_pattern_gains`, > 0 |
| `eirp_dbm` | REAL or NULL | known EIRP in the main direction; NULL: unknown (estimated from the measurements, IND-42) |
| `feed_power_dbm` | REAL or NULL | power at the DAS feed point |
| `cable_loss_db`, `splitter_loss_db` | REAL >= 0 | losses from the feed point to the antenna |
| `cell` | TEXT or NULL | `cells.id` of the cell it serves |
| `direction_deg` | REAL or NULL | `outdoor_donor`: bearing towards the outdoor site (same angle convention) |
| `elevation_deg` | REAL -90..90 or NULL | `outdoor_donor`: elevation towards the site |

### `antenna_pattern_gains`

The gains of a `table` pattern relative to the main direction, one per
`step_deg` clockwise from the azimuth. Removed with the antenna.

| Column | Type | Meaning |
| --- | --- | --- |
| `antenna` | INTEGER | `antennas.row_id` |
| `idx` | INTEGER >= 0 | entry, from 0 (at idx x step_deg) |
| `gain_db` | REAL | gain relative to the main direction in dB |

### `recordings`

The recording files of the project's walks (REC-02, REC-03; version 4).
A recording is a file of its own ([RECORDING.md](RECORDING.md)), kept
next to the project; the project lists it with its size and SHA-256 so
a moved, missing or changed file is noticed.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER, primary key | recording id |
| `file_name` | TEXT, unique, not empty | file name, e.g. `walk_0000.yrec.zst` |
| `session_id` | TEXT | the recording session id (16 hex digits) |
| `session_name` | TEXT | the recording session name |
| `file_index` | INTEGER >= 0 | the file number in its session |
| `sha256` | TEXT | 64 lower-case hex digits of the file |
| `size` | INTEGER >= 0 | bytes |
| `reference` | TEXT | the path when it was added (a hint) |
| `header` | TEXT | the file's header line (JSON) |
| `added_utc` | TEXT | when it was added |

### `sessions`

A walk test or a set of manual points on one floor (IND-20, GEO-02,
GEO-05; version 4); several per floor. A walk that changes floor goes on
in a new session on the other floor (`continues`). A floor with sessions
cannot be removed.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER, primary key | session id |
| `floor` | TEXT | `floors.id` |
| `kind` | TEXT | `walk` (point and click, GEO-02), `manual` (manual points, GEO-05) or `simulated` (points at the simulator's positions, RAD-19) |
| `name` | TEXT | its name |
| `started_utc`, `ended_utc` | TEXT | its date: start and end (`ended_utc` empty while it runs) |
| `radio` | TEXT | the radio: plugin, device model and serial |
| `calibration` | TEXT | the calibration in use (JSON) |
| `notes` | TEXT | free notes |
| `walk_mode` | TEXT | `point_and_click` or `continuous` |
| `recording_session` | TEXT | the recording session id of its recording, or empty |
| `plugins` | TEXT | provenance (PLG-12): JSON object, plugin id to version, of the plugins whose values it holds |
| `continues` | INTEGER or NULL | the session on the floor before a floor change |

### `points`

The measured points of a session (version 4): every sample that has a
plan position. This is the one point list of the project window and the
report (IND-30): both read it unchanged.

| Column | Type | Meaning |
| --- | --- | --- |
| `session` | INTEGER | `sessions.id`; removed with its session |
| `sequence` | INTEGER >= 1 | the point's number in its session, ascending in time |
| `mono_ns`, `utc_ns` | INTEGER | the sample's time: the pipeline clock (monotonic, the recording's `mono`) and UTC, nanoseconds |
| `x`, `y` | REAL | plan position in plan units of the floor (pixels for display) |
| `x_m`, `y_m` | REAL or NULL | the same in metres in the floor's plan frame (plan units x metres per unit), kept up to date with the floor's scale; NULL when the floor is not to scale |
| `kind` | TEXT | `walk`, `manual` or `simulated` |
| `grp` | INTEGER >= 0 | the walk segment number, or the manual point number |
| `cycles` | INTEGER >= 1 | measurement cycles in the point (manual points: averaged over N seconds) |
| `label` | TEXT | a manual point's label (e.g. a room) |
| `sample` | TEXT | the sample's values (JSON, [SURVEY.md](../SURVEY.md) "Sample values") |

When a floor's trim is edited, its points are mapped through the page to
the new trim, so they stay where they were measured on the sheet.

### `propagation`

The propagation model settings of the project (version 6, IND-40/41;
`Project::propagation()`): one row (`id` = 1), the start values of the
fit and the model of the coverage map. The dB per floor of the
inter-floor term is the building's (`building.floor_attenuation_db`,
`floors.slab_attenuation_db`, IND-44); wall losses are the wall-class
table.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER, 1 | the one row |
| `grid_m` | REAL 0.05 .. 10 | map grid cell, metres (default 0.25) |
| `max_reflections` | INTEGER 0 .. 3 | specular reflections traced (default 2) |
| `path_loss_exponent` | REAL 1 .. 8 | n beyond the reference distance (default 2) |
| `reference_distance_m` | REAL 0.01 .. 100 | d0: free space up to it (default 1) |
| `reflection_loss_db` | REAL 0 .. 60 | loss of a reflection off a wall without its own class value (default 6) |
| `rx_height_m` | REAL 0 .. 10 | receiver height above the floor (default 1.5) |
| `other_floors` | INTEGER 0/1 | antennas of other floors reach this floor through the slabs (default 1) |
| `default_eirp_dbm` | REAL -50 .. 80 | EIRP of an antenna without one (default 10) |

### `interpolations`

The stored interpolated maps (version 6, IND-41..45; `survey/interpolation.h`,
[PROPAGATION.md](../PROPAGATION.md) "Interpolation of a floor"): one per
floor and key, kept with what they were made from, so a result is shown
as stale when its inputs change and stays usable until run again
(IND-45). Removed with its floor.

| Column | Type | Meaning |
| --- | --- | --- |
| `id` | INTEGER | result id; never reused, also not when a result is replaced or removed |
| `floor_id` | TEXT | `floors.id` |
| `key` | TEXT, not empty | what it is for: the metric and the source group (unique per floor) |
| `created_utc`, `created_with` | TEXT | when and by which Yantra version it was made |
| `building_sha256`, `points_sha256`, `settings_sha256` | TEXT, 64 hex digits | fingerprints of the building model, the measured points used and the settings with the request |
| `parameters` | TEXT | JSON object: the request (metric, sessions, fit, residual and source-search options), the start and the fitted model parameters |
| `result` | TEXT | JSON object: the label (`interpolated`, or `interpolated, estimated sources`), fit quality (rms, points), fitted parameters with bounds, parameters not fitted and why, estimated sources, cells on the floor, the measured points with model value and residual |
| `origin_x`, `origin_y` | REAL | the grid's corner, plan-frame metres |
| `cell_m` | REAL > 0 | grid cell size, metres |
| `cols`, `rows` | INTEGER > 0 | grid size |
| `cells` | BLOB, cols x rows x 12 bytes | per cell, row by row: the map value, the fitted model (dBm, -infinity where no source reaches) and the confidence 0..1, each a little-endian IEEE 754 float |

### `acceptance_profiles`

The project's acceptance profiles (version 7, EXP-06; `survey/acceptance.h`,
[SURVEY.md](../SURVEY.md) "Acceptance profiles"), in the order the report
lists them. Replaced as a whole when they are changed.

| Column | Type | Meaning |
| --- | --- | --- |
| `position` | INTEGER >= 0, primary key | order in the report |
| `name` | TEXT, not empty, unique | the profile's name |
| `definition` | TEXT | JSON object `yantra-acceptance-profile` version 1: unit, basis (`points`, `tested_area`, `floor_area`), criteria (`threshold`, `min_percent`), `square_m`, `square_percentile`, `min_confidence` |

### `sqlite_sequence`

SQLite's own table of the last id given out in `interpolations` (version
6), so result ids are never reused.

| Column | Type | Meaning |
| --- | --- | --- |
| `name` | TEXT | table name (`interpolations`) |
| `seq` | INTEGER | the highest id given out so far |

## Example

A project with one PDF sheet and two floors, as the `sqlite3` tool shows
it (`sqlite3 survey.yantra .dump`, content shortened):

```sql
PRAGMA application_id = 1498305618;  -- 0x594E5452
PRAGMA user_version = 4;
INSERT INTO meta VALUES ('format', 'yantra-project');
INSERT INTO meta VALUES ('next_floor_number', '3');
INSERT INTO meta VALUES ('next_source_number', '2');
INSERT INTO meta VALUES ('schema_1_by', 'Yantra 0.1.0 2026-11-05T09:30:00Z');
INSERT INTO meta VALUES ('schema_2_by', 'Yantra 0.1.0 2026-11-05T09:30:00Z');
INSERT INTO meta VALUES ('schema_3_by', 'Yantra 0.1.0 2026-11-05T09:30:00Z');
INSERT INTO meta VALUES ('schema_4_by', 'Yantra 0.1.0 2026-11-05T09:30:00Z');
INSERT INTO project VALUES (1, 'Town hall survey', 'Town hall', 'Example network', '',
  '2026-11-05T09:30:00Z', '2026-11-05T09:41:12Z', 'Yantra 0.1.0', 'Yantra 0.1.0');
INSERT INTO building VALUES (1, 'Town hall', 15.0);
INSERT INTO sources VALUES (1, 'source-1', 'plans.pdf', 'application/pdf', 482113,
  '9f2c...e1a0', 'embedded', X'255044462D312E37...', '', '2026-11-05T09:31:02Z');
INSERT INTO source_pages VALUES ('source-1', 0, 1191.0, 842.0, 72.0);
INSERT INTO floors VALUES ('floor-1', 'Ground floor', 0, 3.6, 'source-1', 0, '',
  560.0, 400.0, NULL, '2026-11-05T09:33:40Z', 'rectangle', 0, 0.0,
  'two_points', 20.0, 380.0, 520.0, 380.0, 17.5, 0.0, '', 42.0, 61.5);
INSERT INTO floors VALUES ('floor-2', 'First floor', 1, 3.2, 'source-1', 0, '',
  400.0, 560.0, NULL, '2026-11-05T09:35:05Z', 'polygon', 1, -0.8,
  'shared', 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 'floor-1', 338.5, 40.0);
INSERT INTO floor_outline VALUES ('floor-1', 0, 20.0, 40.0), ('floor-1', 1, 580.0, 40.0),
  ('floor-1', 2, 580.0, 440.0), ('floor-1', 3, 20.0, 440.0);
INSERT INTO floor_outline VALUES ('floor-2', 0, 610.0, 40.0), ...;
INSERT INTO walls VALUES (1, 'floor-1', 0, 'wall-1', 'concrete', NULL);
INSERT INTO wall_points VALUES (1, 0, 280.0, 0.0), (1, 1, 280.0, 400.0);
INSERT INTO cells VALUES (1, 'cell-1', 0, 'Indoor DAS', 'tetra', 'tetra_cell', 390012500);
INSERT INTO cell_identity VALUES ('cell-1', 'colour_code', 1), ('cell-1', 'la', 100), ...;
INSERT INTO antennas VALUES (1, 'antenna-1', 'floor-1', 0, 'DAS 1', 'ceiling omni', 'indoor_das',
  120.0, 200.0, 2.8, 2.0, 'omni', 0.0, 65.0, 20.0, 0.0, 10.0, NULL, 0.0, 0.0, 'cell-1', NULL, NULL);
INSERT INTO recordings VALUES (1, 'walk_0000.yrec.zst', '3f0c9a1b2d4e5f60', 'walk', 0,
  '5b1d...07c2', 81233, '<project folder>/recordings/walk_0000.yrec.zst', '{"t":"header",...}', '2026-11-05T09:52:30Z');
INSERT INTO sessions VALUES (1, 'floor-1', 'walk', 'Ground floor walk', '2026-11-05T09:45:00Z',
  '2026-11-05T09:52:30Z', 'simulator 0.2.2, simulator:0', '{"key":"simulator|0|"}', '', 'point_and_click',
  '3f0c9a1b2d4e5f60', '{"simulator":"0.2.2","yantra.tetra":"0.1.0"}', NULL);
INSERT INTO points VALUES (1, 1, 100000000000, 1793872300000000000, 40.0, 200.0, 1.4, 7.0, 'walk', 1, 1, '',
  '{"carriers":[{"dev":"simulator:0","tech":"tetra","schema":"tetra_cell","role":"serving",...}]}');
```
