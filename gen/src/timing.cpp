// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TDMA time counters (EN 300 392-2 clause 7.3) of the TETRA test-signal
// generator (RAD-70).

#include "yantra/gen/timing.hpp"

namespace yantra::gen {

void TdmaTime::advance()
{
    if (++tn <= kSlotsPerFrame)
        return;
    tn = 1;
    if (++fn <= kFramesPerMultiframe)
        return;
    fn = 1;
    if (++mn <= kMultiframesPerHyperframe)
        return;
    mn = 1;
    ++hn;  // wraps 65535 -> 0
}

bool TdmaTime::valid() const
{
    return tn >= 1 && tn <= kSlotsPerFrame && fn >= 1 && fn <= kFramesPerMultiframe && mn >= 1 &&
           mn <= kMultiframesPerHyperframe;
}

}  // namespace yantra::gen
