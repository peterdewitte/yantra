// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The calibration key (HW-01b): a level calibration (RF-02) belongs to one
// radio configuration, because absolute levels are not comparable between
// radios, modules or gain settings. The key is the device's driver and
// serial (yr_device_info, RAD-30) and the current value of every setting
// the radio plugin marks CALIBRATION (RAD-40: antenna, gains, amplifier).
// Another gain or antenna is another key; any part that differs means "no
// entry" (uncalibrated). The key is radio-neutral (PLG-01): the plugin
// says which settings matter, the core only compares texts.
//
// The radio host (radio/radio_host.h) keeps the key of every open device
// current and hands it on with each record (measure::RadioRecord); the
// level corrector looks it up in the calibration table (calibration/
// table.h). Kept free of JSON so measure/ headers can include it.
#pragma once

#include <map>
#include <string>

#include "yantra/core/plugin/record.h"

namespace yantra::calibration {

struct Key {
    std::string driver;                           // yr_device_info::driver
    std::string serial;                           // yr_device_info::serial ("" if none)
    std::map<std::string, std::string> settings;  // CALIBRATION setting key -> value text

    // Canonical text "driver|serial|k=v;k=v" (settings sorted by key).
    std::string id() const;
    // The settings part only, e.g. "gain_amp=0;gain_lna=24".
    std::string settings_text() const;
    bool empty() const noexcept { return driver.empty() && serial.empty() && settings.empty(); }
    bool operator==(const Key&) const = default;
};

// Value text of a setting in a key: booleans "true"/"false", integers in
// decimal, doubles in the shortest text that reads back to the same
// number (24.0 -> "24", 0.5 -> "0.5"), strings as they are, enums by
// their stored item value in decimal. "" for an absent value.
std::string setting_text(const plugin::Value& value);

}  // namespace yantra::calibration
