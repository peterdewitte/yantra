// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only reference decoder for the TETRA test-signal generator
// (RAD-70): descrambling, deinterleaving, depuncturing, a soft-input
// Viterbi decoder for the rate-1/4 mother code, CRC check and a
// maximum-likelihood RM(30,14) decoder. Written separately from the
// encoder (no shared tables), so encode/decode round trips test both.
// Never linked into the product.

#pragma once

#include "yantra/gen/bits.hpp"
#include "yantra/gen/coding.hpp"

#include <cstdint>

namespace yantra::gen::ref {

struct DecodedBlock {
    Bits type1;          ///< decoded type-1 bits
    bool crc_ok = false; ///< CRC-16 residual matched
    int corrected = 0;   ///< received type-5 bits that differ from the re-encoded block
};

/// Scrambling sequence written as the recurrence of EN 300 392-2 8.2.5.2
/// over a bit array (independent of the generator's shift register).
Bits scrambling_sequence(uint32_t init, size_t n);

/// Undo block interleaving: type3[i - 1] = type4[(a * i) mod K].
Bits deinterleave(const Bits& type4, int a);

/// Viterbi decoding of the 16-state rate-1/4 mother code. `soft` has 4
/// values per input bit: > 0 means bit 0, < 0 means bit 1, 0 = erased
/// (punctured). The trellis ends in state 0 (tail bits). Returns all
/// decoded bits including the tail.
Bits viterbi_decode(const std::vector<int>& soft);

/// Rate-2/3 depuncturing to soft values for viterbi_decode (+1 for a 0 bit,
/// -1 for a 1 bit, 0 for punctured positions).
std::vector<int> depuncture_2_3(const Bits& type3);

/// Full chain type-5 -> type-1 for one block of `spec` scrambled with
/// `scrambling`.
DecodedBlock decode_block(const Bits& type5, const BlockSpec& spec, uint32_t scrambling);

/// Checks a type-2 block without the tail (type-1 bits + 16 CRC bits).
bool crc_ok(const Bits& type1_and_crc);

/// Maximum-likelihood RM(30,14) decoding by search over all 2^14 code
/// words, independent of rm3014_encode (uses its own generator rows).
/// `distance` receives the Hamming distance to the chosen code word.
uint16_t rm3014_decode(uint32_t received30, int* distance = nullptr);

/// AACH: descramble and RM-decode 30 bits to the 14-bit ACCESS-ASSIGN.
uint16_t decode_aach(const Bits& type5, uint32_t scrambling, int* distance = nullptr);

}  // namespace yantra::gen::ref
