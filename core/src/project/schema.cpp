// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every schema version is one step here, never changed once released: a
// later version adds a step (and docs/formats/PROJECT.md a section).
// Copied from yantra-v2 68ea20a: core/src/project/schema.cpp, adapted for
// IND-01, IND-03, IND-04 (the tables of the building model's floors and
// sources, string ids, the building row).

#include "schema.h"

#include <string>

namespace yantra::project::schema {

namespace {

// Version 1 (step 7.1): project metadata, the building, its floors and the
// plan source files they use.
const char* const kV1 = R"sql(
CREATE TABLE meta (
  key   TEXT PRIMARY KEY NOT NULL,
  value TEXT NOT NULL
) WITHOUT ROWID;
CREATE TABLE project (
  id            INTEGER PRIMARY KEY CHECK (id = 1),
  name          TEXT NOT NULL CHECK (name <> ''),
  site          TEXT NOT NULL DEFAULT '',
  operator      TEXT NOT NULL DEFAULT '',
  notes         TEXT NOT NULL DEFAULT '',
  created_utc   TEXT NOT NULL,
  modified_utc  TEXT NOT NULL,
  created_with  TEXT NOT NULL,
  modified_with TEXT NOT NULL
);
CREATE TABLE building (
  id                   INTEGER PRIMARY KEY CHECK (id = 1),
  name                 TEXT NOT NULL DEFAULT '',
  floor_attenuation_db REAL NOT NULL DEFAULT 15 CHECK (floor_attenuation_db >= 0)
);
CREATE TABLE sources (
  row_id     INTEGER PRIMARY KEY,
  id         TEXT NOT NULL UNIQUE CHECK (id <> ''),
  file_name  TEXT NOT NULL,
  media_type TEXT NOT NULL,
  size       INTEGER NOT NULL CHECK (size >= 0),
  sha256     TEXT NOT NULL UNIQUE CHECK (length(sha256) = 64),
  storage    TEXT NOT NULL CHECK (storage IN ('embedded', 'reference')),
  content    BLOB,
  reference  TEXT NOT NULL DEFAULT '',
  added_utc  TEXT NOT NULL,
  CHECK ((storage = 'embedded') = (content IS NOT NULL))
);
CREATE TABLE floors (
  id                  TEXT PRIMARY KEY NOT NULL CHECK (id <> ''),
  name                TEXT NOT NULL CHECK (name <> ''),
  level               INTEGER NOT NULL UNIQUE,
  height_m            REAL NOT NULL CHECK (height_m > 0),
  source              TEXT REFERENCES sources (id) ON DELETE RESTRICT,
  page                INTEGER NOT NULL DEFAULT 0 CHECK (page >= 0),
  example_id          TEXT NOT NULL DEFAULT '',
  plan_width          REAL NOT NULL DEFAULT 0 CHECK (plan_width >= 0),
  plan_height         REAL NOT NULL DEFAULT 0 CHECK (plan_height >= 0),
  slab_attenuation_db REAL,
  created_utc         TEXT NOT NULL
);
CREATE INDEX floors_by_source ON floors (source);
)sql";

// Version 2 (step 7.2): the non-destructive trim of each floor (IND-05..07)
// and the page sizes of each source (IND-02). Existing floors keep the
// whole page (shape 'none').
const char* const kV2 = R"sql(
ALTER TABLE floors ADD COLUMN trim_shape TEXT NOT NULL DEFAULT 'none'
  CHECK (trim_shape IN ('none', 'rectangle', 'polygon'));
ALTER TABLE floors ADD COLUMN quarter_turns INTEGER NOT NULL DEFAULT 0 CHECK (quarter_turns BETWEEN 0 AND 3);
ALTER TABLE floors ADD COLUMN deskew_deg REAL NOT NULL DEFAULT 0 CHECK (deskew_deg BETWEEN -45 AND 45);
CREATE TABLE floor_outline (
  floor TEXT NOT NULL REFERENCES floors (id) ON DELETE CASCADE,
  idx   INTEGER NOT NULL CHECK (idx >= 0),
  x     REAL NOT NULL,
  y     REAL NOT NULL,
  PRIMARY KEY (floor, idx)
) WITHOUT ROWID;
CREATE TABLE source_pages (
  source TEXT NOT NULL REFERENCES sources (id) ON DELETE CASCADE,
  page   INTEGER NOT NULL CHECK (page >= 0),
  width  REAL NOT NULL CHECK (width > 0),
  height REAL NOT NULL CHECK (height > 0),
  units_per_inch REAL NOT NULL DEFAULT 0 CHECK (units_per_inch >= 0),
  PRIMARY KEY (source, page)
) WITHOUT ROWID;
)sql";

// Version 3 (step 7.3): the floor scale (IND-08) and alignment point
// (IND-09) in the floors row; the project's editable wall-class table
// with the default classes (IND-11), walls per floor as polylines, the
// cells and the antennas per floor (IND-12). Plan units throughout.
// Existing floors get no scale and no reference point.
const char* const kV3 = R"sql(
ALTER TABLE floors ADD COLUMN scale_method TEXT NOT NULL DEFAULT 'none'
  CHECK (scale_method IN ('none', 'two_points', 'scale_bar', 'factor', 'shared'));
ALTER TABLE floors ADD COLUMN scale_ax REAL NOT NULL DEFAULT 0;
ALTER TABLE floors ADD COLUMN scale_ay REAL NOT NULL DEFAULT 0;
ALTER TABLE floors ADD COLUMN scale_bx REAL NOT NULL DEFAULT 0;
ALTER TABLE floors ADD COLUMN scale_by REAL NOT NULL DEFAULT 0;
ALTER TABLE floors ADD COLUMN scale_distance_m REAL NOT NULL DEFAULT 0 CHECK (scale_distance_m >= 0);
ALTER TABLE floors ADD COLUMN scale_metres_per_unit REAL NOT NULL DEFAULT 0 CHECK (scale_metres_per_unit >= 0);
ALTER TABLE floors ADD COLUMN scale_shared_floor TEXT NOT NULL DEFAULT '';
ALTER TABLE floors ADD COLUMN reference_x REAL;
ALTER TABLE floors ADD COLUMN reference_y REAL CHECK ((reference_x IS NULL) = (reference_y IS NULL));
CREATE TABLE wall_classes (
  id                 TEXT PRIMARY KEY NOT NULL CHECK (id <> ''),
  position           INTEGER NOT NULL,
  name               TEXT NOT NULL DEFAULT '',
  reflection_loss_db REAL CHECK (reflection_loss_db IS NULL OR reflection_loss_db >= 0)
) WITHOUT ROWID;
CREATE TABLE wall_class_bands (
  class          TEXT NOT NULL REFERENCES wall_classes (id) ON DELETE CASCADE,
  idx            INTEGER NOT NULL CHECK (idx >= 0),
  frequency_mhz  REAL NOT NULL CHECK (frequency_mhz > 0),
  attenuation_db REAL NOT NULL CHECK (attenuation_db >= 0),
  PRIMARY KEY (class, idx)
) WITHOUT ROWID;
INSERT INTO wall_classes (id, position, name) VALUES
  ('concrete', 0, 'Concrete'), ('brick', 1, 'Brick'), ('plasterboard', 2, 'Plasterboard'),
  ('glass', 3, 'Glass'), ('wood', 4, 'Wood'), ('door', 5, 'Door'), ('metal', 6, 'Metal'),
  ('coated_glass', 7, 'Coated glass');
INSERT INTO wall_class_bands (class, idx, frequency_mhz, attenuation_db) VALUES
  ('concrete', 0, 400, 12), ('concrete', 1, 900, 23),
  ('brick', 0, 400, 8), ('brick', 1, 900, 10),
  ('plasterboard', 0, 400, 3), ('plasterboard', 1, 900, 6),
  ('glass', 0, 400, 2), ('glass', 1, 900, 4.5),
  ('wood', 0, 400, 3), ('wood', 1, 900, 6.5),
  ('door', 0, 400, 4), ('door', 1, 900, 9),
  ('metal', 0, 400, 25), ('metal', 1, 900, 25),
  ('coated_glass', 0, 400, 20), ('coated_glass', 1, 900, 20);
CREATE TABLE walls (
  row_id         INTEGER PRIMARY KEY,
  floor          TEXT NOT NULL REFERENCES floors (id) ON DELETE CASCADE,
  position       INTEGER NOT NULL,
  id             TEXT NOT NULL DEFAULT '',
  wall_class     TEXT REFERENCES wall_classes (id),
  attenuation_db REAL CHECK (attenuation_db IS NULL OR attenuation_db >= 0),
  CHECK (wall_class IS NOT NULL OR attenuation_db IS NOT NULL),
  UNIQUE (floor, position)
);
CREATE UNIQUE INDEX walls_by_id ON walls (floor, id) WHERE id <> '';
CREATE INDEX walls_by_class ON walls (wall_class);
CREATE TABLE wall_points (
  wall INTEGER NOT NULL REFERENCES walls (row_id) ON DELETE CASCADE,
  idx  INTEGER NOT NULL CHECK (idx >= 0),
  x    REAL NOT NULL,
  y    REAL NOT NULL,
  PRIMARY KEY (wall, idx)
) WITHOUT ROWID;
CREATE TABLE cells (
  row_id        INTEGER PRIMARY KEY,
  id            TEXT NOT NULL UNIQUE CHECK (id <> ''),
  position      INTEGER NOT NULL,
  label         TEXT NOT NULL DEFAULT '',
  technology_id TEXT NOT NULL DEFAULT '',
  schema_id     TEXT NOT NULL DEFAULT '',
  carrier_hz    INTEGER NOT NULL DEFAULT 0 CHECK (carrier_hz >= 0)
);
CREATE TABLE cell_identity (
  cell  TEXT NOT NULL REFERENCES cells (id) ON DELETE CASCADE,
  name  TEXT NOT NULL CHECK (name <> ''),
  value INTEGER NOT NULL,
  PRIMARY KEY (cell, name)
) WITHOUT ROWID;
CREATE TABLE antennas (
  row_id           INTEGER PRIMARY KEY,
  id               TEXT NOT NULL UNIQUE CHECK (id <> ''),
  floor            TEXT NOT NULL REFERENCES floors (id) ON DELETE CASCADE,
  position         INTEGER NOT NULL,
  name             TEXT NOT NULL DEFAULT '',
  model            TEXT NOT NULL DEFAULT '',
  type             TEXT NOT NULL CHECK (type IN ('indoor_das', 'outdoor_donor')),
  x                REAL NOT NULL,
  y                REAL NOT NULL,
  height_m         REAL NOT NULL,
  gain_dbi         REAL NOT NULL,
  pattern          TEXT NOT NULL CHECK (pattern IN ('omni', 'sector', 'table')),
  azimuth_deg      REAL NOT NULL DEFAULT 0,
  beamwidth_deg    REAL NOT NULL DEFAULT 65,
  front_to_back_db REAL NOT NULL DEFAULT 20,
  step_deg         REAL NOT NULL DEFAULT 0,
  eirp_dbm         REAL,
  feed_power_dbm   REAL,
  cable_loss_db    REAL NOT NULL DEFAULT 0 CHECK (cable_loss_db >= 0),
  splitter_loss_db REAL NOT NULL DEFAULT 0 CHECK (splitter_loss_db >= 0),
  cell             TEXT REFERENCES cells (id),
  direction_deg    REAL,
  elevation_deg    REAL CHECK (elevation_deg IS NULL OR elevation_deg BETWEEN -90 AND 90),
  UNIQUE (floor, position)
);
CREATE INDEX antennas_by_cell ON antennas (cell);
CREATE TABLE antenna_pattern_gains (
  antenna INTEGER NOT NULL REFERENCES antennas (row_id) ON DELETE CASCADE,
  idx     INTEGER NOT NULL CHECK (idx >= 0),
  gain_db REAL NOT NULL,
  PRIMARY KEY (antenna, idx)
) WITHOUT ROWID;
INSERT OR IGNORE INTO meta (key, value) VALUES
  ('next_wall_number', '1'), ('next_antenna_number', '1'), ('next_cell_number', '1');
)sql";

}  // namespace

void migrate(sql::Db& db, int from, int to, const std::string& now, const std::string& tool,
             const std::vector<std::string>& extra, const std::function<void(std::string_view)>& fault) {
    for (int v = from + 1; v <= to; ++v) {
        sql::Transaction tx(db);
        const std::string what = "schema version " + std::to_string(v);
        if (v == 1) {
            db.exec(kV1, what);
            db.exec("INSERT INTO meta (key, value) VALUES ('format', 'yantra-project'), "
                    "('next_floor_number', '1'), ('next_source_number', '1'); "
                    "INSERT INTO building (id) VALUES (1)",
                    what);
        } else if (v == 2) {
            db.exec(kV2, what);
        } else if (v == 3) {
            db.exec(kV3, what);
        } else if (v > kSchemaVersion && static_cast<std::size_t>(v - kSchemaVersion) <= extra.size()) {
            db.exec(extra[static_cast<std::size_t>(v - kSchemaVersion - 1)], what);
        } else {
            throw ProjectError(ProjectError::Code::TooNew, "no migration to schema version " + std::to_string(v));
        }
        // Who migrated it, for support questions.
        sql::Stmt note(db, "INSERT OR REPLACE INTO meta (key, value) VALUES (?1, ?2)");
        note.bind(1, "schema_" + std::to_string(v) + "_by").bind(2, tool + " " + now).run();
        db.exec("PRAGMA user_version = " + std::to_string(v), what);
        if (fault) fault("migrate:step");
        tx.commit();
    }
}

}  // namespace yantra::project::schema
