// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Training sequences and the bit-error-tolerant burst synchroniser
// (RAD-100, RAD-101, DEC-01): the receiver's training sequences are the
// generator's, sequences with bit errors are still found, symbol slips are
// followed, a lost signal drops the lock and a new one is acquired, and
// the demodulator's output on generator IQ with noise and a carrier
// offset gives a burst in every slot.
// Copied from yantra 68ea20a: tests/tetra_decoder/test_burst_sync.cpp,
// adapted for yantra::receiver.

#include "signal.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/receiver/burst_sync.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <random>

using namespace dsptest;
namespace gen = yantra::gen;
namespace rt = yantra::receiver::tetra;
using yantra::receiver::DemodSymbol;

namespace {

constexpr int kNormalAt = rt::kNormalTrainingBit;
constexpr int kSyncAt = rt::kSyncTrainingBit;

// Demodulator symbols for a bit sequence (2 bits per symbol, soft +/-0.7
// like the demodulator at a clean signal).
std::vector<DemodSymbol> symbols_of(const gen::Bits& bits, uint64_t first_index = 0)
{
    std::vector<DemodSymbol> s(bits.size() / 2);
    for (std::size_t k = 0; k < s.size(); ++k) {
        s[k].index = first_index + k;
        s[k].sample_pos = 4.0 * static_cast<double>(first_index + k);
        s[k].soft[0] = bits[2 * k] ? -0.7f : 0.7f;
        s[k].soft[1] = bits[2 * k + 1] ? -0.7f : 0.7f;
    }
    return s;
}

// The bursts of `n` slots of a cell.
std::vector<gen::Bits> bursts(const gen::CellConfig& cell, int n)
{
    gen::CellTransmitter tx(cell, 72000.0);
    std::vector<gen::Bits> out;
    for (int i = 0; i < n; ++i)
        out.push_back(tx.next_burst());
    return out;
}

gen::Bits stream_with_errors(int slots, int normal_errors, int sync_errors, uint64_t seed)
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 16, 1, 0};
    std::mt19937_64 rng(seed);
    gen::Bits all;
    const gen::Bits& y = gen::sync_training_sequence();
    for (gen::Bits b : bursts(cell, slots)) {
        const bool sync = std::equal(y.begin(), y.end(), b.begin() + kSyncAt);
        const int at = sync ? kSyncAt : kNormalAt;
        const int len = sync ? 38 : 22;
        const int errors = sync ? sync_errors : normal_errors;
        std::vector<int> pos(static_cast<std::size_t>(len));
        for (int i = 0; i < len; ++i)
            pos[static_cast<std::size_t>(i)] = i;
        std::shuffle(pos.begin(), pos.end(), rng);
        for (int i = 0; i < errors; ++i)
            b[static_cast<std::size_t>(at + pos[static_cast<std::size_t>(i)])] ^= 1;
        all.insert(all.end(), b.begin(), b.end());
    }
    return all;
}

std::vector<rt::SoftBit> soft_of(const gen::Bits& bits)
{
    std::vector<rt::SoftBit> s(bits.size());
    for (std::size_t i = 0; i < bits.size(); ++i)
        s[i] = bits[i] ? -64 : 64;
    return s;
}

}  // namespace

TEST_CASE("The receiver's training sequences are the generator's (EN 300 392-2 9.4.4.3)")
{
    const auto same = [](std::span<const uint8_t> a, const gen::Bits& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end());
    };
    CHECK(same(rt::training_sequence(rt::BurstType::NormalFull), gen::normal_training_sequence_1()));
    CHECK(same(rt::training_sequence(rt::BurstType::NormalHalf), gen::normal_training_sequence_2()));
    CHECK(same(rt::training_sequence(rt::BurstType::Sync), gen::sync_training_sequence()));
    CHECK(rt::training_sequence_bit(rt::BurstType::Sync) == 214);
    CHECK(rt::training_sequence_bit(rt::BurstType::NormalHalf) == 244);

    // In generated bursts the sequences sit at their bit positions; the
    // synchronisation burst also carries the frequency correction field.
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 17, 1, 0};
    int sync = 0, normal = 0;
    for (const gen::Bits& b : bursts(cell, 8)) {
        const auto soft = soft_of(b);
        const std::span<const rt::SoftBit> s(soft);
        if (rt::bit_errors(s.subspan(kSyncAt, 38), rt::kSyncTraining) == 0) {
            ++sync;
            CHECK(rt::bit_errors(s.subspan(rt::kFrequencyCorrectionBit, 80), rt::kFrequencyCorrection) == 0);
        } else {
            const int e1 = rt::bit_errors(s.subspan(kNormalAt, 22), rt::kNormalTraining1);
            const int e2 = rt::bit_errors(s.subspan(kNormalAt, 22), rt::kNormalTraining2);
            CHECK((e1 == 0) != (e2 == 0));
            ++normal;
        }
    }
    CHECK(sync == 4);  // frame 18: a synchronisation burst in every slot
    CHECK(normal == 4);
    // One wrong bit is one error.
    auto soft = soft_of(gen::sync_training_sequence());
    soft[5] = static_cast<rt::SoftBit>(-soft[5]);
    CHECK(rt::bit_errors(soft, rt::kSyncTraining) == 1);
}

TEST_CASE("Training sequences with bit errors below the threshold are still found")
{
    // 3 of 22 bits wrong in every normal training sequence and 4 of 38 in
    // every synchronisation training sequence: an exact match (osmo-tetra's
    // tetra_find_train_seq) would find nothing.
    const gen::Bits bits = stream_with_errors(4 * 18 * 2, 3, 4, 7);
    rt::BurstSync sync;
    std::vector<rt::SlotEvent> ev;
    const auto sym = symbols_of(bits);
    sync.push(sym.data(), sym.size(), ev);
    REQUIRE(ev.size() > 100);
    int found = 0;
    for (const auto& e : ev) {
        found += e.found ? 1 : 0;
        CHECK_FALSE(e.lost);
        CHECK(e.slip == 0);
    }
    CHECK(found == static_cast<int>(ev.size()));
    CHECK(sync.locked());

    // Errors above the tracking threshold: every slot is missed, the lock
    // is lost after max_missed slots.
    const gen::Bits bad = stream_with_errors(4 * 18 * 2, 6, 9, 8);
    rt::BurstSync sync2;
    std::vector<rt::SlotEvent> ev2;
    // Acquire on clean bursts first, then feed the damaged ones.
    const gen::Bits good = stream_with_errors(4 * 18, 0, 0, 1);
    gen::Bits joined = good;
    joined.insert(joined.end(), bad.begin(), bad.end());
    const auto sym2 = symbols_of(joined);
    sync2.push(sym2.data(), sym2.size(), ev2);
    bool lost = false;
    for (const auto& e : ev2)
        lost = lost || e.lost;
    CHECK(lost);
}

TEST_CASE("Burst sync follows a symbol slip and reacquires after a gap")
{
    gen::CellConfig cell = gen::CellConfig::defaults();
    cell.start = {1, 17, 1, 0};
    gen::Bits bits;
    for (const gen::Bits& b : bursts(cell, 4 * 18 * 3))
        bits.insert(bits.end(), b.begin(), b.end());
    // Drop one symbol (2 bits) in the middle of slot 40.
    const std::size_t cut = 40 * 510 + 100;
    bits.erase(bits.begin() + static_cast<std::ptrdiff_t>(cut), bits.begin() + static_cast<std::ptrdiff_t>(cut + 2));
    rt::BurstSync sync;
    std::vector<rt::SlotEvent> ev;
    const auto sym = symbols_of(bits);
    sync.push(sym.data(), sym.size(), ev);
    int slips = 0;
    int missed = 0;
    for (const auto& e : ev) {
        slips += e.slip != 0 ? 1 : 0;
        missed += e.found ? 0 : 1;
    }
    CHECK(slips == 1);
    CHECK(missed == 0);

    // A gap of noise-like symbols (alternating soft signs that match no
    // training sequence). Shorter than max_missed + coast_slots (a fade):
    // the lock and the slot grid are kept, the bursts after it are found
    // again. Longer: the lock is lost, then found again on the next
    // synchronisation burst.
    const auto run_gap = [&](int slots, int& lost, int& coasting, int& found_after) {
        gen::Bits gap(510 * static_cast<std::size_t>(slots));
        for (std::size_t i = 0; i < gap.size(); ++i)
            gap[i] = static_cast<uint8_t>((i * 7 + i / 3) % 2);
        gen::Bits all = bits;
        all.insert(all.end(), gap.begin(), gap.end());
        all.insert(all.end(), bits.begin(), bits.end());
        rt::BurstSync sync2;
        std::vector<rt::SlotEvent> ev2;
        const auto sym2 = symbols_of(all);
        sync2.push(sym2.data(), sym2.size(), ev2);
        lost = coasting = found_after = 0;
        bool gap_seen = false;
        for (const auto& e : ev2) {
            gap_seen = gap_seen || !e.found;
            lost += e.lost ? 1 : 0;
            coasting += e.coasting ? 1 : 0;
            found_after += gap_seen && e.found ? 1 : 0;
        }
    };
    const rt::BurstSyncConfig defaults;
    int lost = 0, coasting = 0, found_after = 0;
    run_gap(20, lost, coasting, found_after);
    CHECK(lost == 0);
    CHECK(coasting > 0);
    CHECK(found_after > 200);  // every slot of the second part
    run_gap(defaults.max_missed + defaults.coast_slots + 12, lost, coasting, found_after);
    CHECK(lost == 1);
    CHECK(coasting == defaults.coast_slots);
    CHECK(found_after > 100);
}

TEST_CASE("Demodulated generator IQ with noise and a carrier offset gives a burst in every slot")
{
    SignalSpec spec;
    spec.bursts = 4 * 18 * 2;
    spec.cfo_hz = 1200.0;
    spec.clock_ppm = 20.0;
    spec.ebn0_db = 14.0;
    spec.seed = 11;
    const TestSignal t = make_signal(spec);
    yantra::receiver::Pi4DqpskDemodulator d(rt::demod_config());
    const auto sym = demodulate(d, t.iq);
    rt::BurstSync sync;
    std::vector<rt::SlotEvent> ev;
    sync.push(sym.data(), sym.size(), ev);
    // Acquisition needs the first synchronisation burst (frame 18 starts
    // the train), so nearly every slot of the train gives an event.
    REQUIRE(ev.size() >= static_cast<std::size_t>(spec.bursts - 8));
    int found = 0, sync_bursts = 0;
    for (const auto& e : ev) {
        CHECK_FALSE(e.lost);
        if (!e.found)
            continue;
        ++found;
        sync_bursts += e.burst.type == rt::BurstType::Sync ? 1 : 0;
        // The burst's bits are the transmitted ones (hard decisions; Eb/N0
        // 14 dB leaves a few errors at most per burst). Symbol k of the
        // train is centred at (k + 8) symbol periods (generator).
        const long k = std::lround(e.burst.sample_pos / (rt::kSamplesPerSymbol * t.rate_scale) - 8.0);
        REQUIRE(k >= 0);
        REQUIRE(k % rt::kSlotSymbols == 0);
        const auto first = static_cast<std::size_t>(2 * k);
        if (first + rt::kSlotBits > t.bits.size())
            continue;
        int errors = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(rt::kSlotBits); ++i)
            errors += (e.burst.soft[i] < 0 ? 1 : 0) != t.bits[first + i];
        CHECK(errors <= 10);
    }
    CHECK(found == static_cast<int>(ev.size()));
    CHECK(sync_bursts >= 4);
}
