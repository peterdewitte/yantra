// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Import and export of the cell database as CSV (DB-02, TST-33, NFR-05;
// docs/CELL_DATABASE.md "CSV" has the columns, rules and an example). The
// CSV file is also the form of a shared cell database outside a project.
//
// - Optional first line `#yantra-cells,1` (the format and its version;
//   written on export). A higher version is refused; a file without it is
//   read as version 1 (e.g. typed in a spreadsheet).
// - A header line, then one cell per line, RFC 4180 quoting (also for
//   line breaks in notes), UTF-8 (a byte order mark is skipped), CRLF on
//   export; separator ',' or ';' (detected from the header on import).
// - Columns: id, label, technology, schema, site_name, sector,
//   azimuth_deg, colour, notes, origin, plugin_version; frequency_hz (the
//   main carrier, Hz) and/or channel (the technology's channel number,
//   converted by its plugin); every other column is a technology field by
//   name (`field:<name>` when the name is one of these columns);
//   `neighbours.<name>` columns hold the neighbour list, one value per
//   neighbour separated by '|', aligned across the columns
//   (`neighbours.channel` converted like channel).
// - Integers are read in decimal or hexadecimal ("0x12C") in every column
//   ("LA in hex or decimal"); the export writes the fields named in
//   hex_fields in hexadecimal.
// - A row that cannot be used is skipped and reported with its line
//   number; the file as a whole is refused (CellDbError) when it has no
//   header, a column twice, an open quote, a newer version, or more than
//   max_bytes / kMaxEntries.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/celldb/technology.h"

namespace yantra::celldb {

/// The format name and version of the first line.
inline constexpr const char* kCsvFormat = "yantra-cells";
inline constexpr int kCsvVersion = 1;

struct CsvOptions {
    /// ',' or ';'; 0: ',' on export, detected from the header on import.
    char separator = 0;
    /// Integer fields written in hexadecimal (cells and neighbour columns), e.g. {"la"}.
    std::vector<std::string> hex_fields;
    /// Technology and schema of rows that name none ("" = such rows have none).
    std::string technology;
    std::string schema;
    /// Plugins for channel <-> frequency (channel columns); nullptr: a
    /// channel column cannot be read and none is written.
    const Technologies* technologies = nullptr;
    /// Largest input accepted, bytes.
    std::size_t max_bytes = std::size_t{64} << 20;
};

struct CsvImport {
    std::vector<CellEntry> entries;     ///< origin Import unless the file names one
    std::vector<std::string> problems;  ///< "line 7: ..." per skipped row, ignored columns
};

/// The database as CSV text (entries in list order).
std::string export_csv(const CellDatabase& db, const CsvOptions& options = {});
/// Reads CSV text; CellDbError when the text as a whole is unusable.
CsvImport import_csv(std::string_view text, const CsvOptions& options = {});

/// Reads a CSV file into a new database (the import's entries, merged in
/// file order); the problems go to `problems` when given.
CellDatabase read_csv_file(const std::string& path, const CsvOptions& options = {},
                           std::vector<std::string>* problems = nullptr);
/// Writes the database as a CSV file (a temporary file renamed over it).
void write_csv_file(const CellDatabase& db, const std::string& path, const CsvOptions& options = {});

}  // namespace yantra::celldb
