// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/measure/sample.h"

namespace yantra::measure {

std::string sample_flags_text(std::uint32_t flags) {
    static const std::pair<std::uint32_t, const char*> names[] = {
        {sample_flag::kNoSync, "no_sync"},
        {sample_flag::kOverload, "overload"},
        {sample_flag::kUncalibrated, "uncalibrated"},
        {sample_flag::kSimulated, "simulated"},
        {sample_flag::kHeld, "held"},
        {sample_flag::kStale, "stale"},
        {sample_flag::kLate, "late"},
        {sample_flag::kDeviceLost, "device_lost"},
        {sample_flag::kDataLoss, "data_loss"},
    };
    std::string out;
    for (const auto& [bit, name] : names) {
        if ((flags & bit) == 0) continue;
        if (!out.empty()) out += ',';
        out += name;
    }
    return out;
}

const char* role_text(CarrierRole role) noexcept {
    switch (role) {
    case CarrierRole::Serving: return "serving";
    case CarrierRole::Neighbour: return "neighbour";
    case CarrierRole::Other: return "other";
    case CarrierRole::ScanChannel: return "scan";
    case CarrierRole::NeighbourEntry: return "neighbour_entry";
    }
    return "other";
}

const CarrierSample* MeasurementCycle::serving(const std::string& device_key) const noexcept {
    for (const CarrierSample& s : samples) {
        if (s.latest.role != CarrierRole::Serving) continue;
        if (device_key.empty() || s.latest.device_key == device_key) return &s;
    }
    return nullptr;
}

std::vector<const CarrierSample*> MeasurementCycle::with_role(CarrierRole role) const {
    std::vector<const CarrierSample*> out;
    for (const CarrierSample& s : samples)
        if (s.latest.role == role) out.push_back(&s);
    return out;
}

}  // namespace yantra::measure
