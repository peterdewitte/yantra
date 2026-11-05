// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shared point list of a floor (IND-30, TST-34): the ONE function the
// project window and the coverage report (EXP-05) both call for the
// measured points of a floor, so the report shows every point of the
// window at the same position, with the same value and colour, and states
// the same count. docs/SURVEY.md "Shared point list". Partly modelled on
// yantra-v2 68ea20a core/include/yantra/core/survey/points.h.
//
// Contract:
// - A point is a stored point of a session of the floor (project schema
//   4, `points`): walk samples placed on a segment, manual points,
//   simulated points. Cycles without a plan position were never stored.
// - Every point is returned, in a fixed order (sessions by id, points by
//   sequence): no decimation, thinning, binning or re-sampling. A point
//   without a value for the metric is still a point (value nullopt, the
//   scale's no-value colour).
// - Values come from the stored sample JSON through metric_value(), the
//   colour from the list's ColourScale; callers never recompute them.
// - Binning (PP-05) is an extra, labelled view built from this list,
//   never a replacement.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/building/model.h"
#include "yantra/core/project/project.h"

namespace yantra::survey {

using building::Point2;

/// A cell as the points name it: the carrier frequency and the cell
/// identity text (the key fields without the frequency, as
/// SurveyPoint::cell); an empty `cell` is any cell on that frequency.
struct CellRef {
    std::uint64_t frequency_hz = 0;
    std::string cell;
    bool operator==(const CellRef&) const = default;
};

/// Which value of a point is shown (PP-02 views).
struct Metric {
    enum class Carrier {
        Serving,    ///< the serving carrier (RF-01)
        Strongest,  ///< the carrier with the highest level in the point
        Frequency,  ///< the carrier on `frequency_hz` (serving role first, else the strongest)
    };
    enum class Value {
        Level,    ///< dBm (dBFS when uncalibrated)
        C1,       ///< dB (RF-06)
        Quality,  ///< the quality value `quality_field` (e.g. "mer_db", "ber_pct")
    };
    Carrier carrier = Carrier::Serving;
    Value value = Value::Level;
    std::uint64_t frequency_hz = 0;
    std::string quality_field;
    std::string device;  ///< only this device's carriers; "" = any
    /// Base-station fail simulation (PP-03): the carriers of these cells
    /// are left out of every point, as if the sites were switched off. The
    /// serving carrier is then the best remaining server: the stored
    /// serving carrier when it is not failed, else the strongest remaining
    /// measured carrier. docs/SURVEY.md "Post-processing".
    std::vector<CellRef> failed_cells;

    /// "Level, serving carrier", "C1, 390.2000 MHz", "mer_db, strongest carrier".
    std::string label() const;
    bool operator==(const Metric&) const = default;
};

/// A value of a point for a metric.
struct MetricValue {
    double value = 0.0;
    std::string unit;
    std::uint64_t frequency_hz = 0;
    std::string cell;  ///< cell identity text of that carrier ("colour_code=1 la=100 ...")
};
/// nullopt when the sample has no value for the metric.
std::optional<MetricValue> metric_value(const nlohmann::json& sample, const Metric& metric);
/// The serving carrier's cell identity text ("" none): serving-cell
/// changes. With failed cells (PP-03), the best remaining server's.
std::string serving_cell(const nlohmann::json& sample, const std::string& device = {},
                         const std::vector<CellRef>& failed_cells = {});
/// The cell identity text of a sample carrier object ("" without one).
std::string carrier_cell_text(const nlohmann::json& carrier);

/// Colours by value: the band with the highest `from` <= value
/// (`lower_is_better`: the band with the lowest `from` >= value).
struct ColourBand {
    double from = 0.0;
    std::uint32_t rgb = 0;  ///< 0xRRGGBB
    std::string label;
    bool operator==(const ColourBand&) const = default;
};
struct ColourScale {
    std::string unit;
    std::vector<ColourBand> bands;  ///< ascending `from`
    bool lower_is_better = false;   ///< error rates (%)
    std::uint32_t outside_rgb = 0x7f0000;   ///< worse than every band
    std::uint32_t no_value_rgb = 0x9e9e9e;  ///< a point without a value
    std::uint32_t colour_of(std::optional<double> value) const;
    bool operator==(const ColourScale&) const = default;
};
/// Default scales: dBm -100/-90/-80/-70, dBFS -70/-60/-50/-40, dB
/// 0/5/10/20, % (lower is better) 1/2/5/10; other units one band.
ColourScale default_colour_scale(const std::string& unit);

struct SurveyPoint {
    std::int64_t session_id = 0;
    std::int64_t sequence = 0;
    std::int64_t time_ns = 0;      ///< pipeline clock
    std::int64_t utc_ns = 0;
    Point2 plan;                   ///< plan units, as stored
    std::optional<Point2> metres;  ///< plan-frame metres; nullopt when not to scale
    std::string kind;              ///< walk, manual, simulated
    std::uint32_t group = 0;       ///< walk segment / manual point number
    std::string label;
    std::optional<double> value;   ///< nullopt: no value for the metric
    std::string unit;
    std::uint64_t frequency_hz = 0;
    std::string cell;              ///< of the metric's carrier
    std::string serving_cell;      ///< of the serving carrier
    std::uint32_t rgb = 0;         ///< from the list's scale
    std::uint64_t serving_hz = 0;  ///< the serving carrier's frequency (0 none)
    bool operator==(const SurveyPoint&) const = default;
};

struct PointQuery {
    std::string floor_id;
    std::vector<std::int64_t> sessions;  ///< of the floor; empty = all of them
    Metric metric;
    std::optional<ColourScale> scale;    ///< nullopt: default_colour_scale(unit)
};

struct PointList {
    std::string floor_id;
    Metric metric;
    std::vector<std::int64_t> sessions;  ///< the sessions the points come from, by id
    std::string unit;                    ///< unit of the first value ("" none)
    bool mixed_units = false;            ///< values in other units exist (e.g. dBFS next to dBm)
    bool to_scale = false;               ///< every point has metres
    ColourScale scale;
    std::vector<SurveyPoint> points;
    std::size_t count() const noexcept { return points.size(); }
    bool operator==(const PointList&) const = default;
};

/// The points of a floor (contract above). ProjectError NotFound for an
/// unknown floor or session, Invalid for a session of another floor.
PointList point_list(const project::Project& project, const PointQuery& query);

/// Points per floor (all sessions), in floor order: the counts the
/// window's floor tabs and the report's summary state.
std::vector<std::pair<std::string, std::size_t>> point_counts(const project::Project& project);

}  // namespace yantra::survey
