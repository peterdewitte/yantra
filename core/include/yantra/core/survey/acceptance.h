// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stored acceptance profiles (EXP-06; docs/SURVEY.md "Acceptance
// profiles"): the user-defined criteria "X % of the points / of the area
// >= Y" an operator's indoor-coverage acceptance asks for (e.g. a DAS
// acceptance), kept in the project (schema 7, `acceptance_profiles`), so
// the report and every computer that opens the project evaluate the same
// profiles. The evaluation is stats.h evaluate() / evaluate_floors()
// (points, tested area) and interpolation.h evaluate_area() (floor area,
// interpolated).
//
// Definition (JSON object, the name is the row's):
//   {"format": "yantra-acceptance-profile", "version": 1,
//    "unit": "dBm", "basis": "points" | "tested_area" | "floor_area",
//    "criteria": [{"threshold": -95, "min_percent": 95}, ...],
//    "square_m": 2, "square_percentile": 50, "min_confidence": 0.5}
// Unknown members are ignored; a newer version is refused.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/project/project.h"
#include "yantra/core/survey/stats.h"

namespace yantra::survey {

inline constexpr const char* kAcceptanceFormat = "yantra-acceptance-profile";
inline constexpr int kAcceptanceVersion = 1;

/// "points", "tested_area", "floor_area".
const char* basis_name(AcceptanceProfile::Basis basis) noexcept;
std::optional<AcceptanceProfile::Basis> basis_from_name(const std::string& name);

/// Problems of a profile, one message each (empty: usable): a name, a
/// unit, at least one criterion, finite thresholds, percentages 0..100,
/// square 0.1..100 m, square percentile 0..100, confidence 0..1.
std::vector<std::string> validate(const AcceptanceProfile& profile);

/// The definition without the name.
nlohmann::json profile_json(const AcceptanceProfile& profile);
/// std::invalid_argument for a wrong format, a newer version, a missing
/// or wrong member, or a profile validate() refuses.
AcceptanceProfile profile_from_json(const std::string& name, const nlohmann::json& definition);

/// The project's profiles in order. ProjectError Damaged for a stored
/// profile that cannot be read.
std::vector<AcceptanceProfile> acceptance_profiles(const project::Project& project);
/// Replaces the project's profiles. ProjectError Invalid for a profile
/// validate() refuses or a repeated name.
void set_acceptance_profiles(project::Project& project, const std::vector<AcceptanceProfile>& profiles);

/// The profile used where none is stored (the post-processing form's and
/// the report's default): "Indoor acceptance", 95 % of the points >= -95 dBm.
AcceptanceProfile default_acceptance_profile();

}  // namespace yantra::survey
