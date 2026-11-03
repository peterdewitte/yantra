// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Downlink logical channels of one cell's main carrier for the TETRA
// test-signal generator (RAD-70): which broadcast PDUs are sent in which
// timeslot, coded to type-5 bits per EN 300 392-2 clause 8. The burst
// builder and modulator (step 3.2) put these blocks into the burst fields
// (SB1/SB2 of a synchronization burst, BKN1/BKN2 of a normal burst, the
// AACH in the broadcast block BB).
//
// Schedule of the main carrier (one timeslot = one burst):
// - frame 18, every timeslot: synchronization burst, SB1 = BSCH with
//   MAC-SYNC + D-MLE-SYNC carrying that slot's TN/FN/MN, SB2 = BNCH with
//   MAC-SYSINFO + D-MLE-SYSINFO carrying the hyperframe number;
// - frames 1..17, timeslot 1 (MCCH): normal burst with one SCH/F block,
//   D-NWRK-BROADCAST every `nwrk_every_frames` frames when configured
//   (nwrk_broadcast and nwrk_more in turn), otherwise a MAC Null PDU;
// - frames 1..17, timeslots 2..4: normal burst with a SCH/F Null PDU;
// - block_sequences: SCH/F blocks prepared by the caller (e.g. the blocks
//   of a fragmented TM-SDU, fragment_blocks) on a timeslot in consecutive
//   frames, every multiframe, in place of the above;
// - every burst: the AACH with ACCESS-ASSIGN (access_assign, or in
//   frames 1..17 the access_assign_plan entry of that slot).
// Everything except the BSCH is scrambled with the cell's extended colour
// code (MCC, MNC, colour code of the SYNC PDU).
// Copied from yantra 68ea20a (plugins/tetra/gen cell_tx), split so that
// this part stops at the coded blocks.

#pragma once

#include "bits.hpp"
#include "pdu.hpp"
#include "timing.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace yantra::gen {

/// Downlink frequency of a carrier from the MAC-SYSINFO carrier fields:
/// band * 100 MHz + carrier * 25 kHz + offset (code 0: 0, 1: +6.25 kHz,
/// 2: -6.25 kHz, 3: +12.5 kHz). std::out_of_range for codes out of their
/// field.
double downlink_frequency_hz(int frequency_band, int main_carrier, int offset_code = 0);

/// One cell's main carrier. `sync` and `sysinfo` are templates: the
/// schedule fills in the timing fields (timeslot, frame, multiframe; the
/// hyperframe number when cck_flag is false) of every slot.
struct CellConfig {
    MacSync sync;
    MacSysinfo sysinfo;
    AccessAssign access_assign{{0, 1}, {0, 1}};
    /// D-NWRK-BROADCAST on the MCCH; none: only Null PDUs.
    std::optional<NwrkBroadcast> nwrk_broadcast;
    /// Further D-NWRK-BROADCAST PDUs (a longer neighbour list, see
    /// split_nwrk_broadcast): the D-NWRK-BROADCAST frames send
    /// nwrk_broadcast and these in turn.
    std::vector<NwrkBroadcast> nwrk_more;
    /// Frames (1..17) of timeslot 1 with D-NWRK-BROADCAST: fn - 1 is a
    /// multiple of this (6: frames 1, 7 and 13).
    int nwrk_every_frames = 6;
    /// Blocks sent on `timeslot` in frames first_frame, first_frame + 1, ...
    /// (all within 1..17) of every multiframe.
    struct BlockSequence {
        int timeslot = 1;
        int first_frame = 2;
        std::vector<Bits> blocks;  ///< SCH/F MAC blocks (268 type-1 bits)
    };
    std::vector<BlockSequence> block_sequences;
    /// ACCESS-ASSIGN per slot of frames 1..17, in turn (see
    /// planned_access_assign); empty: access_assign everywhere.
    std::vector<AccessAssign> access_assign_plan;
    /// Time of the first slot (used by the transmitter of step 3.2).
    TdmaTime start;

    /// The defaults: MCC 901, MNC 1, colour code 1, LA 1, main carrier
    /// 3600 in band 3 (390.000 MHz), extended services with security
    /// class 1, no D-NWRK-BROADCAST.
    static CellConfig defaults();

    /// The cell's scrambling init (extended colour code).
    uint32_t scrambling() const;
};

/// Throws (std::invalid_argument, std::out_of_range, std::length_error)
/// when `cfg` cannot be encoded: encodes one slot of every kind once.
void validate(const CellConfig& cfg);

/// The ACCESS-ASSIGN `cfg` sends at time `t` (frames 1..17: slot k of the
/// multiframe, k = (fn - 1) * 4 + tn - 1, sends plan entry k mod size).
const AccessAssign& planned_access_assign(const CellConfig& cfg, const TdmaTime& t);

/// Which PDU a normal slot carries (for tests and the ground truth).
enum class SlotContent : uint8_t {
    Sync,           ///< frame 18: BSCH + BNCH
    NwrkBroadcast,  ///< SCH/F with D-NWRK-BROADCAST
    BlockSequence,  ///< SCH/F block of a block_sequences entry
    Null,           ///< SCH/F with a MAC Null PDU
};

/// The type-1 blocks of one slot, before coding.
struct SlotPdus {
    SlotContent content = SlotContent::Null;
    uint16_t access_assign = 0;  ///< 14 bits for the AACH
    /// Sync: the 60 BSCH bits and the 124 BNCH bits; otherwise the 268
    /// SCH/F bits in `first` and `second` empty.
    Bits first;
    Bits second;
};

/// The coded (type-5) blocks of one slot.
struct SlotBlocks {
    SlotContent content = SlotContent::Null;
    Bits aach;  ///< 30 bits, for the broadcast block BB
    /// Sync: SB1 (BSCH, 120 bits) and SB2 (BNCH, 216 bits). Otherwise the
    /// SCH/F block (432 bits) split into BKN1 = bits 0..215 and BKN2 =
    /// bits 216..431.
    Bits block1;
    Bits block2;
};

/// Bits of one block field of a normal burst (BKN1, BKN2) and of SB2.
inline constexpr int kBlockFieldBits = 216;

/// The PDUs `cfg` sends at time `t`. std::invalid_argument for a time out
/// of range or a bad configuration; std::length_error when a PDU does not
/// fit its block.
SlotPdus downlink_pdus(const CellConfig& cfg, const TdmaTime& t);

/// downlink_pdus coded per logical channel (BSCH, BNCH, SCH/F, AACH).
SlotBlocks downlink_blocks(const CellConfig& cfg, const TdmaTime& t);

}  // namespace yantra::gen
