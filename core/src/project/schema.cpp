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
