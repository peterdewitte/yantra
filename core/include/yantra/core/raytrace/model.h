// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ray-tracing propagation model per floor (IND-40) with the inter-floor
// term (IND-44). Own code, no extra library. From every antenna of the
// building model (IND-12) rays go to the centre of every cell of a grid
// over the floor (default 0.25 m): the direct ray and the specular
// reflections on the floor's walls up to a set order (default 2, image
// method). A ray loses the path loss over its unfolded 3D length (free
// space below the reference distance, log-distance with exponent n
// beyond it; n = 2 is free space throughout), the attenuation of every
// wall it crosses (IND-11, at the cell's carrier), the reflection loss of
// every wall it reflects off and the antenna pattern loss towards its
// first leg; antennas on other floors add the slabs between (IND-44,
// through the IND-09 alignment). Contributions are summed in power, per
// served cell. A prediction model on the building, not signal processing.
//
// Positions are plan-frame metres of the floor the map is for (the frame
// of the walk points, IND-20, and of the scene, RAD-36). With no
// reflections, exponent 2 and the same receiver height the levels are
// those of yantra-radios' multi-wall model (RAD-73; TST-36 tests that).
//
// For the fit (IND-41, step 8.2): trace_points() returns the geometry of
// every ray to a set of points once, and evaluate() turns it into levels
// for any EIRP per antenna, path-loss exponent, per-class wall losses,
// reflection loss and floor loss without tracing again. compute_map()
// runs on the caller's thread (and RunControl::threads workers) with
// progress and cancellation; the background job (IND-45) wraps it.
// Equations and limits: docs/PROPAGATION.md.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/raytrace/model.h,
// adapted (plan-frame metres, cells per antenna, per-cell carrier
// frequency, per-class reflection losses, free space below d0).
#pragma once

#include "yantra/core/building/model.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yantra::raytrace {

using building::Point2;

/// Highest reflection order that may be set (the image count grows as
/// wall segments ^ order).
inline constexpr int kMaxReflections = 3;

/// Level of a grid cell or point that no antenna reaches (no antennas).
inline constexpr double kNoLevel = -std::numeric_limits<double>::infinity();

/// Shortest ray length used in the free-space loss, metres (as RAD-73).
inline constexpr double kMinDistanceM = 0.01;

/// The model parameters (docs/PROPAGATION.md "Parameters"). Geometric
/// parameters (marked [trace]) change the rays; the others only the
/// levels, so a fit can vary them on a PathSet without tracing again.
struct ModelParams {
    /// Frequency for every cell, Hz; none: each cell's carrier
    /// (Cell::carrier_hz). A group without a carrier needs this.
    std::optional<double> frequency_hz;
    /// [trace] Grid cell size, metres (0.05 .. 10); IND-40 default 0.25.
    double grid_m = 0.25;
    /// [trace] Highest reflection order (0 .. kMaxReflections); default 2.
    int max_reflections = 2;
    /// [trace] Receiver height above its floor, metres (0 .. 10).
    double rx_height_m = 1.5;
    /// [trace] Antennas on other floors contribute (IND-44).
    bool other_floors = true;
    /// Path-loss exponent n beyond the reference distance (1 .. 8; 2 =
    /// free space).
    double path_loss_exponent = 2.0;
    /// Reference distance d0, metres (0.01 .. 100): free space up to d0,
    /// FSPL(d0) + 10 n log10(d / d0) beyond.
    double reference_distance_m = 1.0;
    /// Loss per specular reflection off a wall whose class has no
    /// reflection_loss_db of its own, dB (0 .. 60); 6 dB as RAD-73.
    double reflection_loss_db = 6.0;
    /// Loss per floor boundary between antenna and receiver, dB (0 .. 100);
    /// none: the building's slab values (Floor::slab_attenuation_db, else
    /// Building::floor_attenuation_db).
    std::optional<double> floor_loss_db;
    /// Loss per crossing of a wall class, dB (0 .. 100), at every frequency,
    /// instead of the class table value (fitted values, IND-41). Walls with
    /// their own attenuation keep it.
    std::map<std::string, double> wall_class_loss_db;
    /// EIRP per antenna id, dBm (-50 .. 80), instead of the antenna's own
    /// (fitted values, IND-41/42).
    std::map<std::string, double> antenna_eirp_dbm;
    /// EIRP of an antenna with neither an entry above nor its own
    /// (Antenna::effective_eirp_dbm), dBm (-50 .. 80).
    double default_eirp_dbm = 10.0;

    bool operator==(const ModelParams&) const = default;

    /// Problems, one message each ("grid_m: 0 is outside 0.05 .. 10"):
    /// empty when the parameters are usable.
    std::vector<std::string> validate() const;
};

/// Which inner loop the tracer uses for the wall-candidate test (the hot
/// path). Both give the same results bit for bit; Simd falls back to
/// Scalar when the build has no SIMD kernels (YANTRA_SIMD=OFF).
enum class Isa { Scalar, Simd };

/// True when this build has the SIMD kernels (YANTRA_SIMD=ON).
bool simd_built() noexcept;

/// How a run is executed: not part of the model, results do not depend
/// on it (any thread count gives the same values).
struct RunControl {
    Isa isa = Isa::Simd;
    /// Worker threads including the calling one; 0: hardware threads.
    unsigned threads = 0;
    /// Set to true (from any thread) to stop; the run returns early with
    /// `cancelled` set.
    const std::atomic<bool>* cancel = nullptr;
    /// Called on the calling thread with the done fraction (0 .. 1),
    /// non-decreasing, at most about 200 times per run.
    std::function<void(double)> progress;
};

/// A grid over a floor in plan-frame metres: cell (col, row) covers
/// [origin + (col, row) * cell_m, origin + (col + 1, row + 1) * cell_m).
struct Grid {
    Point2 origin_m;
    double cell_m = 0.25;
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
/// plan-frame metres; without a plan size the bounding box of its walls
/// and antennas plus 1 m. std::logic_error without scale;
/// std::invalid_argument for a cell size outside 0.05 .. 10 m, an empty
/// floor or more than kMaxGridCells cells.
Grid floor_grid(const building::Building& b, const building::Floor& f, double cell_m);

/// The antennas serving one cell: their levels are summed in power. Key
/// "cell:<Cell::id>", or "antenna:<id>" for an antenna without a cell.
struct SourceGroup {
    std::string key;
    std::string label;
    std::string cell_id;          ///< "" for an antenna without a cell
    double frequency_hz = 0.0;    ///< the frequency its levels are for
    std::vector<std::string> antenna_ids;

    bool operator==(const SourceGroup&) const = default;
};

/// A computed map of one floor (IND-40).
struct CoverageMap {
    std::string floor_id;
    Grid grid;
    ModelParams params;
    std::vector<SourceGroup> groups;
    /// Level per group and cell (grid.index order), dBm.
    std::vector<std::vector<double>> level_dbm;
    /// Highest group level per cell, dBm, and that group (-1 when none).
    std::vector<double> best_dbm;
    std::vector<int> best_group;
    /// Antennas used, antennas left out with the reason (e.g. on a floor
    /// without scale), and antennas that got default_eirp_dbm.
    std::vector<std::string> antennas_used;
    std::vector<std::string> skipped;
    std::vector<std::string> default_eirp;
    /// Rays summed over all cells (direct + reflected).
    std::size_t rays = 0;
    bool cancelled = false;
};

/// The map of floor `floor_id`. std::invalid_argument for invalid
/// parameters, an unknown floor or a group without a frequency;
/// std::logic_error when the floor has no scale. Antennas on floors
/// without scale are skipped (listed).
CoverageMap compute_map(const building::Building& b, const std::string& floor_id, const ModelParams& p,
                        const RunControl& run = {});

/// One ray to a point, independent of the non-geometric parameters.
struct RayTerm {
    int antenna = 0;              ///< index into PathSet::antennas
    double length_m = 0.0;        ///< unfolded 3D length
    int reflections = 0;          ///< bounces in all
    /// Bounces off walls whose class has no reflection loss of its own
    /// (ModelParams::reflection_loss_db each), and the summed loss of the
    /// other bounces (their classes' reflection_loss_db).
    int default_reflections = 0;
    double class_reflection_db = 0.0;
    double pattern_loss_db = 0.0; ///< >= 0, towards the first leg
    int slabs = 0;                ///< floor boundaries between
    double slab_db = 0.0;         ///< their loss from the building's slab values
    double fixed_wall_db = 0.0;   ///< walls with their own attenuation
    /// Crossings per wall class: (index into PathSet::wall_classes, count).
    std::vector<std::pair<int, int>> class_crossings;

    bool operator==(const RayTerm&) const = default;
};

/// The rays from every antenna to a set of points on one floor.
struct PathSet {
    std::string floor_id;
    std::vector<Point2> points_m;
    std::vector<std::string> antennas;  ///< antenna ids
    std::vector<int> antenna_group;     ///< index into groups, per antenna
    std::vector<SourceGroup> groups;
    std::vector<std::string> wall_classes;  ///< class ids used by the rays
    std::vector<std::vector<RayTerm>> rays; ///< per point
    std::vector<std::string> skipped;
    bool cancelled = false;
};

/// Traces the rays to `points_m` (plan-frame metres on floor `floor_id`)
/// with the [trace] parameters of `p`. Same exceptions as compute_map().
PathSet trace_points(const building::Building& b, const std::string& floor_id, const std::vector<Point2>& points_m,
                     const ModelParams& p, const RunControl& run = {});

/// Levels at the points of a PathSet for the non-geometric parameters of
/// `p` (EIRP, exponent, reference distance, reflection, floor and wall
/// losses, frequency); the [trace] parameters are ignored.
struct PointLevels {
    std::vector<std::vector<double>> level_dbm;  ///< per group, per point
    std::vector<double> best_dbm;                ///< per point
    std::vector<int> best_group;
};
PointLevels evaluate(const building::Building& b, const PathSet& s, const ModelParams& p);

/// The frequency of every group of `s` under `p` (as evaluate() uses
/// them): ModelParams::frequency_hz, else the group cell's carrier.
/// std::invalid_argument for a group without either.
std::vector<double> group_frequencies(const building::Building& b, const PathSet& s, const ModelParams& p);

/// Path loss of one ray of 3D length `d_m` at `frequency_hz` (dB): free
/// space up to d0 (lengths below kMinDistanceM held there), log-distance
/// with `exponent` beyond.
double path_loss_db(double d_m, double frequency_hz, double exponent, double d0_m);

/// Height of floor level `level` above floor `f` in metres (negative
/// below): the floor-to-floor heights in between, `f`'s height for a level
/// without a floor (as the scene export's storeys).
double level_above_m(const building::Building& b, const building::Floor& f, int level);

}  // namespace yantra::raytrace
