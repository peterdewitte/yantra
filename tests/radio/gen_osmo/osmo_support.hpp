// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared pieces of the osmo-tetra receive-side tests (RAD-93, RAD-101):
// the blocks osmo-tetra's PHY delivers through its TP-SAP entry point
// (tp_sap_udata_ind, defined in test_osmo_bursts.cpp) and osmo-tetra's
// receive chain for one block (descramble, deinterleave, depuncture,
// Viterbi, CRC).
#pragma once

#include "yantra/gen/bits.hpp"
#include "yantra/gen/coding.hpp"

#include <cstdint>
#include <vector>

namespace yantra::gen::osmo_test {

struct CapturedBlock {
    int type;  // enum tp_sap_data_type
    int blk_num;
    Bits bits;
};

/// Every block delivered since the last clear().
std::vector<CapturedBlock>& captured_blocks();

/// osmo-tetra's receive chain of tetra_lower_mac.c for one block: the
/// type-1 bits, crc_ok set when the CRC is good.
Bits osmo_decode(Bits type5, const BlockSpec& spec, uint32_t scrambling, bool& crc_ok);

}  // namespace yantra::gen::osmo_test
