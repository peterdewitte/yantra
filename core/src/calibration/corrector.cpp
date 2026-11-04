// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Provenance object adapted from yantra-v2 68ea20a:
// core/src/measure/levels.cpp (LevelCalibration::provenance).

#include "yantra/core/calibration/corrector.h"

#include <nlohmann/json.hpp>

namespace yantra::calibration {

const AntennaCorrection& AntennaSettings::of(std::string_view radio_plugin_id, std::string_view device_key) const {
    if (!devices.empty()) {
        const auto it = devices.find({std::string(radio_plugin_id), std::string(device_key)});
        if (it != devices.end()) return it->second;
    }
    return all;
}

std::string AntennaSettings::validate() const {
    if (std::string p = all.validate(); !p.empty()) return p;
    for (const auto& [k, c] : devices)
        if (std::string p = c.validate(); !p.empty()) return k.first + " " + k.second + ": " + p;
    return {};
}

TableCorrector::TableCorrector(std::shared_ptr<const Table> table, AntennaSettings antenna, Options options)
    : table_(table ? std::move(table) : std::make_shared<const Table>()),
      antenna_(std::move(antenna)),
      options_(std::move(options)) {
    if (!options_.now_unix) options_.now_unix = [] { return now_unix(); };
}

TableCorrector::TableCorrector(std::shared_ptr<const Table> table, AntennaSettings antenna)
    : TableCorrector(std::move(table), std::move(antenna), Options{}) {}

measure::LevelCorrection TableCorrector::correction(const measure::LevelContext& c) const {
    measure::LevelCorrection out;
    out.correction_db = antenna_.of(c.radio_plugin_id, c.device_key).offset_db();
    if (c.radio_dbm) {
        out.source = "radio";
        return out;
    }
    if (c.calibration_key == nullptr) return out;
    const Lookup l = table_->lookup(*c.calibration_key, static_cast<double>(c.frequency_hz), options_.now_unix(),
                                    options_.max_extrapolation_hz);
    if (!l.usable() || (l.simulated && !c.simulated)) return out;
    out.calibrated = true;
    out.offset_db = l.offset_db;
    out.source = "table";
    out.key = c.calibration_key->id();
    out.calibration_date_unix = l.created_unix;
    out.expired = l.status == LookupStatus::Expired;
    out.extrapolated = l.extrapolated;
    return out;
}

std::string TableCorrector::provenance(const Key& key, std::string_view radio_plugin_id,
                                       std::string_view device_key, std::int64_t now) const {
    using json = nlohmann::json;
    json j;
    const Entry* e = table_->find(key);
    j["status"] = e == nullptr ? "uncalibrated" : e->expired(now) ? "expired" : "valid";
    j["key"] = key.id();
    j["driver"] = key.driver;
    j["serial"] = key.serial;
    j["settings"] = key.settings;
    j["date"] = e == nullptr ? json(nullptr) : json(utc_text(e->created_unix));
    j["expires"] = e == nullptr ? json(nullptr) : json(utc_text(e->expires_unix()));
    j["antenna_correction"] = json::parse(to_json(antenna_.of(radio_plugin_id, device_key)));
    j["table"] = e == nullptr ? json(nullptr) : json::parse(table_->subset(key).to_json());
    return j.dump();
}

}  // namespace yantra::calibration
