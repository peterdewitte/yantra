// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell transmitter of the TETRA generator (RAD-70, RAD-93): its samples
// demodulate (test-only demodulator, ref_demod.*) to the bursts of the
// schedule; level, frequency offset and timing offset of one carrier;
// several carriers in one span separated and demodulated again with their
// powers; power normalisation; refusals.
// Copied from yantra 68ea20a (tests/tetra_gen/test_cell_tx.cpp), adapted
// to CarrierSettings; offset, level and timing cases added.

#include "ref_demod.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/gen/cell_tx.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

using namespace yantra::gen;
using cf = std::complex<float>;

namespace {

NwrkBroadcast two_neighbours()
{
    NwrkBroadcast nb;
    nb.network_time = NetworkTime{};
    NeighbourCell n1;
    n1.cell_id = 1;
    n1.main_carrier = 3604;
    n1.location_area = 2;
    NeighbourCell n2;
    n2.cell_id = 2;
    n2.main_carrier = 3596;
    n2.location_area = 3;
    nb.neighbours = std::vector<NeighbourCell>{n1, n2};
    return nb;
}

CellConfig cell(uint8_t colour_code, uint16_t carrier)
{
    CellConfig c = CellConfig::defaults();
    c.sync.colour_code = colour_code;
    c.sysinfo.main_carrier = carrier;
    c.nwrk_broadcast = two_neighbours();
    return c;
}

Bits burst_bits(const CellConfig& cfg, int bursts)
{
    Bits bits;
    TdmaTime t = cfg.start;
    for (int i = 0; i < bursts; ++i, t.advance()) {
        const Bits b = downlink_burst(cfg, t);
        bits.insert(bits.end(), b.begin(), b.end());
    }
    return bits;
}

size_t samples_for(double fs, size_t symbols, double extra_symbols = 0.0)
{
    return static_cast<size_t>(
        std::ceil((static_cast<double>(symbols) + 2.0 * Pi4DqpskModulator::kHalfSpanSymbols + extra_symbols) * fs /
                  kSymbolRate));
}

double mean_power(const std::vector<std::complex<double>>& z)
{
    double p = 0.0;
    for (const auto& v : z)
        p += std::norm(v);
    return p / static_cast<double>(z.size());
}

}  // namespace

TEST_CASE("Bad cell configurations and settings are refused when the transmitter is made")
{
    CellConfig c = CellConfig::defaults();
    c.sync.mle.mcc = 1024;  // 10 bits
    CHECK_THROWS_AS(CellTransmitter(c, 72000.0), std::out_of_range);
    c = CellConfig::defaults();
    c.nwrk_every_frames = 0;
    c.nwrk_broadcast = NwrkBroadcast{};
    CHECK_THROWS_AS(CellTransmitter(c, 72000.0), std::invalid_argument);
    c = CellConfig::defaults();
    NwrkBroadcast big;
    big.neighbours = std::vector<NeighbourCell>(7);
    big.network_time = NetworkTime{};
    c.nwrk_broadcast = big;
    CHECK_THROWS_AS(CellTransmitter(c, 72000.0), std::length_error);
    c = CellConfig::defaults();
    c.start = {1, 19, 1, 0};
    CHECK_THROWS_AS(CellTransmitter(c, 72000.0), std::invalid_argument);
    CHECK_THROWS_AS(CellTransmitter(CellConfig::defaults(), 20000.0), std::invalid_argument);
    CHECK_THROWS_AS(CellTransmitter(CellConfig::defaults(), 72000.0, {.offset_hz = 25000.0}), std::invalid_argument);
    CHECK_THROWS_AS(CellTransmitter(CellConfig::defaults(), 72000.0, {.level_db = std::nan("")}),
                    std::invalid_argument);
    CHECK_THROWS_AS(CellTransmitter(CellConfig::defaults(), 72000.0, {.timing_offset_s = -1e-3}),
                    std::invalid_argument);
    CellTransmitter ok(CellConfig::defaults(), 72000.0, {.offset_hz = 20000.0, .level_db = -40.0});
    CHECK(ok.settings().level_db == -40.0);
}

TEST_CASE("Cell transmitter samples demodulate to its bursts; the TDMA time runs through frame 18")
{
    const double fs = 72000.0;
    CellConfig cfg = cell(1, 3600);
    cfg.start = {3, 17, 60, 65535};  // crosses frame 18 and the hyperframe wrap
    CellTransmitter tx(cfg, fs);
    const int bursts = 6;
    const Bits bits = burst_bits(cfg, bursts);
    const size_t symbols = bits.size() / 2;
    std::vector<cf> iq(samples_for(fs, symbols));
    // Odd read sizes: bursts are made as needed.
    for (size_t pos = 0; pos < iq.size();) {
        const size_t n = std::min<size_t>(iq.size() - pos, 777);
        tx.read(iq.data() + pos, n);
        pos += n;
    }
    CHECK(tx.samples() == iq.size());
    const auto z = ref::matched_symbols(iq, fs, Pi4DqpskModulator::kHalfSpanSymbols, 0, symbols);
    CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
    CHECK(mean_power(z) == doctest::Approx(1.0).epsilon(0.01));
    // Bursts were made one slot at a time from the start time.
    TdmaTime t = cfg.start;
    for (uint64_t i = 0; i < tx.bursts(); ++i)
        t.advance();
    CHECK(tx.time() == t);
    CHECK(tx.bursts() >= static_cast<uint64_t>(bursts));
}

TEST_CASE("One carrier with level, frequency offset and timing offset")
{
    const double fs = 96000.0;
    CellConfig cfg = cell(5, 3601);
    cfg.start = {1, 18, 3, 9};
    const CarrierSettings s{.offset_hz = 12500.0, .level_db = -17.0, .timing_offset_s = 2.5e-3};
    CellTransmitter tx(cfg, fs, s);
    const int bursts = 5;
    const Bits bits = burst_bits(cfg, bursts);
    const size_t symbols = bits.size() / 2;
    const double delay_symbols = s.timing_offset_s * kSymbolRate;  // 45 symbols
    std::vector<cf> iq(samples_for(fs, symbols, delay_symbols));
    tx.read(iq.data(), iq.size());
    const auto z = ref::matched_symbols(iq, fs, Pi4DqpskModulator::kHalfSpanSymbols + delay_symbols, 0, symbols,
                                        s.offset_hz);
    CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
    CHECK(10.0 * std::log10(mean_power(z)) == doctest::Approx(-17.0).epsilon(0.005));
    // Before the first symbol's pulse starts there is nothing.
    const size_t quiet = static_cast<size_t>(std::floor(s.timing_offset_s * fs));
    double before = 0.0;
    for (size_t i = 0; i < quiet; ++i)
        before = std::max(before, static_cast<double>(std::abs(iq[i])));
    CHECK(before < 1e-6);
    // At the nominal frequency the symbols do not decode.
    const auto wrong = ref::matched_symbols(iq, fs, Pi4DqpskModulator::kHalfSpanSymbols + delay_symbols, 0, symbols);
    CHECK(ref::differential_decode(wrong, {1.0, 0.0}) != bits);
}

TEST_CASE("Power normalisation: a carrier at level L has an average power of L over whole slots")
{
    for (double level : {0.0, -10.0, -33.0}) {
        for (double fs : {36000.0, 500000.0}) {
            CAPTURE(level);
            CAPTURE(fs);
            CellTransmitter tx(cell(1, 3600), fs, {.offset_hz = fs > 100000.0 ? -100000.0 : 0.0, .level_db = level});
            // Skip the ramp-up, then 36 slots (two frames 18 included).
            std::vector<cf> skip(samples_for(fs, 0));
            tx.read(skip.data(), skip.size());
            std::vector<cf> iq(static_cast<size_t>(36.0 * kSlotSeconds * fs));
            tx.read(iq.data(), iq.size());
            double p = 0.0;
            for (const cf& v : iq)
                p += std::norm(v);
            p /= static_cast<double>(iq.size());
            CHECK(10.0 * std::log10(p) == doctest::Approx(level).epsilon(0.002).scale(1.0));
        }
    }
}

TEST_CASE("Read sizes do not change the samples of a shifted carrier")
{
    const double fs = 250000.0;
    const CarrierSettings s{.offset_hz = -73100.0, .level_db = 3.0, .timing_offset_s = 1.234e-4};
    CellTransmitter a(cell(2, 3602), fs, s);
    CellTransmitter b(cell(2, 3602), fs, s);
    std::vector<cf> whole(40000);
    a.read(whole.data(), whole.size());
    std::vector<cf> parts(whole.size());
    const size_t chunks[] = {1, 255, 256, 257, 4097, 3};
    size_t pos = 0;
    for (size_t c = 0; pos < parts.size(); ++c) {
        const size_t n = std::min(parts.size() - pos, chunks[c % 6]);
        b.read(parts.data() + pos, n);
        pos += n;
    }
    double worst = 0.0;
    for (size_t i = 0; i < whole.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(whole[i] - parts[i])));
    CHECK(worst < 1e-5);
}

TEST_CASE("Several carriers in one span: each demodulates with its own power and timing")
{
    struct Spec {
        uint8_t cc;
        uint16_t carrier;
        double offset;
        double power_db;
        double delay_s;
    };
    for (double fs : {500000.0, 1000000.0}) {
        CAPTURE(fs);
        // Carriers within +/-200 kHz, 0..-30 dB, the weak ones next to
        // strong ones, with different timing offsets.
        const std::vector<Spec> specs = {
            {1, 3592, -200000.0, 0.0, 0.0},       {2, 3594, -150000.0, -30.0, 1.0e-3},
            {3, 3600, 0.0, -10.0, 0.37e-3},       {4, 3601, 25000.0, 0.0, 0.0},
            {5, 3608, 200000.0, -20.0, 2.0e-3},
        };
        MultiCarrierTransmitter span(fs);
        for (const Spec& s : specs) {
            CellConfig c = cell(s.cc, s.carrier);
            c.start = {1, 16, 1, 0};
            span.add(c, {.offset_hz = s.offset, .level_db = s.power_db, .timing_offset_s = s.delay_s});
        }
        REQUIRE(span.carriers() == specs.size());
        CHECK(span.carrier(1).settings().offset_hz == -150000.0);
        const int bursts = 4;
        const size_t symbols = static_cast<size_t>(bursts * kSlotSymbols);
        std::vector<cf> iq(samples_for(fs, symbols, 2.0e-3 * kSymbolRate));
        span.read(iq.data(), iq.size() / 2);
        span.read(iq.data() + iq.size() / 2, iq.size() - iq.size() / 2);
        CHECK(span.samples() == iq.size());

        for (size_t i = 0; i < specs.size(); ++i) {
            CAPTURE(i);
            CellConfig c = cell(specs[i].cc, specs[i].carrier);
            c.start = {1, 16, 1, 0};
            const Bits bits = burst_bits(c, bursts);
            const auto z = ref::matched_symbols(
                iq, fs, Pi4DqpskModulator::kHalfSpanSymbols + specs[i].delay_s * kSymbolRate, 0, symbols,
                specs[i].offset);
            CHECK(ref::differential_decode(z, {1.0, 0.0}) == bits);
            CHECK(std::fabs(10.0 * std::log10(mean_power(z)) - specs[i].power_db) < 0.1);
        }
        // An empty channel between carriers stays empty.
        const auto empty = ref::matched_symbols(iq, fs, Pi4DqpskModulator::kHalfSpanSymbols, 0, symbols, -100000.0);
        CHECK(10.0 * std::log10(mean_power(empty) + 1e-30) < -60.0);
    }
}

TEST_CASE("Carriers that do not fit the span are refused; carriers are added before reading")
{
    MultiCarrierTransmitter span(500000.0);
    CHECK_THROWS_AS(span.add(CellConfig::defaults(), {.offset_hz = 240000.0}), std::invalid_argument);
    CHECK_THROWS_AS(span.add(CellConfig::defaults(), {.offset_hz = -240000.0}), std::invalid_argument);
    CHECK_THROWS_AS(span.add(CellConfig::defaults(), {.level_db = std::nan("")}), std::invalid_argument);
    span.add(CellConfig::defaults(), {.offset_hz = 200000.0});
    std::vector<cf> iq(16);
    span.read(iq.data(), iq.size());
    CHECK_THROWS_AS(span.add(CellConfig::defaults(), {}), std::logic_error);
    MultiCarrierTransmitter wide(1000000.0);
    wide.add(CellConfig::defaults(), {.offset_hz = 450000.0});
    CHECK(wide.carriers() == 1);
    CHECK(wide.carrier(0).settings().offset_hz == 450000.0);
    CHECK_THROWS_AS(MultiCarrierTransmitter(24000.0), std::invalid_argument);
    // Without carriers the span is silent.
    MultiCarrierTransmitter none(500000.0);
    std::vector<cf> zero(8, cf(1.0f, 1.0f));
    none.read(zero.data(), zero.size());
    CHECK(std::all_of(zero.begin(), zero.end(), [](cf v) { return v == cf(0.0f, 0.0f); }));
}
