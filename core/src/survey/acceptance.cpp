// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stored acceptance profiles (acceptance.h; EXP-06).

#include "yantra/core/survey/acceptance.h"

#include <cmath>
#include <set>
#include <stdexcept>

namespace yantra::survey {

namespace {

using Basis = AcceptanceProfile::Basis;

bool in_range(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }

double number(const nlohmann::json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    if (it == j.end()) return fallback;
    if (!it->is_number()) throw std::invalid_argument(std::string("'") + key + "' is not a number");
    return it->get<double>();
}

}  // namespace

const char* basis_name(Basis b) noexcept {
    switch (b) {
    case Basis::Points: return "points";
    case Basis::TestedArea: return "tested_area";
    case Basis::FloorArea: return "floor_area";
    }
    return "points";
}

std::optional<Basis> basis_from_name(const std::string& n) {
    for (Basis b : {Basis::Points, Basis::TestedArea, Basis::FloorArea})
        if (n == basis_name(b)) return b;
    return std::nullopt;
}

std::vector<std::string> validate(const AcceptanceProfile& p) {
    std::vector<std::string> out;
    if (p.name.empty()) out.emplace_back("the profile has no name");
    if (p.unit.empty()) out.emplace_back("the profile has no unit");
    if (p.criteria.empty()) out.emplace_back("the profile has no criterion");
    for (const AcceptanceCriterion& c : p.criteria) {
        if (!std::isfinite(c.threshold)) out.emplace_back("a threshold is not a number");
        if (!in_range(c.min_percent, 0.0, 100.0)) out.emplace_back("a percentage is outside 0 .. 100");
    }
    if (!in_range(p.square_m, 0.1, 100.0)) out.emplace_back("the square size is outside 0.1 .. 100 m");
    if (!in_range(p.square_percentile, 0.0, 100.0)) out.emplace_back("the square percentile is outside 0 .. 100");
    if (!in_range(p.min_confidence, 0.0, 1.0)) out.emplace_back("the least confidence is outside 0 .. 1");
    return out;
}

nlohmann::json profile_json(const AcceptanceProfile& p) {
    nlohmann::json criteria = nlohmann::json::array();
    for (const AcceptanceCriterion& c : p.criteria)
        criteria.push_back({{"threshold", c.threshold}, {"min_percent", c.min_percent}});
    return {{"format", kAcceptanceFormat},
            {"version", kAcceptanceVersion},
            {"unit", p.unit},
            {"basis", basis_name(p.basis)},
            {"criteria", criteria},
            {"square_m", p.square_m},
            {"square_percentile", p.square_percentile},
            {"min_confidence", p.min_confidence}};
}

AcceptanceProfile profile_from_json(const std::string& name, const nlohmann::json& j) {
    if (!j.is_object()) throw std::invalid_argument("the definition is not a JSON object");
    if (j.value("format", std::string()) != kAcceptanceFormat)
        throw std::invalid_argument(std::string("the definition is not a ") + kAcceptanceFormat);
    const auto version = j.find("version");
    if (version == j.end() || !version->is_number_integer()) throw std::invalid_argument("the definition has no version");
    if (version->get<int>() > kAcceptanceVersion)
        throw std::invalid_argument("the profile was saved by a newer Yantra (version " +
                                    std::to_string(version->get<int>()) + ")");
    AcceptanceProfile p;
    p.name = name;
    if (const auto u = j.find("unit"); u != j.end()) {
        if (!u->is_string()) throw std::invalid_argument("'unit' is not text");
        p.unit = u->get<std::string>();
    }
    if (const auto b = j.find("basis"); b != j.end()) {
        const auto basis = b->is_string() ? basis_from_name(b->get<std::string>()) : std::nullopt;
        if (!basis) throw std::invalid_argument("unknown basis " + b->dump());
        p.basis = *basis;
    }
    const auto criteria = j.find("criteria");
    if (criteria == j.end() || !criteria->is_array()) throw std::invalid_argument("'criteria' is not a list");
    for (const nlohmann::json& c : *criteria) {
        if (!c.is_object()) throw std::invalid_argument("a criterion is not an object");
        if (!c.contains("threshold") || !c.contains("min_percent"))
            throw std::invalid_argument("a criterion needs 'threshold' and 'min_percent'");
        p.criteria.push_back(AcceptanceCriterion{number(c, "threshold", 0.0), number(c, "min_percent", 0.0)});
    }
    p.square_m = number(j, "square_m", p.square_m);
    p.square_percentile = number(j, "square_percentile", p.square_percentile);
    p.min_confidence = number(j, "min_confidence", p.min_confidence);
    if (const auto problems = validate(p); !problems.empty()) throw std::invalid_argument(problems.front());
    return p;
}

std::vector<AcceptanceProfile> acceptance_profiles(const project::Project& project) {
    std::vector<AcceptanceProfile> out;
    for (const project::StoredAcceptanceProfile& s : project.acceptance_profiles()) {
        try {
            out.push_back(profile_from_json(s.name, nlohmann::json::parse(s.definition)));
        } catch (const std::exception& e) {
            throw project::ProjectError(project::ProjectError::Code::Damaged,
                                        "acceptance profile '" + s.name + "' cannot be read: " + e.what());
        }
    }
    return out;
}

void set_acceptance_profiles(project::Project& project, const std::vector<AcceptanceProfile>& profiles) {
    std::vector<project::StoredAcceptanceProfile> rows;
    std::set<std::string> names;
    for (const AcceptanceProfile& p : profiles) {
        if (const auto problems = validate(p); !problems.empty())
            throw project::ProjectError(project::ProjectError::Code::Invalid,
                                        "acceptance profile '" + p.name + "': " + problems.front());
        if (!names.insert(p.name).second)
            throw project::ProjectError(project::ProjectError::Code::Invalid,
                                        "acceptance profile '" + p.name + "' is there twice");
        rows.push_back(project::StoredAcceptanceProfile{p.name, profile_json(p).dump()});
    }
    project.set_acceptance_profiles(rows);
}

AcceptanceProfile default_acceptance_profile() {
    AcceptanceProfile p;
    p.name = "Indoor acceptance";
    p.unit = "dBm";
    p.criteria = {AcceptanceCriterion{-95.0, 95.0}};
    p.basis = AcceptanceProfile::Basis::Points;
    return p;
}

}  // namespace yantra::survey
