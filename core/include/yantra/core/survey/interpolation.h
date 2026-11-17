// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolated coverage map of a floor (IND-41, IND-43, IND-45): an
// inverse-distance-weighted (IDW) interpolation of the measured levels of
// the shared point list (IND-30, points.h: the ONLY point source) on a
// grid over the floor, a confidence mask, the hover readout, the "Share of
// floor area (interpolated) (%)" statistics and acceptance basis next to
// the point statistics (IND-43, EXP-06), storage in the project with its
// parameters and input fingerprints, staleness, and a background job
// (IND-45). No walls, antennas or propagation model: only the measured
// points. The drawing is the UI's. docs/INTERPOLATION.md.
//
// The map (all in dB, plan-frame metres):
//   - a grid cell holding measured points takes their level (the median
//     when several; the mean of the two middle ones for an even count);
//   - any other cell takes sum(w_i v_i) / sum(w_i), w_i = 1 / d_i^power,
//     over the points i within the search radius of the cell centre;
//     a cell with no point within the radius has no value (-infinity);
//   - confidence (IND-43): 1 up to full_confidence_m from the nearest
//     point, falling linearly to 0 at the search radius; 0 off the floor
//     (outside its trim outline) and where there is no value.
//
// Always labelled "interpolated" and kept apart from the measured values:
// coverage_stats() (stats.h) counts measured points only; area_stats()
// and evaluate_area() here give the floor-area figures.
//
// Interface copied from yantra-v2 68ea20a: core/include/yantra/core/survey/interpolation.h,
// adapted (IDW instead of the ray-tracing model, Yantra 0.4).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/project/project.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/util/job.h"

namespace yantra::survey {

/// The label of everything interpolated (IND-43).
inline constexpr const char* kInterpolatedLabel = "interpolated";
/// The label of the floor-area figures (IND-43, EXP-06).
inline constexpr const char* kFloorAreaLabel = "Share of floor area (interpolated) (%)";

/// Which inner loop the interpolation uses for the distances (the hot
/// path). Both give the same results bit for bit; Simd falls back to
/// Scalar when the build has no SIMD kernels (YANTRA_SIMD=OFF).
enum class Isa { Scalar, Simd };

/// True when this build has the SIMD kernels (YANTRA_SIMD=ON).
bool simd_built() noexcept;

/// How a run is executed: results do not depend on it (any thread count
/// and Isa give the same values).
struct RunControl {
    Isa isa = Isa::Simd;
    /// Worker threads including the calling one; 0: hardware threads.
    unsigned threads = 0;
    /// Set to true (from any thread) to stop; the run returns early.
    const std::atomic<bool>* cancel = nullptr;
    /// Called on the calling thread with the done fraction (0 .. 1),
    /// non-decreasing, at most about 200 times per run.
    std::function<void(double)> progress;
};

/// A grid over a floor in plan-frame metres: cell (col, row) covers
/// [origin + (col, row) * cell_m, origin + (col + 1, row + 1) * cell_m).
struct Grid {
    Point2 origin_m;
    double cell_m = 0.5;
    int cols = 0;
    int rows = 0;

    bool operator==(const Grid&) const = default;

    std::size_t size() const { return static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows); }
    std::size_t index(int col, int row) const {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
    }
    Point2 centre(int col, int row) const {
        return {origin_m.x + (col + 0.5) * cell_m, origin_m.y + (row + 0.5) * cell_m};
    }
    /// The cell holding a point, or nullopt outside the grid.
    std::optional<std::size_t> cell_at(Point2 m) const;
};

/// Most grid cells of one map.
inline constexpr std::size_t kMaxGridCells = 16'000'000;

/// The grid of floor `f`: its plan rectangle (0, 0) .. plan size in
/// plan-frame metres; without a plan size the bounding box of `points_m`
/// plus 1 m. std::logic_error without scale; std::invalid_argument for a
/// cell size outside 0.05 .. 10 m, nothing to cover or more than
/// kMaxGridCells cells.
Grid floor_grid(const building::Building& b, const building::Floor& f, double cell_m,
                const std::vector<Point2>& points_m = {});

/// One measured value for idw().
struct IdwSample {
    Point2 position_m;
    double value_db = 0.0;
    bool operator==(const IdwSample&) const = default;
};

/// The IDW field over a grid (IND-41): per cell in grid index order the
/// value (-infinity where no point is within the radius), the confidence
/// 0 .. 1 and 1 for the cells holding a measured point.
struct IdwField {
    std::vector<float> value_db;
    std::vector<float> confidence;
    std::vector<std::uint8_t> measured;
    bool cancelled = false;
};

/// The field of `samples` over `grid` with `settings` (the rules at the
/// top). std::invalid_argument for invalid settings. Any Isa and thread
/// count give the same values bit for bit.
IdwField idw(const Grid& grid, const std::vector<IdwSample>& samples, const project::InterpolationSettings& settings,
             const RunControl& run = {});

/// What to interpolate and how.
struct InterpolationRequest {
    std::string floor_id;
    /// A level metric (Metric::Value::Level); values in dBm are used.
    Metric metric;
    std::vector<std::int64_t> sessions;  ///< empty: all sessions of the floor
    /// The settings; nullopt: the project's (Project::interpolation_settings).
    std::optional<project::InterpolationSettings> settings;

    bool operator==(const InterpolationRequest&) const = default;
    /// The key a result is stored under: the metric.
    std::string key() const;
};

/// A measured point the map used.
struct InterpolatedPoint {
    std::int64_t session_id = 0;
    std::int64_t sequence = 0;
    Point2 position_m;  ///< plan-frame metres
    double measured_dbm = 0.0;
    bool operator==(const InterpolatedPoint&) const = default;
};

/// An interpolated map, made by interpolate() or read from the project.
struct Interpolation {
    std::int64_t id = 0;  ///< stored id; 0 when not stored
    InterpolationRequest request;
    std::string created_utc;  ///< set when stored
    std::string created_with;
    /// The settings the map was made with (the map's parameters).
    project::InterpolationSettings params;
    std::vector<InterpolatedPoint> points;
    Grid grid;
    /// Grid cells whose centre lies on the floor (inside the trim outline).
    std::size_t floor_cells = 0;
    /// Per cell (grid index order): the map in dBm (-infinity where no
    /// point is within the radius), the confidence 0..1 (0 off the floor)
    /// and 1 for the cells holding a measured point.
    std::vector<float> value_dbm;
    std::vector<float> confidence;
    std::vector<std::uint8_t> measured;
    /// Input fingerprints (staleness).
    std::string building_sha256;
    std::string points_sha256;
    std::string settings_sha256;

    /// "interpolated".
    std::string label() const;
    bool operator==(const Interpolation&) const = default;
};

/// The cells of `grid` whose centre lies on floor `f`: inside its trim
/// outline (rectangle or polygon, mapped to plan units), else inside the
/// plan rectangle (EXP-06 "floor area").
std::vector<bool> floor_cells(const building::Building& b, const building::Floor& f, const Grid& grid);

/// Fingerprints of the current inputs of a request.
struct InputFingerprints {
    std::string building;
    std::string points;
    std::string settings;
    bool operator==(const InputFingerprints&) const = default;
};
InputFingerprints fingerprints(const project::Project& project, const InterpolationRequest& request);

/// Runs a request on the calling thread (plus `run.threads`). `stage`, if
/// set, is told each step ("Points", "Map"). ProjectError NotFound for an
/// unknown floor; Invalid for a floor without scale, a metric that is not
/// a level, invalid settings or no point with a level in dBm. A cancelled
/// run returns nullopt. The same inputs give the same result with any
/// thread count and Isa.
std::optional<Interpolation> interpolate(const project::Project& project, const InterpolationRequest& request,
                                         const RunControl& run = {},
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

/// Staleness (IND-45): a result is stale when the floors, the measured
/// points or the settings differ from those it was made from. It stays
/// stored and usable, marked stale, until run again.
struct Staleness {
    bool stale = false;
    std::vector<std::string> reasons;  ///< e.g. "measured points changed"
};
Staleness staleness(const project::Project& project, const Interpolation& interpolation);

/// Hover readout (IND-43) at a position (plan-frame metres).
struct Readout {
    bool on_map = false;              ///< inside the grid
    std::optional<double> value_dbm;  ///< nullopt where no point is within the radius
    bool measured = false;            ///< over a cell holding a measured point: the cell's measured value
    std::string label;                ///< "measured" or "interpolated"
    double confidence = 0.0;
    /// The nearest measured point, when there is one.
    std::optional<InterpolatedPoint> nearest;
    double nearest_m = 0.0;
};
Readout readout(const Interpolation& interpolation, Point2 position_m);

/// The colour of every cell with the measured points' legend (IND-43):
/// the scale's colour of the value, alpha = confidence * 255 (0xAARRGGBB).
std::vector<std::uint32_t> cell_colours(const Interpolation& interpolation, const ColourScale& scale);

/// "Share of floor area (interpolated) (%)" (IND-43): the share of the assessed
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
/// measured points of the same selection (point_list, "Share of measured
/// points (%)") and the interpolated area.
struct CoverageSummary {
    CoverageStats measured;
    InterpolatedAreaStats interpolated;
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
        Isa isa = Isa::Simd;
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
