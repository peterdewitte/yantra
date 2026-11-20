// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TDMA frame time base of the TETRA decoder (RAD-101, DEC-11) and the
// downlink logical channels (DEC-01). The time is hyperframe, multiframe,
// frame and timeslot (H-M-F-S, EN 300 392-2 clause 7.3); the decoder
// derives it from the MAC-SYNC PDU of the BSCH (timeslot, frame,
// multiframe) and the hyperframe number of MAC-SYSINFO, and counts it on
// from slot to slot in between.
#pragma once

#include <cstdint>
#include <string_view>

namespace yantra::receiver::tetra {

/// Downlink logical channels (EN 300 392-2 9.5.1). The numbers index
/// counter arrays; never renumber.
enum class LogicalChannel : uint8_t {
    Unknown = 0,
    Bsch = 1,   ///< broadcast synchronisation (SB1 of a synchronisation burst)
    Bnch = 2,   ///< broadcast network (SB2 in its frame-18 slot)
    SchF = 3,   ///< signalling, full slot
    SchHd = 4,  ///< signalling, half slot downlink
    Stch = 5,   ///< stealing channel on a traffic slot
    Aach = 6,   ///< access assignment (broadcast block of every burst)
    Tch = 7,    ///< traffic channel: never decoded (RAD-17), only counted
};
inline constexpr int kLogicalChannelCount = 8;

std::string_view channel_name(LogicalChannel c);

/// TDMA time H-M-F-S (DEC-11). Numbers as on the air: timeslot 1..4,
/// frame 1..18, multiframe 1..60, hyperframe 0..65535 once known (from
/// MAC-SYSINFO; a cell that sends a cipher key identifier instead never
/// gives it).
struct TdmaTime {
    uint16_t hyperframe = 0;
    bool hyperframe_known = false;
    uint8_t multiframe = 1;
    uint8_t frame = 1;
    uint8_t timeslot = 1;

    /// Moves to the next timeslot (and frame, multiframe, hyperframe).
    void advance();
    /// Timeslots since hyperframe 0, multiframe 1, frame 1, timeslot 1:
    /// the frame-time stamp for elapsed-time measurement between blocks
    /// (one slot = 85/6 ms, DEC-11). Without a known hyperframe it counts
    /// from hyperframe 0.
    uint64_t slot_number() const;
    /// Same multiframe, frame and timeslot (the hyperframe aside).
    bool same_slot(const TdmaTime& o) const
    {
        return multiframe == o.multiframe && frame == o.frame && timeslot == o.timeslot;
    }
    bool operator==(const TdmaTime&) const = default;
};

/// Timeslots per hyperframe (60 multiframes x 18 frames x 4 slots).
inline constexpr uint64_t kSlotsPerHyperframe = 60u * 18u * 4u;
/// Duration of one timeslot in seconds (EN 300 392-2 9.3: 85/6 ms).
inline constexpr double kTimeslotSeconds = 0.085 / 6.0;

}  // namespace yantra::receiver::tetra
