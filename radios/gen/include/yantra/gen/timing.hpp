// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TDMA time of the TETRA test-signal generator (RAD-70), EN 300 392-2
// clause 7.3: 4 timeslots per TDMA frame, 18 frames per multiframe, 60
// multiframes per hyperframe; the hyperframe number counts 0..65535.

#pragma once

#include <cstdint>

namespace yantra::gen {

inline constexpr int kSlotsPerFrame = 4;
inline constexpr int kFramesPerMultiframe = 18;
inline constexpr int kMultiframesPerHyperframe = 60;
/// Symbol rate (symbols per second) and the timeslot length in seconds
/// (255 symbols, 14.1666... ms).
inline constexpr double kSymbolRate = 18000.0;
inline constexpr double kSlotSeconds = 255.0 / kSymbolRate;

/// The timeslot a burst is sent in. tn 1..4, fn 1..18, mn 1..60,
/// hn 0..65535 (wraps).
struct TdmaTime {
    uint8_t tn = 1;
    uint8_t fn = 1;
    uint8_t mn = 1;
    uint16_t hn = 0;

    /// Moves to the next timeslot.
    void advance();
    /// True when every counter is in range.
    bool valid() const;
    bool operator==(const TdmaTime&) const = default;
};

}  // namespace yantra::gen
