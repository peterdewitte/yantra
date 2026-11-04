// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The level-correction stage with the calibration table (RF-02, HW-01b)
// and the antenna correction (HW-09): a measure::LevelCorrector for the
// measurement pipeline (OA-13). Per record:
//  - SELF_CALIBRATED radio (levels in dBm, e.g. the TETRA terminal): no
//    table; source "radio"; the antenna correction is added; not flagged
//    uncalibrated (RAD-21);
//  - dBFS radio with a calibration key (from the radio host) that has a
//    usable table entry at the record's frequency: offset_db from the
//    lookup (interpolated/extrapolated as table.h says), source "table",
//    with the entry's key, date and expired/extrapolated marks;
//  - otherwise (no key, no entry, frequency out of range, a practice entry
//    for a real radio): uncalibrated, the level stays dBFS and the
//    pipeline flags it UNCALIBRATED (HW-01b);
//  - always: correction_db = the device's antenna correction offset_db()
//    (HW-09), on every LEVEL reading (dBFS or dBm), never on broadcast
//    thresholds (SYSTEM_INFO; technology_map.h).
//
// Immutable and thread-safe: a changed table or antenna setting is a new
// TableCorrector handed to MeasurementPipeline::set_corrector(). No I/O,
// no plugin calls, no blocking (PLG-07).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "yantra/core/calibration/antenna.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/measure/correction.h"

namespace yantra::calibration {

// Antenna correction per radio device (HW-09): a default for every device
// and overrides per (radio plugin id, device key).
struct AntennaSettings {
    AntennaCorrection all;
    std::map<std::pair<std::string, std::string>, AntennaCorrection> devices;

    const AntennaCorrection& of(std::string_view radio_plugin_id, std::string_view device_key) const;
    // "" when every value is valid, else what is wrong.
    std::string validate() const;
};

class TableCorrector final : public measure::LevelCorrector {
public:
    struct Options {
        double max_extrapolation_hz = Table::kDefaultMaxExtrapolationHz;
        // Seconds since 1970 for the validity check; nullptr = system clock.
        std::function<std::int64_t()> now_unix;
    };

    TableCorrector(std::shared_ptr<const Table> table, AntennaSettings antenna, Options options);
    TableCorrector(std::shared_ptr<const Table> table, AntennaSettings antenna = {});

    measure::LevelCorrection correction(const measure::LevelContext& context) const override;

    const Table& table() const noexcept { return *table_; }
    const AntennaSettings& antenna() const noexcept { return antenna_; }

    // What goes with a recording of this device (RF-02, HW-09, REC-03
    // header): compact JSON {"status", "key", "driver", "serial",
    // "settings", "date", "expires", "antenna_correction", "table"} where
    // "table" is a calibration document with only the entry of `key`
    // (null when none). docs/CALIBRATION.md "Stored with the data".
    std::string provenance(const Key& key, std::string_view radio_plugin_id, std::string_view device_key,
                           std::int64_t now_unix) const;

private:
    std::shared_ptr<const Table> table_;
    AntennaSettings antenna_;
    Options options_;
};

}  // namespace yantra::calibration
