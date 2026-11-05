// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fit of the ray-tracing model to measured points (IND-41) and estimation
// of source positions when no antenna is placed (IND-42). Own code, no
// extra library: a bounded Levenberg-Marquardt least-squares solver on the
// rays traced once (trace_points), with the analytic derivatives of the
// power-summed level in dB. Deterministic: the same inputs give the same
// result (fixed start, fixed order, no random numbers). Algorithm, bounds
// and stop criteria: docs/PROPAGATION.md "Fit to the measured points".
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/raytrace/fit.h,
// adapted (per-group carriers, free space below d0, per-class reflection
// losses, the dB per floor of IND-44 as a fitted parameter, plan-frame
// metres, estimated sources serving one estimated cell).
#pragma once

#include "yantra/core/raytrace/model.h"

#include <atomic>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace yantra::raytrace {

/// A closed interval [lo, hi] a fitted parameter stays in.
struct Bounds {
    double lo = 0.0;
    double hi = 0.0;
    bool operator==(const Bounds&) const = default;
};

/// Which EIRPs are fitted.
enum class EirpFit {
    Unknown,  ///< antennas without a known EIRP (Antenna::effective_eirp_dbm) or a value in the start parameters
    All,      ///< every antenna that reaches a measured point
    None,
};

/// What is fitted and within which bounds (docs/PROPAGATION.md).
struct FitOptions {
    /// The modelled level a measured value is compared with: a group key
    /// (SourceGroup::key, e.g. "cell:<id>" of the metric's carrier) or,
    /// when nullopt, the strongest group at the point (the "strongest
    /// carrier" metric).
    std::optional<std::string> group_key;
    EirpFit eirp = EirpFit::Unknown;
    Bounds eirp_dbm{-30.0, 60.0};
    bool fit_exponent = true;
    Bounds exponent{1.5, 6.0};
    bool fit_wall_classes = true;
    /// Bounds per wall class id; a class without an entry gets
    /// [0, min(100, 2 x its start value + 10)] dB.
    std::map<std::string, Bounds> wall_class_bounds;
    /// The dB per floor of the inter-floor term (IND-44), when a measured
    /// point depends on an antenna of another floor.
    bool fit_floor_loss = true;
    Bounds floor_loss_db{0.0, 60.0};
    /// Iteration limit of the solver (1 .. 10000).
    int max_iterations = 200;

    bool operator==(const FitOptions&) const = default;
    /// Problems, one message each; empty when usable.
    std::vector<std::string> validate() const;
};

/// One fitted parameter.
struct FitParameter {
    enum class Kind { Eirp, Exponent, WallClass, FloorLoss };
    Kind kind = Kind::Eirp;
    /// Antenna id, "path_loss_exponent", the wall class id or "floor_loss_db".
    std::string id;
    double start = 0.0;
    double value = 0.0;
    Bounds bounds;
    bool at_bound = false;  ///< ended on lo or hi
    bool operator==(const FitParameter&) const = default;
};

/// Agreement between model and measurement (IND-41: shown and reported).
struct FitQuality {
    std::size_t points = 0;     ///< measured points used
    double rms_db = 0.0;        ///< root mean square of (model - measured)
    double mean_db = 0.0;       ///< mean of (model - measured)
    double max_abs_db = 0.0;
    double start_rms_db = 0.0;  ///< rms with the start parameters
    bool operator==(const FitQuality&) const = default;
};

struct FitResult {
    /// The start parameters with the fitted values set (antenna_eirp_dbm,
    /// path_loss_exponent, wall_class_loss_db, floor_loss_db).
    ModelParams params;
    std::vector<FitParameter> parameters;
    /// Parameters left out, one line each with the reason (e.g. "wall
    /// class 'glass': no measured point behind such a wall").
    std::vector<std::string> not_fitted;
    FitQuality quality;
    int iterations = 0;
    std::string stop_reason;  ///< e.g. "converged: step below 1e-9"
    /// Per point of the PathSet: the fitted model level (kNoLevel when no
    /// ray reaches it) and whether the point was used (a finite measured
    /// value and a modelled level).
    std::vector<double> model_dbm;
    std::vector<bool> used;
    bool cancelled = false;
};

/// Fits the parameters chosen in `options` so that the modelled levels at
/// the points of `rays` match `measured_dbm` (parallel to rays.points_m;
/// a non-finite value leaves the point out) in the least-squares sense.
/// `start` gives the frequency, the fixed parameters and the start values
/// (clamped into the bounds). std::invalid_argument for invalid options or
/// parameters, sizes that do not match, or an unknown group key. With no
/// usable point nothing is fitted (quality.points 0, stop_reason says why).
FitResult fit_model(const building::Building& b, const PathSet& rays, const std::vector<double>& measured_dbm,
                    const ModelParams& start, const FitOptions& options = {},
                    const std::atomic<bool>* cancel = nullptr);

/// The id of the cell estimated sources serve together (IND-42).
inline constexpr const char* kEstimatedCellId = "estimated";

/// Options of the source search (IND-42).
struct SourceSearchOptions {
    int max_sources = 3;  ///< 1 .. 8
    /// A further source is kept only when it lowers the rms by at least
    /// this much, dB.
    double min_improvement_db = 1.0;
    /// Candidate spacing of the coarse search, metres; 0: the larger floor
    /// side / 32, at least 0.5 m.
    double step_m = 0.0;
    double height_m = 2.7;  ///< height of an estimated source above the floor
    Bounds eirp_dbm{-30.0, 60.0};
    /// The sources serve one cell and their levels add up in power (a DAS,
    /// the usual indoor case); false: each its own source, the strongest
    /// counts (the "strongest carrier" metric of several cells).
    bool one_cell = true;

    bool operator==(const SourceSearchOptions&) const = default;
    std::vector<std::string> validate() const;
};

struct SourceEstimate {
    /// The estimated sources as antennas of the floor (plan units, ids
    /// "estimated-1", ..., fitted EIRP, gain 0 dBi, omni); with one_cell
    /// they serve `cell`, else no cell.
    std::vector<building::Antenna> antennas;
    /// The cell of one_cell sources (id kEstimatedCellId, carrier = the
    /// search frequency); add it with add_estimated_sources().
    std::optional<building::Cell> cell;
    /// rms after each added source, dB.
    std::vector<double> rms_db;
    bool cancelled = false;
};

/// Estimates source positions from the measured levels at `points_m`
/// (plan-frame metres on `floor_id`) for a floor without placed antennas
/// (IND-42): a deterministic grid search over the floor plus four finer
/// rounds around the best candidate (spacing halved each round), one
/// source at a time, the kept sources searched again after each new one,
/// with the EIRPs fitted for every candidate (direct rays only; the
/// exponent and wall losses of `p`). Antennas already in the building are
/// ignored. `p.frequency_hz` must be set (the measured carrier).
/// `run.progress` gets 0..1 on the calling thread. Same exceptions as
/// trace_points; std::invalid_argument for invalid options, sizes or no
/// frequency.
SourceEstimate estimate_sources(const building::Building& b, const std::string& floor_id,
                                const std::vector<Point2>& points_m, const std::vector<double>& measured_dbm,
                                const ModelParams& p, const SourceSearchOptions& options = {},
                                const RunControl& run = {});

/// Puts the estimated sources (and their cell, replacing a cell with that
/// id) on floor `floor_id` of `b`. std::invalid_argument for an unknown
/// floor or an antenna id already used.
void add_estimated_sources(building::Building& b, const std::string& floor_id, const SourceEstimate& estimate);

}  // namespace yantra::raytrace
