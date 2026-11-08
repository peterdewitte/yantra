// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CSV export (EXP-01; docs/EXPORT.md "CSV"): one line per row of a
// table (table.h: the floor's shared point list, IND-30), selectable
// column groups in a chosen order and separator, time in UTC or local
// time, up to N neighbours, -999 for every invalid or missing value.
// Numbers use '.' and the shortest text that reads back to the same
// double, so plan positions survive a round trip unchanged (TST-33).
// Texts holding the separator, a quote or a line break are quoted
// (RFC 4180). read_csv reads such a file back.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/export/table.h"

namespace yantra::exports {

/// The value written for an invalid or missing value (EXP-01).
inline constexpr const char* kInvalidValue = "-999";

/// Column groups (EXP-01).
enum class Column {
    Time,               ///< time_utc or time_local
    Floor,              ///< floor (name)
    Position,           ///< x_m, y_m: plan-frame metres
    PlanPosition,       ///< x_plan, y_plan: plan units
    Cell,               ///< the serving cell's identity fields (e.g. la)
    Channel,            ///< channel (technology raster)
    Frequency,          ///< frequency_hz
    Level,              ///< rssi, rssi_unit
    C1,                 ///< c1_db
    Quality,            ///< one column per quality field (e.g. mer_db, ber_pct)
    ServingChange,      ///< serving_change: 1 when the serving cell changed
    Neighbours,         ///< nb<i>_<cell field>, nb<i>_channel, [nb<i>_frequency_hz,] nb<i>_rssi, nb<i>_c1_db
    Marker,             ///< marker
    AntennaCorrection,  ///< antenna_correction_db
    Label,              ///< label (manual points)
    Kind,               ///< kind (walk, manual, simulated, cycle)
    Session,            ///< session (project session id)
};
const char* column_id(Column c) noexcept;  ///< "time", "floor", "position", ...
std::optional<Column> column_from_id(std::string_view id) noexcept;
/// Every column group in the order above.
std::vector<Column> all_columns();
/// EXP-01's list: time, floor, position, cell, channel, level, C1,
/// quality, serving change, neighbours, marker, antenna correction.
std::vector<Column> default_columns();

struct CsvOptions {
    std::vector<Column> columns = default_columns();  ///< in this order; each at most once
    char separator = ',';                             ///< ',', ';', '\t' or '|'
    bool local_time = false;                          ///< time_local instead of time_utc
    std::size_t neighbours = 6;                       ///< N (0..32)
    /// The identity fields written; empty: every key field found in the
    /// rows, in order of first appearance.
    std::vector<std::string> cell_fields;
    /// The quality fields written; empty: every one found, in order of
    /// first appearance.
    std::vector<std::string> quality_fields;
    bool header = true;
};
/// "" when the options are usable, else what is wrong.
std::string validate(const CsvOptions& options);

/// The columns as resolved for a set of rows.
struct CsvLayout {
    std::vector<std::string> cell_fields;
    std::vector<std::string> quality_fields;
    std::vector<std::string> header;  ///< every column name, in order
};
CsvLayout csv_layout(const std::vector<Row>& rows, const CsvOptions& options);
/// The texts of one row (unquoted), one per layout column.
std::vector<std::string> csv_values(const Row& row, const CsvLayout& layout, const CsvOptions& options);
/// The whole file (header + one line per row, '\n' line ends). ExportError
/// for invalid options.
std::string csv_text(const std::vector<Row>& rows, const CsvOptions& options);
void write_csv(const std::filesystem::path& path, const std::vector<Row>& rows, const CsvOptions& options);

/// A CSV file read back (TST-33): the first line as header.
struct CsvTable {
    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    std::optional<std::size_t> column(std::string_view name) const;
};
/// ExportError for an unterminated quote or a line with another number of
/// fields than the header.
CsvTable read_csv(std::string_view text, char separator = ',');

}  // namespace yantra::exports
