// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The indoor coverage and acceptance report of a project (EXP-05, EXP-06,
// IND-30, IND-43; docs/REPORT.md "Report content"), composed on the
// report engine (document.h) and written as PDF and HTML:
//   - a cover (project, site, network operator, the operator field, the
//     measurement dates, equipment) and the measurement metadata
//     (equipment, calibration, antenna correction, cell database, the walk
//     tests);
//   - a building summary: every floor's points together, a table per
//     floor and the acceptance profiles over the building;
//   - one section per floor: the plan with every measured point exactly as
//     the project window shows it (the shared point list, IND-30) and its
//     legend, the interpolated coverage map with its fit quality and the
//     label "interpolated" (IND-41, IND-43), the floor statistics with
//     "Share of measured points (%)" and "Share of floor area (interpolated) (%)" against
//     the thresholds, the acceptance profiles on both bases with pass or
//     fail, the RSSI histogram and CDF, per-cell statistics and the
//     serving-cell changes; optionally the point list as a table;
//   - the BS parameter verification and the neighbour cross-check against
//     the project's cell database (DB-04, DB-05).
// The composer is technology-free (PLG-01) and has no clock: the same
// project and options give the same document. Its texts go through the
// caller's translation (the app's tr()).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/celldb/network.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/celldb/verify.h"
#include "yantra/core/export/table.h"
#include "yantra/core/project/project.h"
#include "yantra/core/report/document.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/survey/weak_areas.h"

namespace yantra::measure {
class TechnologyCatalog;
}

namespace yantra::report {

/// English text -> the caller's language (the app passes its tr()).
using Translate = std::function<std::string(const std::string& english)>;

struct CoverageOptions {
    std::string title;     ///< "" = "Indoor coverage report" (translated)
    /// The operator field (EXP-05): who made the measurements, as typed in
    /// the report dialog. Printed as given, never stored; "" uses the
    /// surveyor of the project's About building tab (IND-47), if any.
    std::string surveyor;
    /// The equipment line; "" = the radios of the walk tests and recordings.
    std::string equipment;
    /// The project window's metric and colour scale: the points, values and
    /// colours of the report are the window's (IND-30).
    survey::Metric metric;
    std::optional<survey::ColourScale> scale;
    /// The floors in the report, in this order; empty = every floor in
    /// floor order.
    std::vector<std::string> floors;
    /// The good/bad threshold (EXP-05 "% of samples above") and further
    /// thresholds of the statistics tables, in the list's unit.
    double threshold = -95.0;
    std::vector<double> more_thresholds;
    double histogram_bin = 2.0;  ///< histogram bin width (dB), 0.5 .. 20
    /// The acceptance profiles (EXP-06); nullopt = the project's stored
    /// profiles, or survey::default_acceptance_profile() when it has none.
    std::optional<std::vector<survey::AcceptanceProfile>> profiles;
    /// Also print each floor's points as a table (sequence, plan position,
    /// value, serving cell): the detail view IND-30 allows.
    bool point_table = false;
    /// Channel numbers and the verification (DB-04): the loaded technology
    /// plugins; nullptr: none.
    const celldb::Technologies* technologies = nullptr;
    /// The cells received in the project's walks (observed_cells()), for
    /// the verification against the cell database; nullopt: no
    /// verification table (the cross-check then uses the database alone).
    std::optional<std::vector<celldb::ObservedCell>> observed;
    /// Yantra's data folders (fonts, logos, sheet descriptions; data/ in
    /// the tree, share/yantra installed).
    std::vector<std::filesystem::path> data_folders;
    /// The user's cache of downloaded licence-restricted plans (DEMO-12).
    std::filesystem::path sheet_cache;
    /// IND-47: the user's OpenStreetMap tile cache; the report's map is
    /// made of the cached tiles only (never fetched for a report). ""
    /// leaves the map out.
    std::filesystem::path tile_cache;
    std::string demo_label;  ///< DEMO-07: "" for a real project
    std::string lang = "en";
    Translate translate;     ///< empty: English
    std::string generator;   ///< "" = "Yantra <version>"
    /// Document::created_utc_ns (creation_time(now)); 0 = none, and no
    /// date in the footer.
    std::int64_t created_utc_ns = 0;
    int max_plan_px = 1400;  ///< picture size (exports::ImageOptions)
};

/// One basis of an acceptance profile on a floor or the building.
struct AcceptanceRow {
    std::string profile;
    survey::AcceptanceProfile::Basis basis = survey::AcceptanceProfile::Basis::Points;
    bool decides = false;  ///< the profile's own basis: its pass or fail
    survey::AcceptanceResult result;
};

/// A weak area of a failing profile (DEMO-16, survey/weak_areas.h): the
/// points of a floor below the profile's first failed threshold.
struct WeakAreaRow {
    std::string profile;
    std::string floor_id;
    std::string floor_name;
    double threshold = 0.0;
    std::string unit;
    survey::WeakArea area;
    std::string part;  ///< plan_part() of the area ("" without the plan size)
};

/// The points of one serving cell (EXP-05 per-cell statistics).
struct CellRow {
    std::string cell;  ///< identity text ("" = no serving cell)
    std::uint64_t frequency_hz = 0;
    std::size_t points = 0;
    double percent = 0.0;  ///< of the floor's points
    survey::CoverageStats stats;
};

/// Serving-cell changes from one cell to another (EXP-05).
struct ChangeRow {
    std::string from, to;
    std::uint64_t from_hz = 0, to_hz = 0;
    std::size_t count = 0;
};

struct FloorSection {
    std::string floor_id;
    std::string name;
    survey::PointList points;  ///< the project window's list (IND-30, TST-34)
    exports::Table table;      ///< its rows (the picture's points)
    std::size_t drawn_points = 0;  ///< points drawn in the floor picture
    survey::FloorStats stats;  ///< measured points: thresholds, cells, changes
    std::optional<survey::Interpolation> map;  ///< the stored map shown, if any
    std::optional<survey::CoverageSummary> summary;  ///< of the map (IND-43)
    std::vector<AcceptanceRow> acceptance;
    std::vector<CellRow> cells;
    std::vector<ChangeRow> changes;
    std::vector<std::string> warnings;
};

struct CoverageReport {
    Document document;
    std::vector<FloorSection> floors;
    survey::FloorStats building;  ///< every floor's points together
    std::vector<AcceptanceRow> acceptance;  ///< over the building
    std::vector<survey::AcceptanceProfile> profiles;
    bool default_profile = false;  ///< none given or stored: the default
    std::vector<WeakAreaRow> weak_areas;  ///< of the failing profiles, per floor, largest first
    std::optional<celldb::Verification> verification;
    std::vector<celldb::LogLine> verification_lines;
    std::vector<celldb::CrossCheckFinding> cross_check;
    std::vector<std::string> warnings;
};

/// Composes the report. ReportError for bad options or an unknown floor;
/// ProjectError as the point list.
CoverageReport coverage_report(const project::Project& project, const CoverageOptions& options);

/// The cells received in the project's walks: every recording of the
/// project replayed through the technology plugins of `catalog` (files
/// found as the exports find them: the path hint, next to the project or
/// in its recordings folder). A recording that cannot be found or replayed
/// is skipped with a warning.
std::vector<celldb::ObservedCell> observed_cells(const project::Project& project,
                                                 const std::shared_ptr<measure::TechnologyCatalog>& catalog,
                                                 const celldb::Technologies& technologies,
                                                 std::vector<std::string>* warnings = nullptr);

/// The report as PDF and HTML in one call (the report dialog, File >
/// Report, the sample report): composes it once and writes each file
/// that is not empty (write_report, atomically). ReportError when the
/// assets are missing or a file cannot be written.
CoverageReport write_coverage_report(const project::Project& project, const CoverageOptions& options,
                                     const std::filesystem::path& pdf, const std::filesystem::path& html);

}  // namespace yantra::report
