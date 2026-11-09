// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target celldb_csv (TST-12): cell database CSV import (DB-02,
// docs/formats/CELL_DATABASE.md, "yantra-cells" v1). Either CellDbError for
// unusable text or entries plus per-row problems; the imported entries
// export again into CSV that imports to the same number of entries.

#include "fuzz.hpp"

#include "yantra/core/celldb/csv.h"
#include "yantra/core/celldb/database.h"

namespace cdb = yantra::celldb;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;
    cdb::CsvOptions options;
    options.hex_fields = {"la"};
    cdb::CsvImport in;
    try {
        in = cdb::import_csv(yantra::fuzz::text_of(data, size), options);
    } catch (const cdb::CellDbError&) {
        return 0;
    }
    cdb::CellDatabase db;
    try {
        (void)db.merge(in.entries);  // as the import does
    } catch (const cdb::CellDbError&) {
        return 0;  // an entry the database refuses (all or nothing)
    }
    const std::string text = cdb::export_csv(db, options);
    cdb::CsvImport again;
    try {
        again = cdb::import_csv(text, options);
    } catch (const cdb::CellDbError&) {
        fail("an exported cell database is refused on import");
    }
    if (again.entries.size() != db.entries().size()) fail("cell database CSV round trip changed the entry count");
    return 0;
}
