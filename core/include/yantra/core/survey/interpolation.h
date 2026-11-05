// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolated coverage map of a floor (IND-41..45): the ray-tracing
// model (raytrace/model.h) fitted to the measured points of the shared
// point list (IND-30, points.h: the ONLY point source), its residuals
// interpolated over the floor, a confidence mask, estimated sources when
// no antenna reaches the floor (IND-42), the hover readout, the "% of
// floor area (interpolated)" statistics and acceptance basis next to the
// point statistics (IND-43, EXP-06), storage in the project with its
// parameters and input fingerprints, staleness, and a background job
// (IND-45). The drawing is the UI's (phase 9). docs/PROPAGATION.md
// "Interpolation of a floor".
//
// Always labelled "interpolated" and kept apart from the measured values:
// coverage_stats() (stats.h) counts measured points only; area_stats()
// and evaluate_area() here give the floor-area figures.
//
// Technology-free: the metric is a level in dBm; which source group (cell)
// a value belongs to follows from the metric (its carrier's cell) or is
// the strongest group.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/survey/interpolation.h,
// adapted (survey::Metric, plan-frame metres, per-cell carriers, the floor
// outline, the floor-area acceptance basis, a shared project in the job).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/project/project.h"
#include "yantra/core/raytrace/fit.h"
#include "yantra/core/raytrace/residuals.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/util/job.h"

namespace yantra::survey {

/// The label of everything interpolated (IND-43).
inline constexpr const char* kInterpolatedLabel = "interpolated";
inline constexpr const char* kEstimatedSourcesLabel = "estimated sources";
/// The label of the floor-area figures (IND-43, EXP-06).
inline constexpr const char* kFloorAreaLabel = "% of floor area (interpolated)";

/// The model parameters of the project settings (frequency unset: each
/// cell's carrier).
raytrace::ModelParams model_params(const project::PropagationSettings& s);

/// What to interpolate and how.
struct InterpolationRequest {
    std::string floor_id;
    /// A level metric (Metric::Value::Level); values in dBm are used.
    Metric metric;
    std::vector<std::int64_t> sessions;  ///< empty: all sessions of the floor
    /// One model frequency for every source; nullopt: each cell's carrier,
    /// and for sources without one (and estimated sources) the metric's
    /// frequency, else the median carrier of the points.
    std::optional<double> frequency_hz;
    /// Grid cell size; nullopt: the project's propagation setting.
    std::optional<double> grid_m;
    /// fit.group_key nullopt: the cell on the metric's carrier when the
    /// metric names one (Metric::Carrier::Frequency) and an antenna serves
    /// it, else the strongest group.
    raytrace::FitOptions fit;
    raytrace::ResidualOptions residuals;
    raytrace::SourceSearchOptions sources;  ///< when no antenna reaches the floor (IND-42)

    bool operator==(const InterpolationRequest&) const = default;
    /// The key a result is stored under: the metric and the source group.
    std::string key() const;
};

/// A measured point the fit used.
struct InterpolatedPoint {
    std::int64_t session_id = 0;
    std::int64_t sequence = 0;
    Point2 position_m;          ///< plan-frame metres
    double measured_dbm = 0.0;
    double model_dbm = 0.0;     ///< fitted model at the point
    double residual_db = 0.0;   ///< measured - model
    bool operator==(const InterpolatedPoint&) const = default;
};

/// An interpolated map, made by interpolate() or read from the project.
struct Interpolation {
    std::int64_t id = 0;  ///< stored id; 0 when not stored
    InterpolationRequest request;
    std::string created_utc;  ///< set when stored
    std::string created_with;
    double frequency_hz = 0.0;           ///< the points' / metric's carrier (0 when none was needed)
    std::string group_key;               ///< the source group compared ("" = the strongest)
    raytrace::ModelParams start_params;  ///< the project's settings
    raytrace::ModelParams params;        ///< after the fit (the map's parameters)
    raytrace::FitQuality quality;        ///< IND-41: rms error and point count
    std::vector<raytrace::FitParameter> fitted;
    std::vector<std::string> not_fitted;
    int iterations = 0;
    std::string stop_reason;
    /// IND-42: no antenna reached the floor; `sources` were estimated.
    bool estimated_sources = false;
    std::vector<building::Antenna> sources;
    std::vector<InterpolatedPoint> points;
    raytrace::Grid grid;
    /// Grid cells whose centre lies on the floor (inside the trim outline).
    std::size_t floor_cells = 0;
    /// Per cell (grid index order): the map (model + residual correction)
    /// and the fitted model in dBm (-infinity where no source reaches), and
    /// the confidence 0..1 (0 off the floor).
    std::vector<float> value_dbm;
    std::vector<float> model_dbm;
    std::vector<float> confidence;
    /// Input fingerprints (staleness).
    std::string building_sha256;
    std::string points_sha256;
    std::string settings_sha256;

    /// "interpolated" or "interpolated, estimated sources".
    std::string label() const;
    bool operator==(const Interpolation&) const = default;
};

/// The cells of `grid` whose centre lies on floor `f`: inside its trim
/// outline (rectangle or polygon, mapped to plan units), else inside the
/// plan rectangle (EXP-06 "floor area").
std::vector<bool> floor_cells(const building::Building& b, const building::Floor& f, const raytrace::Grid& grid);

/// Fingerprints of the current inputs of a request.
struct InputFingerprints {
    std::string building;
    std::string points;
    std::string settings;
    bool operator==(const InputFingerprints&) const = default;
};
InputFingerprints fingerprints(const project::Project& project, const InterpolationRequest& request);

/// Runs a request on the calling thread (plus `run.threads`). `stage`, if
/// set, is told each step ("Points", "Sources", "Fit", "Map",
/// "Residuals"). ProjectError NotFound for an unknown floor; Invalid for a
/// floor without scale, a metric that is not a level, invalid options, no
/// point with a level in dBm, or no frequency where one is needed. A
/// cancelled run returns nullopt. The same inputs give the same result
/// with any thread count and Isa.
std::optional<Interpolation> interpolate(const project::Project& project, const InterpolationRequest& request,
                                         const raytrace::RunControl& run = {},
                                         const std::function<void(const std::string&)>& stage = {});

/// Stores (replacing the result with the same floor and key) and returns
/// the id. Reading back gives the same Interpolation (with id and times).
std::int64_t store(project::Project& project, const Interpolation& interpolation);
/// The stored results of a floor (with their cells); ProjectError Damaged
/// for a stored result that cannot be read.
std::vector<Interpolation> stored_interpolations(const project::Project& project, const std::string& floor_id);
std::optional<Interpolation> stored_interpolation(const project::Project& project, std::int64_t id);
/// Conversion to and from the project's row (Damaged when it cannot be
/// read).
project::StoredInterpolation to_stored(const Interpolation& interpolation);
Interpolation from_stored(const project::StoredInterpolation& stored);

/// Staleness (IND-45): a result is stale when the building model, the
/// measured points or the settings/parameters differ from those it was
/// made from. It stays stored and usable, marked stale, until run again.
struct Staleness {
    bool stale = false;
    std::vector<std::string> reasons;  ///< e.g. "measured points changed"
};
Staleness staleness(const project::Project& project, const Interpolation& interpolation);

/// Hover readout (IND-43) at a position (plan-frame metres).
struct Readout {
    bool on_map = false;              ///< inside the grid
    std::optional<double> value_dbm;  ///< nullopt where no source reaches
    bool measured = false;            ///< over a measured point (within half a cell): its measured value
    std::string label;                ///< "measured" or "interpolated"
    double model_dbm = 0.0;
    double confidence = 0.0;
    /// The nearest measured point, when there is one.
    std::optional<InterpolatedPoint> nearest;
    double nearest_m = 0.0;
};
Readout readout(const Interpolation& interpolation, Point2 position_m);

/// The colour of every cell with the measured points' legend (IND-43):
/// the scale's colour of the value, alpha = confidence * 255 (0xAARRGGBB).
std::vector<std::uint32_t> cell_colours(const Interpolation& interpolation, const ColourScale& scale);

/// "% of floor area (interpolated)" (IND-43): the share of the assessed
/// area (cells on the floor with confidence >= min_confidence) at or above
/// each threshold, kept apart from the point statistics.
struct AreaShare {
    double threshold = 0.0;
    double area_m2 = 0.0;      ///< assessed area at or above
    double percent = 0.0;      ///< of the assessed area
    double percent_below = 0.0;
    bool operator==(const AreaShare&) const = default;
};
struct InterpolatedAreaStats {
    std::string label = kFloorAreaLabel;
    std::string unit = "dBm";
    double min_confidence = 0.5;
    double floor_area_m2 = 0.0;     ///< the floor's cells
    double assessed_area_m2 = 0.0;  ///< cells with enough confidence
    std::size_t assessed_cells = 0;
    std::vector<AreaShare> thresholds;
    bool estimated_sources = false;
    bool operator==(const InterpolatedAreaStats&) const = default;
};
InterpolatedAreaStats area_stats(const Interpolation& interpolation, const std::vector<double>& thresholds,
                                 double min_confidence = 0.5);

/// An acceptance profile with the FloorArea basis (EXP-06) over the maps
/// of one or more floors: a criterion's share is of the assessed cells of
/// all floors together (confidence >= profile.min_confidence); the result
/// gives the assessed cells and area. A profile of another basis, or a
/// unit other than dBm, is refused with std::invalid_argument; no assessed
/// cell does not pass.
AcceptanceResult evaluate_area(const AcceptanceProfile& profile, const std::vector<const Interpolation*>& floors);

/// Both figures side by side, as the report gives them (IND-43): the
/// measured points of the same selection (point_list, "% of measured
/// points") and the interpolated area.
struct CoverageSummary {
    CoverageStats measured;
    InterpolatedAreaStats interpolated;
    raytrace::FitQuality fit;
    bool stale = false;
};
CoverageSummary coverage_summary(const project::Project& project, const Interpolation& interpolation,
                                 const std::vector<double>& thresholds, double min_confidence = 0.5);

/// A request run in the background (IND-45, OA-03): reads the inputs,
/// runs, and stores the result in the project at the job's commit point,
/// so a cancelled job stores nothing. The project's change notification
/// of the store comes on the worker thread.
class InterpolationJob {
public:
    struct Options {
        bool store = true;
        unsigned threads = 0;  ///< worker threads of the run; 0: all
        raytrace::Isa isa = raytrace::Isa::Simd;
    };
    static std::unique_ptr<InterpolationJob> start(std::shared_ptr<project::Project> project,
                                                   InterpolationRequest request, Options options,
                                                   util::Job::Listener listener = {});
    ~InterpolationJob();
    InterpolationJob(const InterpolationJob&) = delete;
    InterpolationJob& operator=(const InterpolationJob&) = delete;

    util::JobStatus status() const;
    void cancel();
    void wait() const;
    /// The result once Done (with its stored id), else nullopt.
    std::optional<Interpolation> result() const;

private:
    InterpolationJob() = default;
    struct Shared;
    std::shared_ptr<Shared> shared_;
    std::unique_ptr<util::Job> job_;
};

}  // namespace yantra::survey
