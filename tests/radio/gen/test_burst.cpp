// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Continuous downlink bursts of the TETRA generator (RAD-70, RAD-93) against the
// tables of EN 300 392-2 clause 9.4.4: field positions, training
// sequences, tail bits, dibit phase steps and the phase adjustment bits.
// Offsets and sequences are written out here from the standard, not taken
// from the generator. The downlink_burst() schedule part: frame 18 sends a
// synchronization burst on all four timeslots, frames 1..17 normal bursts,
// each carrying the coded blocks of downlink_blocks().
// Copied from yantra 68ea20a (tests/tetra_gen/test_burst.cpp); the
// schedule case added.

#include "yantra/gen/burst.hpp"
#include "yantra/gen/downlink.hpp"

#include <doctest/doctest.h>

#include <random>
#include <stdexcept>

using namespace yantra::gen;

namespace {

Bits random_bits(std::mt19937& rng, size_t n)
{
    Bits b(n);
    for (auto& v : b)
        v = static_cast<uint8_t>(rng() & 1u);
    return b;
}

Bits slice(const Bits& b, size_t first, size_t count)
{
    REQUIRE(first + count <= b.size());
    return Bits(b.begin() + static_cast<long>(first), b.begin() + static_cast<long>(first + count));
}

// Training sequences of clause 9.4.4.3 as printed in the standard.
const char* const kN = "1101000011101001110100";
const char* const kP = "0111101001000011011110";
const char* const kQ = "1011011100000110101101";
const char* const kY = "11000001100111001110100111000001100111";

// (phase step of symbols first..last + step of the adjustment symbol) mod 8.
int residual(const Bits& burst, int adj_symbol, int first, int last)
{
    int sum = 0;
    for (int s = first; s <= last; ++s)
        sum += dibit_phase_step(burst[2 * (s - 1)], burst[2 * (s - 1) + 1]);
    sum += dibit_phase_step(burst[2 * (adj_symbol - 1)], burst[2 * (adj_symbol - 1) + 1]);
    return ((sum % 8) + 8) % 8;
}

}  // namespace

TEST_CASE("Training sequences and the frequency correction field match the standard")
{
    CHECK(to_string(normal_training_sequence_1()) == kN);
    CHECK(to_string(normal_training_sequence_2()) == kP);
    CHECK(to_string(normal_training_sequence_3()) == kQ);
    CHECK(to_string(sync_training_sequence()) == kY);
    CHECK(to_string(frequency_correction_field()) ==
          "11111111" + std::string(64, '0') + "11111111");
}

TEST_CASE("Dibit phase steps (known answers) and their inverse")
{
    CHECK(dibit_phase_step(0, 0) == 1);
    CHECK(dibit_phase_step(0, 1) == 3);
    CHECK(dibit_phase_step(1, 1) == -3);
    CHECK(dibit_phase_step(1, 0) == -1);
    for (int step : {1, 3, -1, -3, 9, -9, 5, 7}) {
        CAPTURE(step);
        const auto [b1, b2] = dibit_for_phase_step(step);
        CHECK((((dibit_phase_step(b1, b2) - step) % 8) + 8) % 8 == 0);
    }
    CHECK_THROWS_AS(dibit_for_phase_step(0), std::invalid_argument);
    CHECK_THROWS_AS(dibit_for_phase_step(2), std::invalid_argument);
    CHECK_THROWS_AS(dibit_for_phase_step(-4), std::invalid_argument);
}

TEST_CASE("Synchronization continuous downlink burst layout")
{
    std::mt19937 rng(11);
    const Bits sb1 = random_bits(rng, 120);
    const Bits bb = random_bits(rng, 30);
    const Bits sb2 = random_bits(rng, 216);
    const Bits b = sync_downlink_burst(sb1, bb, sb2);
    REQUIRE(b.size() == 510);
    const std::string q = kQ;
    CHECK(to_string(slice(b, 0, 12)) == q.substr(10, 12));  // q11..q22
    CHECK(to_string(slice(b, 14, 80)) == to_string(frequency_correction_field()));
    CHECK(slice(b, 94, 120) == sb1);
    CHECK(to_string(slice(b, 214, 38)) == kY);
    CHECK(slice(b, 252, 30) == bb);
    CHECK(slice(b, 282, 216) == sb2);
    CHECK(to_string(slice(b, 500, 10)) == q.substr(0, 10));  // q1..q10
}

TEST_CASE("Normal continuous downlink burst layout, full slot and two half slots")
{
    std::mt19937 rng(12);
    const Bits bkn1 = random_bits(rng, 216);
    const Bits bb = random_bits(rng, 30);
    const Bits bkn2 = random_bits(rng, 216);
    for (bool half : {false, true}) {
        CAPTURE(half);
        const Bits b = normal_downlink_burst(bkn1, bb, bkn2, half);
        REQUIRE(b.size() == 510);
        const std::string q = kQ;
        CHECK(to_string(slice(b, 0, 12)) == q.substr(10, 12));
        CHECK(slice(b, 14, 216) == bkn1);
        CHECK(slice(b, 230, 14) == slice(bb, 0, 14));
        CHECK(to_string(slice(b, 244, 22)) == (half ? kP : kN));
        CHECK(slice(b, 266, 16) == slice(bb, 14, 16));
        CHECK(slice(b, 282, 216) == bkn2);
        CHECK(to_string(slice(b, 500, 10)) == q.substr(0, 10));
    }
}

TEST_CASE("Phase adjustment bits cancel the phase steps of their symbol ranges")
{
    // Clause 9.4.4.3.6: HA symbols 8..122, HB 123..249, HC 8..108, HD
    // 109..249; HA/HC are symbol 7, HB/HD symbol 250.
    CHECK(phase_adjustment_range(PhaseAdjustment::HA).symbol == 7);
    CHECK(phase_adjustment_range(PhaseAdjustment::HA).first == 8);
    CHECK(phase_adjustment_range(PhaseAdjustment::HA).last == 122);
    CHECK(phase_adjustment_range(PhaseAdjustment::HB).symbol == 250);
    CHECK(phase_adjustment_range(PhaseAdjustment::HB).first == 123);
    CHECK(phase_adjustment_range(PhaseAdjustment::HB).last == 249);
    CHECK(phase_adjustment_range(PhaseAdjustment::HC).symbol == 7);
    CHECK(phase_adjustment_range(PhaseAdjustment::HC).first == 8);
    CHECK(phase_adjustment_range(PhaseAdjustment::HC).last == 108);
    CHECK(phase_adjustment_range(PhaseAdjustment::HD).symbol == 250);
    CHECK(phase_adjustment_range(PhaseAdjustment::HD).first == 109);
    CHECK(phase_adjustment_range(PhaseAdjustment::HD).last == 249);

    std::mt19937 rng(13);
    int total_sync = -1;
    int total_normal = -1;
    for (int round = 0; round < 50; ++round) {
        CAPTURE(round);
        const Bits s = sync_downlink_burst(random_bits(rng, 120), random_bits(rng, 30), random_bits(rng, 216));
        CHECK(residual(s, 7, 8, 108) == 0);
        CHECK(residual(s, 250, 109, 249) == 0);
        const Bits n = normal_downlink_burst(random_bits(rng, 216), random_bits(rng, 30), random_bits(rng, 216),
                                             round % 2 == 1);
        CHECK(residual(n, 7, 8, 122) == 0);
        CHECK(residual(n, 250, 123, 249) == 0);
        // So the phase change over a whole burst does not depend on its
        // contents: the training sequences sit at a known phase.
        const int ts = ((phase_sum(s, 1, 255) % 8) + 8) % 8;
        const int tn = ((phase_sum(n, 1, 255) % 8) + 8) % 8;
        if (round == 0) {
            total_sync = ts;
            total_normal = tn;
        }
        CHECK(ts == total_sync);
        CHECK(tn == total_normal);
    }
}

TEST_CASE("Burst builders refuse fields of the wrong size")
{
    const Bits b216(216, 0), b30(30, 0), b120(120, 0);
    CHECK_THROWS_AS(sync_downlink_burst(Bits(119, 0), b30, b216), std::invalid_argument);
    CHECK_THROWS_AS(sync_downlink_burst(b120, Bits(29, 0), b216), std::invalid_argument);
    CHECK_THROWS_AS(sync_downlink_burst(b120, b30, Bits(217, 0)), std::invalid_argument);
    CHECK_THROWS_AS(normal_downlink_burst(Bits(215, 0), b30, b216, false), std::invalid_argument);
    CHECK_THROWS_AS(normal_downlink_burst(b216, Bits(31, 0), b216, false), std::invalid_argument);
    CHECK_THROWS_AS(normal_downlink_burst(b216, b30, Bits(0), true), std::invalid_argument);
    CHECK_THROWS_AS(phase_sum(b216, 0, 3), std::out_of_range);
    CHECK_THROWS_AS(phase_sum(b216, 1, 109), std::out_of_range);
}

TEST_CASE("Downlink bursts of a continuous carrier: frame 18 sync bursts on all timeslots, normal bursts otherwise")
{
    CellConfig cfg = CellConfig::defaults();
    NwrkBroadcast nb;
    nb.network_time = NetworkTime{};
    NeighbourCell n;
    n.cell_id = 1;
    n.main_carrier = 3604;
    nb.neighbours = std::vector<NeighbourCell>{n};
    cfg.nwrk_broadcast = nb;
    TdmaTime t{1, 1, 60, 65535};  // across the multiframe and hyperframe wrap
    int sync = 0;
    int normal = 0;
    for (int i = 0; i < 2 * 18 * 4; ++i, t.advance()) {
        CAPTURE(i);
        const SlotBlocks blocks = downlink_blocks(cfg, t);
        const Bits b = downlink_burst(cfg, t);
        REQUIRE(b.size() == 510);
        CHECK(b == slot_burst(blocks));
        const std::string q = kQ;
        CHECK(to_string(slice(b, 0, 12)) == q.substr(10, 12));
        CHECK(to_string(slice(b, 500, 10)) == q.substr(0, 10));
        if (t.fn == 18) {
            ++sync;
            CHECK(blocks.content == SlotContent::Sync);
            CHECK(to_string(slice(b, 14, 80)) == to_string(frequency_correction_field()));
            CHECK(slice(b, 94, 120) == blocks.block1);
            CHECK(to_string(slice(b, 214, 38)) == kY);
            CHECK(slice(b, 252, 30) == blocks.aach);
            CHECK(slice(b, 282, 216) == blocks.block2);
            CHECK(residual(b, 7, 8, 108) == 0);
            CHECK(residual(b, 250, 109, 249) == 0);
        } else {
            ++normal;
            CHECK(blocks.content != SlotContent::Sync);
            CHECK(slice(b, 14, 216) == blocks.block1);
            CHECK(slice(b, 230, 14) == slice(blocks.aach, 0, 14));
            CHECK(to_string(slice(b, 244, 22)) == kN);
            CHECK(slice(b, 266, 16) == slice(blocks.aach, 14, 16));
            CHECK(slice(b, 282, 216) == blocks.block2);
            CHECK(residual(b, 7, 8, 122) == 0);
            CHECK(residual(b, 250, 123, 249) == 0);
        }
    }
    CHECK(sync == 2 * 4);
    CHECK(normal == 2 * 17 * 4);
    CHECK_THROWS_AS(downlink_burst(cfg, TdmaTime{5, 1, 1, 0}), std::invalid_argument);
}
