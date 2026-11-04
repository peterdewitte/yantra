// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Level-correction stage of the measurement pipeline (OA-13): the place
// where the calibration table (RF-02, keyed per HW-01b) and the antenna
// correction (HW-09) are applied to every level record. The pipeline asks
// its LevelCorrector once per record, for the record's device and carrier
// frequency, and adds the answer to the record's LEVEL fields
// (technology_map.h Calibration):
//   level_dBm = level_dBFS + offset_db + correction_db   (dBFS radios)
//   level_dBm = level_dBm  + correction_db               (SELF_CALIBRATED radios)
// Without a calibration (calibrated == false) a dBFS level keeps its dBFS
// value (plus correction_db) and is flagged UNCALIBRATED (HW-01b).
//
// The calibration table's corrector is calibration::TableCorrector
// (calibration/corrector.h); IdentityCorrector (no calibration, no antenna
// correction) is the default. The calibration key (HW-01b: driver, serial,
// CALIBRATION settings) is resolved by the radio host, which knows the
// device and its settings, and comes with each record (RadioRecord::
// calibration_key): the corrector itself never calls a plugin, so it can
// answer on the delivery threads without blocking. Technology-neutral
// (PLG-01).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "yantra/core/calibration/key.h"

namespace yantra::measure {

// What the corrector is asked about.
struct LevelContext {
    std::string_view radio_plugin_id;  // yr_descriptor::id
    std::string_view device_key;       // yr_device_info::key
    std::uint64_t frequency_hz = 0;    // carrier of the record (core field frequency_hz)
    bool radio_dbm = false;            // the radio reports dBm (SELF_CALIBRATED), not dBFS
    bool simulated = false;            // the radio is SIMULATED (practice entries apply only there)
    // The device's calibration key at the record's time (HW-01b), from the
    // radio host; nullptr when unknown (e.g. a record file without one).
    const calibration::Key* calibration_key = nullptr;
};

// Its answer, kept with every mapped record and sample (RF-02: stored with
// the recording).
struct LevelCorrection {
    bool calibrated = false;     // a calibration entry was applied to dBFS levels
    double offset_db = 0.0;      // dBFS -> dBm offset (RF-02), dBFS levels only
    double correction_db = 0.0;  // antenna gain / cable loss / attenuator (HW-09), every level
    // Where the level scale came from: "table" (a calibration entry),
    // "radio" (SELF_CALIBRATED, dBm from the radio), "" (uncalibrated dBFS).
    std::string source;
    std::string key;                       // calibration key id used (Key::id()), "" none
    std::int64_t calibration_date_unix = 0;  // the entry's date (RF-02), 0 none
    bool expired = false;       // the entry is past its validity (value still used)
    bool extrapolated = false;  // the frequency is outside the entry's points (nearest used)
    bool operator==(const LevelCorrection&) const = default;
};

// Called on the radios' delivery threads, several at once: thread-safe,
// never blocks, no I/O (PLG-07).
class LevelCorrector {
public:
    virtual ~LevelCorrector() = default;
    virtual LevelCorrection correction(const LevelContext& context) const = 0;
};

// No calibration, no antenna correction: dBFS levels stay dBFS and are
// flagged UNCALIBRATED; dBm levels pass unchanged.
class IdentityCorrector final : public LevelCorrector {
public:
    LevelCorrection correction(const LevelContext& context) const override {
        (void)context;
        return {};
    }
};

}  // namespace yantra::measure
