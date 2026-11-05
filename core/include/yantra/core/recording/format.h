// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Yantra's recording format "yantra-recording/1" (REC-02, REC-03, REC-05,
// REC-10, NFR-05, DOC-12): the reference is docs/formats/RECORDING.md.
// A recording file holds the measurement pipeline's inputs (every radio
// record and event as the pipeline took it in, with its number, arrival
// time and level correction), its cycles, keyframes of its state, plan
// positions, floor changes and markers, so a replay (replay.h) drives the
// same pipeline and gives the same cycles (REC-07). JSON Lines, one
// object per line, optionally as zstd frames.
//
// This header is the in-memory form of one file (reader.h reads it,
// writer.h writes it). Technology-neutral (PLG-01): records keep their
// radio schema, declared in the file.
// Ideas (JSON Lines, numbered files, zstd frames per flush, markers) from
// yantra-v2 68ea20a: core/include/yantra/core/recording/format.h and
// docs/formats/RECORDING.md, rewritten for the radio API's records and
// the measurement pipeline's inputs.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/measure/record_file.h"

namespace yantra::recording {

inline constexpr const char* kFormat = "yantra-recording";
inline constexpr int kFormatVersion = 1;
inline constexpr const char* kExtension = ".yrec";            // JSON Lines
inline constexpr const char* kCompressedExtension = ".yrec.zst";  // the same as zstd frames
// A line longer than this is damaged (bounded reading of hostile files).
inline constexpr std::size_t kMaxLineBytes = 16u << 20;

enum class Compression { None, Zstd };

// A radio device in use (REC-03: SDR model/serial; no personal data).
struct RadioInfo {
    std::string plugin_id;
    std::string plugin_version;
    std::string device_key;
    std::string driver;   // model / driver
    std::string serial;
    std::string label;
    bool operator==(const RadioInfo&) const = default;
};

// A calibration table entry in use (REC-03, RF-02: date and serial).
struct CalibrationInfo {
    std::string key;          // calibration::Key::id()
    std::int64_t created_unix = 0;
    int valid_days = 0;
    std::string method;
    bool simulated = false;
    bool operator==(const CalibrationInfo&) const = default;
};

// An antenna correction in use (REC-03, HW-09); empty plugin/device = all devices.
struct AntennaInfo {
    std::string plugin_id;
    std::string device_key;
    double gain_dbi = 0.0;
    double cable_loss_db = 0.0;
    double attenuator_db = 0.0;
    bool operator==(const AntennaInfo&) const = default;
};

// The project and floor a recording belongs to (IND-20).
struct ProjectLink {
    std::string file;       // project file name (no folder)
    std::string floor_id;   // floor at the start ("" none)
    bool operator==(const ProjectLink&) const = default;
};

// What the application puts into every header (REC-03).
struct HeaderInfo {
    std::vector<RadioInfo> radios;
    std::vector<CalibrationInfo> calibration;
    std::vector<AntennaInfo> antenna;
    std::string cell_db;    // cell database in use ("" none)
    std::string notes;      // operator notes
    std::optional<ProjectLink> project;
    bool operator==(const HeaderInfo&) const = default;
};

// For a file cut from another (REC-08).
struct CutInfo {
    std::int64_t from_ns = 0;  // replay time of the source
    std::int64_t to_ns = 0;
    std::string source;        // source file name
    std::string source_session;
};

// The first line of every file.
struct Header {
    int version = kFormatVersion;
    std::string tool_name = "Yantra";
    std::string tool_version;
    std::string session_id;     // 16 hex digits, the same in every file of a session
    std::string session_name;   // file name stem
    std::uint32_t file_index = 0;
    std::int64_t session_started_utc_ns = 0;
    bool continued = false;     // first file after the session was continued (REC-02)
    std::int64_t started_utc_ns = 0;   // this file
    std::int64_t started_mono_ns = 0;  // this file, on the pipeline clock
    std::uint32_t next_marker = 1;
    HeaderInfo info;
    measure::PipelineConfig pipeline;  // the configuration of the recorded pipeline
    double split_minutes = 0.0;
    std::uint64_t split_messages = 0;
    double keyframe_s = 0.0;
    Compression compression = Compression::None;
    std::optional<CutInfo> cut;
};

// A radio schema of one radio plugin version, declared once per file
// before the first line that uses it.
struct Source {
    std::uint32_t id = 0;            // 1, 2, ... within the file
    std::string plugin_id;
    std::string plugin_version;
    std::string technology_id;
    std::uint32_t capabilities = 0;  // YR_CAP_*
    std::uint32_t schema_index = 0;
    std::shared_ptr<const measure::RadioSchemaView> schema;
};

// One input of the pipeline (`rec` or `event` line).
struct InputLine {
    std::uint64_t sequence = 0;
    std::int64_t arrival_ns = 0;
    std::optional<measure::RadioRecord> record;  // radio_schema = its source's view
    std::optional<measure::RadioEvent> event;
    bool corrected = false;
    measure::LevelCorrection correction;
};

// One sample of a `cycle` line (the summary written for other tools and
// for checking a replay).
struct CycleSample {
    std::string device_key;
    std::string schema_id;
    measure::CarrierRole role = measure::CarrierRole::Other;
    std::uint64_t frequency_hz = 0;
    std::uint64_t entry_index = 0;
    double level = 0.0;  // NaN when none
    std::uint32_t flags = 0;
    std::uint32_t records_in_cycle = 0;
    std::int64_t age_ns = 0;
};

struct CycleLine {
    std::uint64_t sequence = 0;
    std::int64_t time_ns = 0;   // monotonic end of the period
    std::int64_t utc_ns = 0;
    std::int64_t built_ns = 0;  // tick
    std::uint64_t inputs = 0;
    std::int64_t period_ns = 0;
    std::uint32_t flags = 0;
    std::vector<CycleSample> samples;
};

// A keyframe of the pipeline state (`state` line).
struct StateLine {
    std::int64_t time_ns = 0;  // when it was taken (pipeline clock)
    measure::PipelineState state;
    measure::PipelineStats stats;
};

// A plan position (IND-20, GEO-02/05): plan units of the floor and, when
// the floor is to scale, metres.
struct PositionLine {
    std::int64_t time_ns = 0;
    std::string floor_id;
    double x = 0.0, y = 0.0;        // plan units
    std::optional<double> x_m, y_m; // metres
    std::string kind;               // "click", "point", "simulated", "cursor"
    std::string label;
};

struct FloorLine {
    std::int64_t time_ns = 0;
    std::string floor_id;
    std::string name;
};

enum class MarkerAction { Mark, NewFile, NewFilePause };
const char* marker_action_name(MarkerAction action) noexcept;  // "mark", "new_file", "new_file_pause"

struct MarkerLine {
    std::int64_t time_ns = 0;
    std::uint32_t number = 0;
    std::string label;
    MarkerAction action = MarkerAction::Mark;
};

struct EndLine {
    std::int64_t time_ns = 0;
    std::string reason;          // stop, split_time, split_count, new_file, new_file_pause
    std::uint64_t messages = 0;  // rec + event lines of the file
    std::uint64_t inputs = 0;    // the last input number taken in
};

// A whole file as read.
struct File {
    std::filesystem::path path;
    Header header;
    std::vector<Source> sources;
    std::vector<InputLine> inputs;      // file order
    std::vector<CycleLine> cycles;      // file order
    std::vector<StateLine> states;      // file order; the first is the start
    std::vector<PositionLine> positions;
    std::vector<FloorLine> floors;
    std::vector<MarkerLine> markers;
    std::optional<EndLine> end;         // absent: not closed cleanly
    bool truncated = false;             // the last line (or zstd frame) was cut
    std::size_t damaged_lines = 0;      // lines in the middle that could not be read (skipped)
    std::size_t unknown_lines = 0;      // lines of a type this version does not know (skipped)
    std::vector<std::string> warnings;  // what was skipped, with line numbers
    std::uint64_t bytes = 0;            // file size

    // The last time in the file (pipeline clock).
    std::int64_t last_time_ns() const;
};

// File names of a session (REC-02): "<name>_0000.yrec" (or .yrec.zst).
std::string file_name(const std::string& session, std::uint32_t index, Compression compression);
// Quick-record session name from a UTC time: "rec_YYYYMMDD_HHMMSS".
std::string quick_name(std::int64_t utc_ns);
// A session name is usable as a file name stem (1..120 characters, no
// path separators or control characters).
bool valid_session_name(const std::string& name);

}  // namespace yantra::recording
