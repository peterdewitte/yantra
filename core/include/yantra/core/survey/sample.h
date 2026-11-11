// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The values of a measured point (IND-20, IND-30): one measurement cycle
// (docs/MEASUREMENT_PIPELINE.md) as the JSON kept in the project's
// `points.sample` column, and the averaging of manual points (GEO-05).
// docs/SURVEY.md "Sample values" and "Manual points". Technology-free:
// cell identity is whatever the technology schema marks as key fields.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/measure/sample.h"

namespace yantra::survey {

/// The JSON of one cycle: {"cycle", "flags", "carriers": [...]}, one
/// carrier object per sample of the cycle that has a level, a C1 or a
/// quality value (neighbour-list entries without a level are left out),
/// in cycle order. Carrier keys: dev, tech, schema, role, f (Hz), entry
/// (neighbour entries), level and unit ("dBm", or "dBFS" when
/// uncalibrated), c1 and c1_src, quality ([{field, unit, value}]), cell
/// ({key field: value text}, without the frequency), sim ({floor, x_m,
/// y_m}: the simulator's position, RAD-19), flags (text), age_ms.
nlohmann::json sample_json(const measure::MeasurementCycle& cycle);

/// The simulator's position of a cycle (RAD-19 sim_position): of its
/// serving sample, else of its newest sample (smallest age) that has one;
/// with `device`, only that device's samples. A held sample (no record in
/// this cycle, e.g. a scan channel the radio has not measured again) keeps
/// the position of its old record: the first sample's position may be
/// seconds old, so the serving sample's, whose level a point shows, is used.
struct SimPosition {
    std::string floor_id;
    double x_m = 0.0, y_m = 0.0;  ///< plan-frame metres
};
std::optional<SimPosition> sim_position(const nlohmann::json& sample, const std::string& device = {});

enum class AverageRule { Power, Arithmetic };
/// Power for "dBm", "dBFS" and "dB", Arithmetic otherwise.
AverageRule average_rule(std::string_view unit) noexcept;
/// 10 log10(mean(10^(x/10))); NaN for none.
double power_mean_db(const std::vector<double>& values) noexcept;
/// NaN for none.
double arithmetic_mean(const std::vector<double>& values) noexcept;

/// One manual point from the samples of its N seconds (GEO-05), in time
/// order. Per carrier (same dev, schema, role, f, entry), in order of
/// first appearance: level, C1 and each quality value averaged by
/// average_rule of its unit over the samples that have it ("n": how many
/// samples had the carrier), the newest cell identity and sim position,
/// the union of flags, the largest age. "cycle" is the first sample's,
/// "cycles" the number of samples. std::invalid_argument for none.
nlohmann::json average_samples(const std::vector<nlohmann::json>& samples);

}  // namespace yantra::survey
