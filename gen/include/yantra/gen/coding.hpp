// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA V+D downlink channel coding for the broadcast logical channels of
// the test-signal generator (RAD-70), after EN 300 392-2 clause 8:
// type-1 bits -> CRC-16 + tail (type-2) -> rate-1/4 mother code punctured
// to rate 2/3 (type-3) -> block interleaving (type-4) -> scrambling with
// the extended colour code (type-5). The AACH uses the shortened
// Reed-Muller (30,14) code instead. Own implementation, independent of
// osmo-tetra, so decoding the generator's output with osmo-tetra (RAD-101)
// is a real cross-check.

#pragma once

#include "bits.hpp"

#include <string_view>

namespace yantra::gen {

/// Sizes of one coded block: type-1 bits, type-5 bits, interleaver a.
struct BlockSpec {
    std::string_view name;
    int type1;
    int type5;
    int a;
};

/// BSCH (sync burst block 1, SB1): carries MAC-SYNC + D-MLE-SYNC.
inline constexpr BlockSpec kBsch{"BSCH", 60, 120, 11};
/// SCH/HD, BNCH and STCH half-slot blocks (SB2 and the two half blocks of
/// a normal burst): the BNCH carries MAC-SYSINFO + D-MLE-SYSINFO.
inline constexpr BlockSpec kSchHd{"SCH/HD", 124, 216, 101};
/// SCH/F full-slot block (MCCH signalling, e.g. D-NWRK-BROADCAST).
inline constexpr BlockSpec kSchF{"SCH/F", 268, 432, 103};

inline constexpr int kCrcBits = 16;
inline constexpr int kTailBits = 4;
/// CRC register after a received block plus its (inverted) CRC when no
/// bit is wrong.
inline constexpr uint16_t kCrcResidual = 0x1D0F;

/// The BSCH is scrambled with the extended colour code 0 (init value 3),
/// since the receiver learns MCC/MNC/colour code from it.
inline constexpr uint32_t kScramblingInitBsch = 3;

/// AACH (access assignment channel) type-5 size.
inline constexpr int kAachBits = 30;

/// CRC-16 shift register (generator x^16 + x^12 + x^5 + 1) after feeding
/// `n` bits MSB first, starting from `init` (0xFFFF for TETRA).
uint16_t crc16_register(const uint8_t* bits, size_t n, uint16_t init = 0xFFFF);

/// type-1 -> type-2: appends the inverted CRC (16 bits) and 4 zero tail
/// bits that return the encoder to state 0.
Bits add_crc_and_tail(const Bits& type1);

/// Rate-1/4 16-state mother code with G1 = 1+D+D^4, G2 = 1+D^2+D^3+D^4,
/// G3 = 1+D+D^2+D^4, G4 = 1+D+D^3+D^4; output order V(4(k-1)+i) = Gi at
/// input bit k. Starts in state 0.
Bits conv_encode_mother(const Bits& type2);

/// Rate-2/3 puncturing of the mother code: P = {1, 2, 5}, t = 3, period 8.
/// The input size must be a multiple of 8 (type-2 size even).
Bits puncture_2_3(const Bits& mother);

/// (K, a) block interleaving: b4(k) = b3(i), k = 1 + (a * i mod K).
Bits block_interleave(const Bits& type3, int a);

/// Scrambling init value from the extended colour code (MCC 10, MNC 14,
/// colour code 6 bits): ((cc | mnc << 6 | mcc << 20) << 2) | 3.
uint32_t scrambling_init(uint16_t mcc, uint16_t mnc, uint8_t colour_code);

/// The first `n` bits of the scrambling sequence for `init` (32-bit LFSR,
/// taps 32 26 23 22 16 12 11 10 8 7 5 4 2 1).
Bits scrambling_sequence(uint32_t init, size_t n);

/// XORs `bits` with the scrambling sequence (scrambling = descrambling).
void scramble(Bits& bits, uint32_t init);

/// Shortened Reed-Muller (30,14) code of the AACH, systematic: the 14 input
/// bits followed by 16 parity bits, returned in the low 30 bits MSB first.
uint32_t rm3014_encode(uint16_t in14);

/// Full chain type-1 -> type-5 for one block of `spec`.
Bits encode_block(const Bits& type1, const BlockSpec& spec, uint32_t scrambling);

/// BSCH (SB1): 60 type-1 bits (MAC-SYNC + D-MLE-SYNC) -> 120 bits.
Bits encode_bsch(const Bits& sync60);
/// BNCH / SCH/HD: 124 type-1 bits -> 216 bits.
Bits encode_sch_hd(const Bits& block124, uint32_t scrambling);
/// SCH/F: 268 type-1 bits -> 432 bits.
Bits encode_sch_f(const Bits& block268, uint32_t scrambling);
/// AACH: 14 bits ACCESS-ASSIGN -> RM(30,14) -> scrambled 30 bits.
Bits encode_aach(uint16_t access_assign14, uint32_t scrambling);

}  // namespace yantra::gen
