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
| `PRAGMA user_version` | schema version, now **1** |
| `meta.format` | `yantra-project` |

| Version | Change |
| --- | --- |
| 1 | `meta`, `project`, `building`, `sources`, `floors` |

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
model and the source files. The rest of the model and the later data come
as new versions in the steps that add them; until then the API refuses
such data with "cannot be stored in project schema 1 yet" (error
`Unsupported`), so nothing is ever dropped silently.

| Step | Planned content |
| --- | --- |
| 7.2 plan import | floor trims (IND-05..07): `floors` columns `trim_shape`, `quarter_turns`, `deskew_deg` and a table `floor_outline` (floor, index, x, y in page units); PDF page count and page sizes per source |
| 7.3 scale, alignment, walls, antennas | `floors` scale columns (method, two points, distance, metres per unit, shared floor) and `reference_x/y` (IND-08/09); tables `wall_classes` + `wall_class_bands` (the project's table, IND-11), `walls` + `wall_points`, `cells` + `cell_identity`, `antennas` (IND-12) |
| 7.4 recordings | `recordings` (one row per recording file kept in the project: name, SHA-256, size, content or reference, header JSON; REC-*) |
| 7.5 walk tests and points | `provenance` (plugin id, version, technology id; PLG-12), `sessions` (walk or manual, floor, radio, calibration, notes; IND-20), `samples` (session, sequence, time, plan position in metres and plan units, sample JSON) — the one point list of IND-30 |
| 7.6 cell database | a project copy or reference of the cell database (DB-*) |
| 8 interpolation | stored interpolation results with their parameters (IND-45) |

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

## Tables (version 1)

### `meta`

Key/value pairs of the file itself.

| Column | Type | Meaning |
| --- | --- | --- |
| `key` | TEXT, primary key | name |
| `value` | TEXT | value |

Keys: `format` (`yantra-project`), `next_floor_number` and
`next_source_number` (the number of the next generated id; ids are never
reused, also not after a removal), `schema_<n>_by` (who migrated to
version n, and when).

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
once: adding the same content again gives the existing source.

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
| `reference` | TEXT | `reference` only: the cached file's absolute path when it was added (a hint) |
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

## Example

A project with one PDF sheet and two floors, as the `sqlite3` tool shows
it (`sqlite3 survey.yantra .dump`, content shortened):

```sql
PRAGMA application_id = 1498305618;  -- 0x594E5452
PRAGMA user_version = 1;
INSERT INTO meta VALUES ('format', 'yantra-project');
INSERT INTO meta VALUES ('next_floor_number', '3');
INSERT INTO meta VALUES ('next_source_number', '2');
INSERT INTO meta VALUES ('schema_1_by', 'Yantra 0.1.0 2026-11-05T09:30:00Z');
INSERT INTO project VALUES (1, 'Town hall survey', 'Town hall', 'Example network', '',
  '2026-11-05T09:30:00Z', '2026-11-05T09:41:12Z', 'Yantra 0.1.0', 'Yantra 0.1.0');
INSERT INTO building VALUES (1, 'Town hall', 15.0);
INSERT INTO sources VALUES (1, 'source-1', 'plans.pdf', 'application/pdf', 482113,
  '9f2c...e1a0', 'embedded', X'255044462D312E37...', '', '2026-11-05T09:31:02Z');
INSERT INTO floors VALUES ('floor-1', 'Ground floor', 0, 3.6, 'source-1', 0, '',
  1191.0, 842.0, NULL, '2026-11-05T09:33:40Z');
INSERT INTO floors VALUES ('floor-2', 'First floor', 1, 3.2, 'source-1', 0, '',
  1191.0, 842.0, NULL, '2026-11-05T09:35:05Z');
```
