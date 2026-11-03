// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Continuous downlink bursts of the TETRA test-signal generator (RAD-70),
// after EN 300 392-2 clause 9.4.4: the synchronization continuous downlink
// burst (frequency correction, SB1 = BSCH, sync training sequence, AACH,
// SB2 = BNCH) and the normal continuous downlink burst (two 216-bit
// blocks around the AACH and a normal training sequence), each 510 bits =
// 255 pi/4-DQPSK symbols, with the tail bits and the phase adjustment
// bits the standard specifies (clause 9.4.4.3). downlink_burst() puts the
// coded blocks of the downlink schedule (downlink.hpp) into these bursts.
// Copied from yantra 68ea20a (plugins/tetra/gen burst.h, the
// downlink_burst() of cell_tx.cpp).

#pragma once

#include "bits.hpp"
#include "downlink.hpp"
#include "timing.hpp"

#include <cstdint>
#include <utility>

namespace yantra::gen {

/// Bits and symbols of one timeslot (continuous downlink burst).
inline constexpr int kSlotBits = 510;
inline constexpr int kSlotSymbols = 255;
// kBlockFieldBits (216, one block field: bkn1, bkn2, SB2) is in
// downlink.hpp.
/// SB1 (BSCH) field and AACH broadcast block (bb) field.
inline constexpr int kSb1Bits = 120;
inline constexpr int kBroadcastBlockBits = 30;

/// Normal training sequence 1 "n" (22 bits): full-slot normal bursts.
const Bits& normal_training_sequence_1();
/// Normal training sequence 2 "p" (22 bits): normal bursts carrying two
/// half-slot blocks (slot flag set).
const Bits& normal_training_sequence_2();
/// Normal training sequence 3 "q" (22 bits): its bits q11..q22 start and
/// q1..q10 end every continuous downlink burst (tail bits).
const Bits& normal_training_sequence_3();
/// Synchronization training sequence "y" (38 bits).
const Bits& sync_training_sequence();
/// Frequency correction field "f" (80 bits: 8 ones, 64 zeros, 8 ones).
const Bits& frequency_correction_field();

/// pi/4-DQPSK phase step of a dibit in units of pi/4 (clause 5):
/// 00 -> +1, 01 -> +3, 11 -> -3, 10 -> -1.
int dibit_phase_step(uint8_t b1, uint8_t b2);
/// The dibit whose phase step is `step` (mod 8, units of pi/4); the step
/// must be odd (std::invalid_argument otherwise).
std::pair<uint8_t, uint8_t> dibit_for_phase_step(int step);
/// Sum of the phase steps of the 1-based symbols first..last of `burst`
/// (units of pi/4, not reduced).
int phase_sum(const Bits& burst, int first, int last);

/// Phase adjustment fields of the continuous downlink bursts.
enum class PhaseAdjustment { HA, HB, HC, HD };

/// Where a phase adjustment symbol is and which symbols it compensates
/// (1-based, clause 9.4.4.3.6): its phase step is -(sum of the steps of
/// symbols first..last) mod 2 pi.
struct PhaseAdjustmentRange {
    int symbol;
    int first;
    int last;
};
PhaseAdjustmentRange phase_adjustment_range(PhaseAdjustment field);

/// Synchronization continuous downlink burst: q11..q22, HC, f, SB1 (120
/// bits, coded BSCH), y, bb (30 bits, coded AACH), SB2 (216 bits, coded
/// BNCH), HD, q1..q10. std::invalid_argument on a wrong field size.
Bits sync_downlink_burst(const Bits& sb1, const Bits& bb, const Bits& sb2);

/// Normal continuous downlink burst: q11..q22, HA, bkn1, bb1..bb14, n or
/// p, bb15..bb30, bkn2, HB, q1..q10. `two_half_slots` selects training
/// sequence p (two half-slot blocks) instead of n (one full-slot block
/// split over bkn1 and bkn2). std::invalid_argument on a wrong field size.
Bits normal_downlink_burst(const Bits& bkn1, const Bits& bb, const Bits& bkn2, bool two_half_slots);

/// The burst of one slot's coded blocks: a synchronization burst for
/// SlotContent::Sync (SB1, BB, SB2), otherwise a normal burst with training
/// sequence n (one full-slot SCH/F block in bkn1 + bkn2).
Bits slot_burst(const SlotBlocks& blocks);

/// The 510 bits of the burst `cfg` sends at time `t`:
/// slot_burst(downlink_blocks(cfg, t)). Same exceptions as downlink_blocks.
Bits downlink_burst(const CellConfig& cfg, const TdmaTime& t);

}  // namespace yantra::gen
