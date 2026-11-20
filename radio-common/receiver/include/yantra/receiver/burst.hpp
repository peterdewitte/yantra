// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA downlink bursts on the receive side (RAD-100, RAD-101, DEC-01):
// soft bits as osmo-tetra takes them, the burst types, the training
// sequences and the frequency correction field (EN 300 392-2 9.4.4.3) and
// their bit positions, and a hard-decision error count against them. The
// burst synchroniser (burst_sync.hpp) and the osmo-tetra wrapper build on
// these.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace yantra::receiver::tetra {

/// Bits in one downlink timeslot (255 symbols).
inline constexpr int kSlotBits = 510;
inline constexpr int kSlotSymbols = 255;

/// Soft bit as osmo-tetra / libosmocore take it: +127 = surely 0,
/// -127 = surely 1, 0 = unknown (erased).
using SoftBit = int8_t;

/// Downlink continuous burst types (EN 300 392-2 9.4.4.2).
enum class BurstType : uint8_t {
    Sync,        ///< synchronisation burst: BSCH (SB1), AACH, SB2
    NormalFull,  ///< normal burst, training sequence 1: one full-slot block (SCH/F)
    NormalHalf,  ///< normal burst, training sequence 2: two half-slot blocks
};

/// One burst found by the burst synchroniser.
struct Burst {
    BurstType type = BurstType::NormalFull;
    std::array<SoftBit, kSlotBits> soft{};
    double sample_pos = 0.0;  ///< demodulator sample position of the first symbol
    int training_errors = 0;  ///< hard bit errors in its training sequence
};

/// Training sequences (bit values as transmitted, EN 300 392-2 9.4.4.3.2/4).
inline constexpr std::array<uint8_t, 22> kNormalTraining1 = {1, 1, 0, 1, 0, 0, 0, 0, 1, 1, 1,
                                                             0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0};
inline constexpr std::array<uint8_t, 22> kNormalTraining2 = {0, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0,
                                                             0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0};
inline constexpr std::array<uint8_t, 38> kSyncTraining = {1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1,
                                                          1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1,
                                                          0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1};
/// Frequency correction field of the synchronisation burst: 8 ones, 64
/// zeros, 8 ones (EN 300 392-2 9.4.4.3.1).
inline constexpr std::array<uint8_t, 80> kFrequencyCorrection = [] {
    std::array<uint8_t, 80> f{};
    for (std::size_t i = 0; i < 8; ++i)
        f[i] = f[72 + i] = 1;
    return f;
}();

/// Bit positions inside a burst.
inline constexpr int kNormalTrainingBit = 244;  ///< after q(12) ha(2) bkn1(216) bb(14)
inline constexpr int kSyncTrainingBit = 214;    ///< after q(12) hc(2) f(80) sb1(120)
inline constexpr int kFrequencyCorrectionBit = 14;

/// The training sequence of a burst type and its first bit in the burst.
std::span<const uint8_t> training_sequence(BurstType type) noexcept;
int training_sequence_bit(BurstType type) noexcept;

/// Hard-decision bit errors of `pattern` against `soft` (soft < 0 is a 1).
int bit_errors(std::span<const SoftBit> soft, std::span<const uint8_t> pattern) noexcept;

}  // namespace yantra::receiver::tetra
