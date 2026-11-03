// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Indoor multi-wall model (RAD-73) on the scene of one floor (RAD-36,
// common/include/yantra/radios/scene.hpp): the received level of every
// cell at a receiver position, as the simulator plugin (RAD-61) and the
// scenario engine use it. Per antenna a of the cell:
//
//   L_a = EIRP_a + G_a(bearing) - FSPL(d_a, f) - sum(walls crossed)
//         - |floor_offset_a| * slab - S_a(x, y)
//
// with d_a the 3D distance (storeys of storey_height_m apart for antennas
// on other floors), f the cell's carrier, G_a the pattern gain
// towards the receiver, the walls the straight 2D path crosses on this
// floor, slab the floor's slab attenuation, S_a an optional seeded
// spatially correlated shadowing; optional specular reflections (image
// method, up to 2 bounces) add paths with the reflection loss of each
// wall and the walls crossed on every leg. The cell's level is the power
// sum over its antennas and paths. Formula, rules and defaults:
// docs/CHANNEL_MODEL.md. Deterministic for a given seed; independent of
// Yantra's ray tracer (Yantra IND-40), which is tested against it (TST-36).
// Wall-crossing rules copied from yantra 68ea20a
// (core/src/building/geometry.cpp), adapted to the scene format.
#pragma once

#include "channel.hpp"

#include <yantra/radios/scene.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yantra::gen {

/// A point in the floor's plan frame (metres, x right, y down).
struct PlanPoint {
    double x_m = 0.0;
    double y_m = 0.0;
};

/// Bearing from a to b in degrees clockwise from the plan's -y axis
/// ("up"), in [0, 360).
double bearing_deg(PlanPoint a, PlanPoint b);

/// Gain of a scene antenna pattern towards `bearing` (degrees), dB
/// relative to the main direction: omni 0; sector
/// -min(12 (off / beamwidth)^2, front_to_back) with `off` the angle off the
/// azimuth (3 dB down at half the beamwidth); table: linear interpolation
/// between the entries (wrapping around), clockwise from the azimuth.
double pattern_gain_db(const radios::ScenePattern& p, double bearing);

/// Where segment p0-p1 meets segment q0-q1: the parameter t along p
/// (0..1) of a proper or touching intersection; nullopt when they do not
/// meet or are parallel (a path running along a wall does not cross it).
std::optional<double> segment_intersection(PlanPoint p0, PlanPoint p1, PlanPoint q0, PlanPoint q1);

/// One crossing of a path with a wall.
struct WallCrossing {
    size_t wall = 0;  ///< index into Scene::walls
    double t = 0.0;   ///< 0 (start) .. 1 (end)
    double attenuation_db = 0.0;
};

struct WallCrossings {
    std::vector<WallCrossing> crossings;  ///< ordered by t
    double total_db = 0.0;
};

/// The walls the straight path a-b crosses and their total attenuation
/// (SceneWall::attenuation_db). Rules: a crossing at a path end point (t
/// within 1e-9 of 0 or 1) does not count (a point on a wall is on neither
/// side); where the path passes exactly through the joint of two
/// collinear walls (one wall drawn in pieces) it counts once, through the
/// corner of two walls at an angle both count; the walls
/// in `skip` (indexes) are left out (the reflecting walls of a leg).
WallCrossings walls_crossed(const radios::Scene& scene, PlanPoint a, PlanPoint b,
                            const std::vector<size_t>& skip = {});

/// Parameters of the model; the scene's own values win where it has them
/// (floor slab_attenuation_db and storey_height_m, wall class
/// reflection_loss_db).
struct MultiWallParams {
    double receiver_height_m = 1.5;     ///< above the floor
    double slab_attenuation_db = 15.0;  ///< per floor slab (no scene value)
    double storey_height_m = 3.0;       ///< no scene value
    /// Specular reflections per path: 0 (off), 1 or 2.
    int reflections = 0;
    double reflection_loss_db = 6.0;    ///< wall classes without a value
    /// Spatially correlated shadowing per antenna (0: off), seeded from
    /// `seed` and the antenna id.
    double shadowing_sigma_db = 0.0;
    double shadowing_decorrelation_m = 4.0;
    uint64_t seed = 1;
};

/// One propagation path from an antenna to the receiver.
struct PathContribution {
    int bounces = 0;               ///< 0 direct, 1 or 2 reflections
    std::vector<size_t> reflected; ///< walls reflected off, in order
    double length_m = 0.0;         ///< 3D length
    double fspl_db = 0.0;
    double wall_loss_db = 0.0;
    int walls = 0;
    double reflection_loss_db = 0.0;
    double pattern_db = 0.0;       ///< pattern gain towards the first point
    double level_dbm = 0.0;        ///< without shadowing
};

/// What one antenna gives at the receiver.
struct AntennaContribution {
    std::string antenna;
    std::string cell;
    double distance_m = 0.0;       ///< 3D, direct
    double floor_loss_db = 0.0;
    double shadowing_db = 0.0;     ///< subtracted from every path
    std::vector<PathContribution> paths;  ///< the direct path first
    /// Power sum of the paths minus the floor loss and shadowing, dBm.
    double level_dbm = 0.0;
};

/// The received level of one cell.
struct CellLevel {
    std::string cell;
    uint64_t carrier_hz = 0;
    /// Power sum over the antennas, dBm; -inf without antennas.
    double level_dbm = 0.0;
    std::vector<AntennaContribution> antennas;
    /// Index of the strongest antenna in `antennas` (0 without).
    size_t best = 0;
};

/// Not thread-safe: one model per thread (it caches shadowing fields).
class MultiWallModel {
public:
    /// std::invalid_argument for out-of-range parameters.
    explicit MultiWallModel(MultiWallParams params = {});

    const MultiWallParams& params() const { return params_; }

    /// What antenna `a` of `scene` gives at `rx`, at the carrier frequency
    /// `frequency_hz`.
    AntennaContribution antenna(const radios::Scene& scene, const radios::SceneAntenna& a, PlanPoint rx,
                                double frequency_hz) const;
    /// The level of cell `cell_id` (its antennas in `scene`, at the cell's
    /// carrier_hz) at `rx`; level -inf when the cell has no antenna, and
    /// std::invalid_argument when the scene has no such cell.
    CellLevel cell(const radios::Scene& scene, const std::string& cell_id, PlanPoint rx) const;
    /// Every cell of the scene, in scene order.
    std::vector<CellLevel> cells(const radios::Scene& scene, PlanPoint rx) const;

private:
    double shadowing(const std::string& antenna, PlanPoint rx) const;
    MultiWallParams params_;
    // Shadowing fields per antenna id, built on first use (const access is
    // single-threaded per model).
    mutable std::map<std::string, ShadowingField> fields_;
};

}  // namespace yantra::gen
