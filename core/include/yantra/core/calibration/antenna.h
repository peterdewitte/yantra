// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Antenna correction (HW-09): a configurable antenna gain, cable loss and
// attenuator in dB, applied to every level reading and stored with the
// data. A receiver behind an antenna with gain G, a cable with loss C and
// an attenuator A sees (G - C - A) dB more than a 0 dBi antenna at the
// same place would deliver to the connector; every measured level gets
// offset_db() = -G + C + A to undo that, so levels are those of a 0 dBi
// antenna at the measuring point. Broadcast values in dBm (thresholds such
// as a minimum receive level) are never corrected: they are SYSTEM_INFO,
// not LEVEL readings (measure/technology_map.h).
// Copied from yantra-v2 68ea20a: core/include/yantra/core/stream/level.h
// and core/src/stream/level.cpp (AntennaCorrection), adapted for HW-09
// (JSON text instead of a JSON value; per-device configuration).
#pragma once

#include <string>

namespace yantra::calibration {

struct AntennaCorrection {
    double antenna_gain_dbi = 0.0;  // -30 .. +40 dBi
    double cable_loss_db = 0.0;     // 0 .. 60 dB
    double attenuator_db = 0.0;     // 0 .. 100 dB

    double offset_db() const noexcept { return -antenna_gain_dbi + cable_loss_db + attenuator_db; }
    bool is_zero() const noexcept {
        return antenna_gain_dbi == 0.0 && cable_loss_db == 0.0 && attenuator_db == 0.0;
    }
    // "" when valid, else what is wrong (finite values inside the ranges).
    std::string validate() const;
    bool operator==(const AntennaCorrection&) const = default;
};

// {"antenna_gain_dbi": .., "cable_loss_db": .., "attenuator_db": ..}
// (compact JSON text), the form stored with recordings and settings.
std::string to_json(const AntennaCorrection& correction);
// Strict: unknown members, wrong types and values out of range are
// refused with `error`; missing members are 0. `out` is unchanged on
// failure.
bool antenna_correction_from_json(const std::string& text, AntennaCorrection& out,
                                  std::string& error);

}  // namespace yantra::calibration
