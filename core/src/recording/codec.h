// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the JSON lines of docs/formats/RECORDING.md, written and read
// (writer.cpp, reader.cpp). One function pair per line type.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/recording/format.h"

namespace yantra::recording::codec {

using nlohmann::json;

// Per file: the ids of the sources and corrections declared so far.
struct Declared {
    std::map<std::string, std::uint32_t> sources;
    std::map<std::string, std::uint32_t> corrections;
};

// The source id of a record of `schema`; appends its `source` line to
// `out` when it is new in the file.
std::uint32_t source_id(Declared& d, const measure::RadioRecord& r, const measure::RecordFileSchema& schema,
                        std::vector<std::string>& out);
// The id of a correction; appends its `cal` line when new.
std::uint32_t correction_id(Declared& d, const measure::LevelCorrection& c, std::vector<std::string>& out);
// A key for a record's source without building its schema (cache lookups).
std::string source_key(const measure::RadioRecord& r);

std::string header_line(const Header& h);
std::string input_line(Declared& d, const measure::PipelineInput& in, std::vector<std::string>& out);
std::string cycle_line(const measure::MeasurementCycle& c);
// Declares what the carriers need into `out`, then returns the state line.
std::string state_line(Declared& d, std::int64_t time_ns, const measure::PipelineState& s,
                       const measure::PipelineStats& stats, std::vector<std::string>& out);
std::string position_line(const PositionLine& p);
std::string floor_line(const FloorLine& f);
std::string marker_line(const MarkerLine& m);
std::string end_line(const EndLine& e);

// Reading: one parsed line into `file` (sources/corrections by id in
// `corrections`). false with `error` when the line is damaged; unknown
// line types are counted, not errors.
struct ReadContext {
    std::map<std::uint32_t, std::size_t> sources;  // id -> index in file.sources
    std::map<std::uint32_t, measure::LevelCorrection> corrections;
    std::map<std::uint32_t, std::shared_ptr<const std::vector<std::string>>> names;
};
bool read_header(const json& j, Header& h, std::string& error);
bool read_line(const json& j, File& file, ReadContext& c, std::string& error);

std::string iso_utc(std::int64_t utc_ns);
std::int64_t parse_iso_utc(const std::string& text);  // 0 when not ISO 8601 UTC

}  // namespace yantra::recording::codec
