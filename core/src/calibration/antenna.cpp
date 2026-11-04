// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/stream/level.cpp
// (AntennaCorrection), adapted for HW-09 (see antenna.h).

#include "yantra/core/calibration/antenna.h"

#include <cmath>
#include <sstream>

#include <nlohmann/json.hpp>

namespace yantra::calibration {

namespace {

struct Range {
    const char* name;
    double AntennaCorrection::*member;
    double lo;
    double hi;
};

constexpr Range kRanges[] = {
    {"antenna_gain_dbi", &AntennaCorrection::antenna_gain_dbi, -30.0, 40.0},
    {"cable_loss_db", &AntennaCorrection::cable_loss_db, 0.0, 60.0},
    {"attenuator_db", &AntennaCorrection::attenuator_db, 0.0, 100.0},
};

}  // namespace

std::string AntennaCorrection::validate() const {
    for (const auto& r : kRanges) {
        const double v = this->*r.member;
        if (!std::isfinite(v) || v < r.lo || v > r.hi) {
            std::ostringstream os;
            os << r.name << " must be between " << r.lo << " and " << r.hi;
            return os.str();
        }
    }
    return {};
}

std::string to_json(const AntennaCorrection& c) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& r : kRanges) j[r.name] = c.*r.member;
    return j.dump();
}

bool antenna_correction_from_json(const std::string& text, AntennaCorrection& out, std::string& error) {
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        if (!j.is_object()) {
            error = "the antenna correction is not an object";
            return false;
        }
        AntennaCorrection c;
        for (const auto& [k, v] : j.items()) {
            const Range* r = nullptr;
            for (const auto& x : kRanges)
                if (k == x.name) r = &x;
            if (r == nullptr) {
                error = "unknown antenna correction member '" + k + "'";
                return false;
            }
            if (!v.is_number()) {
                error = "'" + k + "' is not a number";
                return false;
            }
            c.*(r->member) = v.get<double>();
        }
        if (std::string problem = c.validate(); !problem.empty()) {
            error = std::move(problem);
            return false;
        }
        out = c;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace yantra::calibration
