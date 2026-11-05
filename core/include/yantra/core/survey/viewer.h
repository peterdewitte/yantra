// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The post-processing viewer model (PP-01; docs/SURVEY.md
// "Post-processing"): load one or more recordings into a project and
// show each data point, with a hover readout of its plan position,
// strongest cell, serving cell and all neighbour cells. The window (phase
// 9) draws the floor's shared point list (points.h, IND-30) and asks
// nearest_point / point_detail on hover.
//
// - import_recordings replays each recording (a sequence of files of one
//   recording session, REC-02/REC-07) through the measurement pipeline
//   (recording/replay.h) and its walk lines through a WalkTest
//   (walk_test.h), so the points are exactly those of the live walk test
//   (same positions, order and values). Every walk test of a recording
//   becomes a session (floor changes continue it, GEO-06); the sessions
//   carry the recording session id. A recording without walk lines has no
//   plan positions and gives no points. The files are listed in the
//   project (`recordings`).
// - A recording already in the project (a session with its recording
//   session id) is refused, so points are never doubled.
// - The readout comes from the point's stored sample only: what the point
//   shows is what the report shows.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/project/project.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/survey/points.h"

namespace yantra::survey {

struct ImportOptions {
    recording::ReplayOptions replay;  ///< technology catalog, calibration key (as the live run)
    bool list_files = true;           ///< add the files to the project's `recordings`
};

struct ImportedRecording {
    std::string recording_session;            ///< the files' session id
    std::string name;                         ///< the session name of the files
    std::vector<std::filesystem::path> files;  ///< in file order
    std::vector<std::int64_t> sessions;       ///< project sessions made, in order
    std::size_t points = 0;
    std::size_t mismatches = 0;               ///< replay cycles that differ from the recorded ones
    std::vector<std::int64_t> recording_ids;  ///< `recordings` rows (list_files)
};

/// Imports recordings (any order; files are grouped by recording session
/// and sorted by file number). Nothing is changed when a group cannot be
/// imported: ProjectError Exists for a recording already in the project,
/// NotFound for a walk on a floor the project does not have, Invalid for
/// a file that is not a recording or does not replay; Io when unreadable.
std::vector<ImportedRecording> import_recordings(const std::shared_ptr<project::Project>& project,
                                                 const std::vector<std::filesystem::path>& paths,
                                                 const ImportOptions& options = {});

/// One carrier as the readout shows it (from the stored sample).
struct CarrierReading {
    std::string role;  ///< serving, neighbour, other, scan, neighbour_entry
    std::string device, technology, schema;
    std::uint64_t frequency_hz = 0;
    std::optional<double> level;
    std::string unit;  ///< of the level
    std::optional<double> c1;
    struct Quality {
        std::string field, unit;
        double value = 0.0;
        bool operator==(const Quality&) const = default;
    };
    std::vector<Quality> quality;
    std::string cell;  ///< identity text ("" when the record had none)
    std::optional<std::uint32_t> entry;  ///< neighbour_entry: the list position
    std::string flags;
    bool operator==(const CarrierReading&) const = default;
};

/// The hover readout of a point (PP-01).
struct PointDetail {
    SurveyPoint point;         ///< as in the list (plan units, metres)
    std::string floor_id;
    std::string session_name;
    std::string label;         ///< manual point label
    std::uint32_t cycles = 1;  ///< cycles averaged (manual points)
    std::optional<CarrierReading> serving;
    std::optional<CarrierReading> strongest;  ///< highest measured level (not a list entry)
    /// Every neighbour cell: measured neighbours by level (highest first),
    /// then the announced list entries by list position.
    std::vector<CarrierReading> neighbours;
    std::vector<CarrierReading> others;  ///< other carriers and scan channels, by level
    bool operator==(const PointDetail&) const = default;
};

/// The readout of a list point; `device` limits it to one device's
/// carriers ("" any). ProjectError NotFound when the point is gone.
PointDetail point_detail(const project::Project& project, const SurveyPoint& point, const std::string& device = {});

/// The list point nearest to `plan` (plan units) within `max_distance`
/// (plan units, e.g. the marker radius); the first in list order on a
/// tie; nullopt when none is that close.
std::optional<std::size_t> nearest_point(const std::vector<SurveyPoint>& points, Point2 plan, double max_distance);

}  // namespace yantra::survey
