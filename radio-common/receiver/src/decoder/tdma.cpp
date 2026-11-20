// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/types.cpp.

#include "yantra/receiver/tdma.hpp"

namespace yantra::receiver::tetra {

std::string_view channel_name(LogicalChannel c)
{
    switch (c) {
    case LogicalChannel::Bsch: return "BSCH";
    case LogicalChannel::Bnch: return "BNCH";
    case LogicalChannel::SchF: return "SCH/F";
    case LogicalChannel::SchHd: return "SCH/HD";
    case LogicalChannel::Stch: return "STCH";
    case LogicalChannel::Aach: return "AACH";
    case LogicalChannel::Tch: return "TCH";
    case LogicalChannel::Unknown: break;
    }
    return "unknown";
}

void TdmaTime::advance()
{
    if (++timeslot <= 4)
        return;
    timeslot = 1;
    if (++frame <= 18)
        return;
    frame = 1;
    if (++multiframe <= 60)
        return;
    multiframe = 1;
    ++hyperframe;  // wraps at 65536
}

uint64_t TdmaTime::slot_number() const
{
    return ((static_cast<uint64_t>(hyperframe) * 60u + (multiframe - 1u)) * 18u + (frame - 1u)) * 4u +
           (timeslot - 1u);
}

}  // namespace yantra::receiver::tetra
