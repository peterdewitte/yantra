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
// This header is the hook of step 6.2: IdentityCorrector (no calibration,
// no antenna correction) is what the pipeline uses until the calibration
// table and the antenna settings exist. Technology-neutral (PLG-01).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace yantra::measure {

// What the corrector is asked about.
struct LevelContext {
    std::string_view radio_plugin_id;  // yr_descriptor::id
    std::string_view device_key;       // yr_device_info::key
    std::uint64_t frequency_hz = 0;    // carrier of the record (core field frequency_hz)
    bool radio_dbm = false;            // the radio reports dBm (SELF_CALIBRATED), not dBFS
};

// Its answer, kept with every mapped record and sample (RF-02: stored with
// the recording).
struct LevelCorrection {
    bool calibrated = false;     // a calibration entry was applied to dBFS levels
    double offset_db = 0.0;      // dBFS -> dBm offset (RF-02), dBFS levels only
    double correction_db = 0.0;  // antenna gain / cable loss / attenuator (HW-09), every level
    std::string source;          // what was applied, e.g. calibration entry id and date; "" none
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
