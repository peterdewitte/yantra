// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rows every export writes (section 10; docs/EXPORT.md): one row per
// measured point of a floor, taken from the shared point list
// (survey/points.h, IND-30), so a CSV, DXF or image export of a floor
// holds exactly the points of the project window, in the same order, with
// the same values and colours. A row carries the values of the point's
// stored sample: the serving carrier (level, C1, quality values, cell
// identity, channel), the neighbours, the serving-cell-change flag, the
// recording markers set since the previous point and the antenna
// correction of the radio. Technology-free (PLG-01): cell identity fields
// are whatever the technology schema marks as key fields; channel numbers
// come from the technology plugin's raster.
//
// Rows of a recording without plan positions (batch export, EXP-04) come
// from its replayed cycles (batch.h) and have no position.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/building/model.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/project/project.h"
#include "yantra/core/recording/format.h"
#include "yantra/core/survey/points.h"

namespace yantra::exports {

/// What an export could not do (bad options, an unknown floor, a file
/// that cannot be written, a damaged file read back).
class ExportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Writes `bytes` to `path` through a temporary file in the same folder
/// (never a half-written file under the final name); creates the folder.
void write_file(const std::filesystem::path& path, const std::string& bytes);

struct QualityValue {
    std::string field, unit;
    double value = 0.0;
    bool operator==(const QualityValue&) const = default;
};

/// One carrier of a point's sample.
struct CarrierValues {
    std::string role;  ///< serving, neighbour, neighbour_entry, other, scan
    std::string device, technology, schema;
    std::uint64_t frequency_hz = 0;
    std::optional<std::uint64_t> channel;  ///< from the technology's raster (nullopt: unknown)
    std::optional<double> level;
    std::string unit;  ///< of the level: dBm, or dBFS when uncalibrated
    std::optional<double> c1;
    std::vector<QualityValue> quality;
    /// The cell identity: the key fields without the frequency, as text,
    /// in the sample's order.
    std::vector<std::pair<std::string, std::string>> cell;
    std::optional<std::uint32_t> entry;  ///< neighbour_entry: list position
    const std::string* cell_value(const std::string& field) const;
    std::optional<double> quality_value(const std::string& field) const;
    bool operator==(const CarrierValues&) const = default;
};

struct Row {
    std::string floor_id;  ///< "" when the row has no floor (an unplaced recording)
    std::string floor_name;
    std::int64_t session_id = 0;   ///< 0 for rows of a replayed recording
    std::int64_t sequence = 0;     ///< point sequence, or the cycle number
    std::int64_t time_ns = 0;      ///< pipeline clock
    std::int64_t utc_ns = 0;
    std::optional<building::Point2> plan;    ///< plan units; nullopt: no position
    std::optional<building::Point2> metres;  ///< plan-frame metres; nullopt: not to scale or no position
    std::string kind;   ///< walk, manual, simulated; "cycle" for an unplaced recording row
    std::string label;  ///< manual point label
    std::optional<CarrierValues> serving;
    /// Measured neighbours by level (highest first), then the announced
    /// list entries by list position (as the hover readout, PP-01).
    std::vector<CarrierValues> neighbours;
    bool serving_change = false;  ///< the serving cell differs from the previous point's (PP-02 rule)
    /// The point list's value and colour (survey::point_list); nullopt
    /// value: no value for the metric.
    std::optional<double> value;
    std::string value_unit;
    std::uint32_t rgb = 0;
    /// Recording markers (REC-05) set after the previous row up to this
    /// one, "M<n> <label>", joined by "; "; "" none.
    std::string marker;
    /// The antenna correction added to every level of the serving radio
    /// (HW-09, calibration::AntennaCorrection::offset_db); nullopt: unknown.
    std::optional<double> antenna_correction_db;
    bool operator==(const Row&) const = default;
};

/// The rows of one floor (or one recording).
struct Table {
    std::string floor_id;
    std::string title;          ///< floor name, or the recording name
    survey::Metric metric;
    survey::ColourScale scale;  ///< the point list's scale
    std::vector<Row> rows;
    bool operator==(const Table&) const = default;
};

struct TableOptions {
    survey::Metric metric;                     ///< value and colour of each row (as the window)
    std::optional<survey::ColourScale> scale;  ///< nullopt: the point list's default
    std::vector<std::int64_t> sessions;        ///< of the floor; empty = all of them
    /// Channel numbers (EXP-01): the loaded technology plugins; nullptr or
    /// a technology not among them: no channel.
    const celldb::Technologies* technologies = nullptr;
    /// Read the markers of each session from its recording files (the
    /// project's `recordings` list; a path relative to the project is
    /// tried next to it). Files that cannot be read give no markers.
    bool read_markers = true;
};

/// The rows of a floor: exactly the floor's survey::point_list (count,
/// order, positions, values, colours; IND-30, TST-34). ExportError for an
/// unknown floor; ProjectError as point_list.
Table floor_table(const project::Project& project, const std::string& floor_id, const TableOptions& options = {});

/// The carriers of a stored sample (survey/sample.h JSON), with channel
/// numbers from `technologies`: the serving carrier of `device` ("" any)
/// and the neighbours in readout order.
struct SampleCarriers {
    std::optional<CarrierValues> serving;
    std::vector<CarrierValues> neighbours;
};
SampleCarriers sample_carriers(const nlohmann::json& sample, const std::string& device = {},
                               const celldb::Technologies* technologies = nullptr);

/// What a recording header says about a session's radios (antenna
/// correction, HW-09) and the markers of its files (REC-05).
struct RecordingExtras {
    std::vector<recording::AntennaInfo> antenna;
    std::vector<recording::MarkerLine> markers;  ///< time order
};
/// The antenna correction (dB added to the levels) of `device` of
/// `plugin`: its own entry, else the all-devices entry; nullopt: none
/// recorded.
std::optional<double> antenna_offset(const std::vector<recording::AntennaInfo>& antenna, const std::string& device);
/// Sets Row::marker and Row::antenna_correction_db of `rows` (one session
/// or recording, time order) from `extras`: a marker goes to the first row
/// at or after its time (markers after the last row go to the last row).
void apply_extras(std::vector<Row>& rows, const RecordingExtras& extras);

/// "2026-11-08T10:00:00.123Z" (utc) or the local time with its offset
/// "2026-11-08T11:00:00.123+01:00" (the TZ of the process).
std::string format_time(std::int64_t utc_ns, bool local);

/// The band of a value as survey::ColourScale::colour_of picks it: 1..n
/// (ascending `from`), 0 outside every band, nullopt without a value.
std::optional<std::size_t> band_of(const survey::ColourScale& scale, std::optional<double> value);

/// Shortest text of a double that reads back to the same value
/// (std::to_chars), "-0" written as "0".
std::string number_text(double v);

}  // namespace yantra::exports
