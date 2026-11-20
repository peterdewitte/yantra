// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Continuous downlink bursts (EN 300 392-2 clause 9.4.4) for the TETRA
// test-signal generator (RAD-70). Copied from yantra 68ea20a
// (plugins/tetra/gen/src/burst.cpp).

#include "yantra/gen/burst.hpp"

#include <stdexcept>
#include <string>

namespace yantra::gen {

namespace {

void require_size(const Bits& b, int expected, const char* what)
{
    if (static_cast<int>(b.size()) != expected) {
        throw std::invalid_argument(std::string(what) + ": expected " + std::to_string(expected) +
                                    " bits, got " + std::to_string(b.size()));
    }
}

// Writes the phase adjustment dibit `field` into `burst` (clause 9.4.4.3.6).
void put_phase_adjustment(Bits& burst, PhaseAdjustment field)
{
    const PhaseAdjustmentRange r = phase_adjustment_range(field);
    const auto [b1, b2] = dibit_for_phase_step(-phase_sum(burst, r.first, r.last));
    burst[static_cast<size_t>(2 * (r.symbol - 1))] = b1;
    burst[static_cast<size_t>(2 * (r.symbol - 1) + 1)] = b2;
}

void append_range(BitWriter& w, const Bits& b, size_t first, size_t count)
{
    w.append(Bits(b.begin() + static_cast<long>(first), b.begin() + static_cast<long>(first + count)));
}

}  // namespace

const Bits& normal_training_sequence_1()
{
    static const Bits n = from_string("1101000011101001110100");
    return n;
}

const Bits& normal_training_sequence_2()
{
    static const Bits p = from_string("0111101001000011011110");
    return p;
}

const Bits& normal_training_sequence_3()
{
    static const Bits q = from_string("1011011100000110101101");
    return q;
}

const Bits& sync_training_sequence()
{
    static const Bits y = from_string("11000001100111001110100111000001100111");
    return y;
}

const Bits& frequency_correction_field()
{
    static const Bits f = [] {
        Bits b(80, 0);
        for (int i = 0; i < 8; ++i) {
            b[static_cast<size_t>(i)] = 1;
            b[static_cast<size_t>(72 + i)] = 1;
        }
        return b;
    }();
    return f;
}

int dibit_phase_step(uint8_t b1, uint8_t b2)
{
    // Index b1 b2: 00 +pi/4, 01 +3pi/4, 10 -pi/4, 11 -3pi/4.
    static constexpr int kStep[4] = {1, 3, -1, -3};
    return kStep[((b1 & 1u) << 1) | (b2 & 1u)];
}

std::pair<uint8_t, uint8_t> dibit_for_phase_step(int step)
{
    switch (((step % 8) + 8) % 8) {
    case 1:
        return {0, 0};
    case 3:
        return {0, 1};
    case 5:  // -3
        return {1, 1};
    case 7:  // -1
        return {1, 0};
    default:
        throw std::invalid_argument("pi/4-DQPSK phase step must be odd, got " + std::to_string(step));
    }
}

int phase_sum(const Bits& burst, int first, int last)
{
    if (first < 1 || last < first - 1 || static_cast<size_t>(2 * last) > burst.size())
        throw std::out_of_range("phase_sum: symbols out of range");
    int sum = 0;
    for (int s = first; s <= last; ++s) {
        const size_t i = static_cast<size_t>(2 * (s - 1));
        sum += dibit_phase_step(burst[i], burst[i + 1]);
    }
    return sum;
}

PhaseAdjustmentRange phase_adjustment_range(PhaseAdjustment field)
{
    switch (field) {
    case PhaseAdjustment::HA:
        return {7, 8, 122};
    case PhaseAdjustment::HB:
        return {250, 123, 249};
    case PhaseAdjustment::HC:
        return {7, 8, 108};
    case PhaseAdjustment::HD:
        return {250, 109, 249};
    }
    throw std::invalid_argument("unknown phase adjustment field");
}

Bits sync_downlink_burst(const Bits& sb1, const Bits& bb, const Bits& sb2)
{
    require_size(sb1, kSb1Bits, "sync burst SB1");
    require_size(bb, kBroadcastBlockBits, "sync burst broadcast block");
    require_size(sb2, kBlockFieldBits, "sync burst SB2");
    const Bits& q = normal_training_sequence_3();
    BitWriter w;
    append_range(w, q, 10, 12);
    w.put(0, 2, "HC");
    w.append(frequency_correction_field());
    w.append(sb1);
    w.append(sync_training_sequence());
    w.append(bb);
    w.append(sb2);
    w.put(0, 2, "HD");
    append_range(w, q, 0, 10);
    Bits burst = w.take();
    put_phase_adjustment(burst, PhaseAdjustment::HC);
    put_phase_adjustment(burst, PhaseAdjustment::HD);
    return burst;
}

Bits normal_downlink_burst(const Bits& bkn1, const Bits& bb, const Bits& bkn2, bool two_half_slots)
{
    require_size(bkn1, kBlockFieldBits, "normal burst bkn1");
    require_size(bb, kBroadcastBlockBits, "normal burst broadcast block");
    require_size(bkn2, kBlockFieldBits, "normal burst bkn2");
    const Bits& q = normal_training_sequence_3();
    BitWriter w;
    append_range(w, q, 10, 12);
    w.put(0, 2, "HA");
    w.append(bkn1);
    append_range(w, bb, 0, 14);
    w.append(two_half_slots ? normal_training_sequence_2() : normal_training_sequence_1());
    append_range(w, bb, 14, 16);
    w.append(bkn2);
    w.put(0, 2, "HB");
    append_range(w, q, 0, 10);
    Bits burst = w.take();
    put_phase_adjustment(burst, PhaseAdjustment::HA);
    put_phase_adjustment(burst, PhaseAdjustment::HB);
    return burst;
}

Bits slot_burst(const SlotBlocks& blocks)
{
    if (blocks.content == SlotContent::Sync)
        return sync_downlink_burst(blocks.block1, blocks.aach, blocks.block2);
    return normal_downlink_burst(blocks.block1, blocks.aach, blocks.block2, false);
}

Bits downlink_burst(const CellConfig& cfg, const TdmaTime& t)
{
    return slot_burst(downlink_blocks(cfg, t));
}

}  // namespace yantra::gen
