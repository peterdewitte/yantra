// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// pi/4-DQPSK demodulator (RAD-100) on TETRA generator output (RAD-70):
// exact bits and reference-decoder CRCs at high SNR, BER vs Eb/N0 against
// the theory of differential detection, carrier-offset pull-in with and
// without the coarse step, sample-clock drift, MER and lock, scalar vs
// SIMD, input splitting and reset.

#include "ref_decoder.hpp"
#include "signal.hpp"

#include "yantra/gen/burst.hpp"
#include "yantra/gen/coding.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

using namespace dsptest;
using namespace yantra;
namespace gen = yantra::gen;

namespace {

receiver::Pi4DqpskConfig cfg(receiver::Isa isa = receiver::Isa::Simd) { return receiver::tetra::demod_config(isa); }

// Theoretical bit error rate of Gray-coded pi/4-DQPSK with differential
// detection in AWGN: Pb = Q1(a, b) - I0(ab)/2 exp(-(a^2 + b^2)/2), a, b =
// sqrt(2 Eb/N0 (1 -/+ 1/sqrt 2)) (Marcum Q by numerical integration).
double dqpsk_ber(double ebn0_db)
{
    const double g = std::pow(10.0, ebn0_db / 10.0);
    const double a = std::sqrt(2.0 * g * (1.0 - std::sqrt(0.5)));
    const double b = std::sqrt(2.0 * g * (1.0 + std::sqrt(0.5)));
    double q = 0.0;
    const double dx = 1e-4;
    for (double x = b; x < b + 40.0; x += dx) {
        const double xm = x + 0.5 * dx;
        q += xm * std::exp(-(xm * xm + a * a) / 2.0) * std::cyl_bessel_i(0.0, a * xm) * dx;
    }
    return q - 0.5 * std::cyl_bessel_i(0.0, a * b) * std::exp(-(a * a + b * b) / 2.0);
}

gen::Bits slice(const gen::Bits& b, std::size_t first, std::size_t n)
{
    return gen::Bits(b.begin() + static_cast<long>(first), b.begin() + static_cast<long>(first + n));
}

// Hard bits of the demodulated symbols placed at their transmitted symbol
// numbers (unknown positions stay 2).
gen::Bits placed_bits(const std::vector<receiver::DemodSymbol>& sym, std::size_t total_bits, double rate_scale = 1.0)
{
    gen::Bits out(total_bits, 2);
    for (const auto& s : sym) {
        const long k = symbol_number(s, 4.0, rate_scale);
        if (k < 0 || static_cast<std::size_t>(2 * k + 1) >= total_bits)
            continue;
        out[static_cast<std::size_t>(2 * k)] = static_cast<uint8_t>((s.dibit() >> 1) & 1);
        out[static_cast<std::size_t>(2 * k + 1)] = static_cast<uint8_t>(s.dibit() & 1);
    }
    return out;
}

}  // namespace

TEST_CASE("Generator bursts demodulate to the exact bits; the reference decoder decodes SYNC and AACH")
{
    SignalSpec spec;
    spec.bursts = 80;  // frames 16..18 and on: several sync bursts
    const TestSignal t = make_signal(spec);
    receiver::Pi4DqpskDemodulator d(cfg());
    const auto sym = demodulate(d, t.iq);
    const Comparison c = compare(sym, t, 4.0, 300);
    CHECK(c.misaligned == 0);
    CHECK(c.errors == 0);
    CHECK(c.compared > 19000);
    CHECK(d.status().locked);
    CHECK(d.status().mer_db > 35.0);
    // Soft values: about +/-0.71 without noise.
    double mag = 0.0;
    for (std::size_t i = 300; i < sym.size(); ++i)
        mag += 0.5 * (std::fabs(sym[i].soft[0]) + std::fabs(sym[i].soft[1]));
    CHECK(mag / static_cast<double>(sym.size() - 300) == doctest::Approx(std::sqrt(0.5)).epsilon(0.02));

    // Decode every burst after acquisition with the reference decoder.
    const gen::Bits rx = placed_bits(sym, t.bits.size());
    const auto cell = gen::CellConfig::defaults();
    const uint32_t scr = gen::scrambling_init(cell.sync.mle.mcc, cell.sync.mle.mnc, cell.sync.colour_code);
    int sync_bursts = 0;
    int aach = 0;
    for (std::size_t b = 2; b < static_cast<std::size_t>(spec.bursts); ++b) {
        const std::size_t o = b * gen::kSlotBits;
        const gen::Bits burst = slice(rx, o, gen::kSlotBits);
        REQUIRE(burst == slice(t.bits, o, gen::kSlotBits));
        const bool sync = slice(burst, 214, 38) == gen::sync_training_sequence();
        if (sync) {
            const auto blk = gen::ref::decode_block(slice(burst, 94, gen::kSb1Bits), gen::kBsch, gen::kScramblingInitBsch);
            CHECK(blk.crc_ok);
            CHECK(blk.corrected == 0);
            ++sync_bursts;
            int dist = -1;
            gen::ref::decode_aach(slice(burst, 252, gen::kAachBits), scr, &dist);
            CHECK(dist == 0);
            ++aach;
        }
    }
    CHECK(sync_bursts >= 4);
    CHECK(aach == sync_bursts);
}

TEST_CASE("BER vs Eb/N0 stays within 1 dB of the theory of differential detection")
{
    struct Point {
        double ebn0;
        int bursts;
    };
    for (const Point p : {Point{4.0, 160}, Point{6.0, 200}, Point{8.0, 400}, Point{10.0, 1200}}) {
        CAPTURE(p.ebn0);
        SignalSpec spec;
        spec.bursts = p.bursts;
        spec.ebn0_db = p.ebn0;
        spec.cfo_hz = 400.0;
        spec.seed = static_cast<uint64_t>(p.ebn0 * 10);
        const TestSignal t = make_signal(spec);
        receiver::Pi4DqpskDemodulator d(cfg());
        const auto sym = demodulate(d, t.iq, 4096);
        const Comparison c = compare(sym, t, 4.0, 600);
        CHECK(c.misaligned == 0);
        const double ber = static_cast<double>(c.errors) / static_cast<double>(c.compared);
        std::printf("Eb/N0 %4.1f dB: BER %.3e (%zu/%zu), theory %.3e, at -1 dB %.3e\n", p.ebn0, ber, c.errors,
                    c.compared, dqpsk_ber(p.ebn0), dqpsk_ber(p.ebn0 - 1.0));
        // At most the theoretical BER 1 dB lower (implementation loss), and
        // not better than theory 0.5 dB higher (sanity of the noise level).
        CHECK(ber <= dqpsk_ber(p.ebn0 - 1.0));
        CHECK(ber >= dqpsk_ber(p.ebn0 + 0.5));
        CHECK(c.errors >= 50);  // enough errors for the comparison to mean something
    }
}

TEST_CASE("Carrier offset: coarse estimate plus FLL up to +/-2.8 kHz, FLL alone pulls in +/-1 kHz")
{
    for (double cfo : {-2800.0, -1500.0, -700.0, 0.0, 300.0, 1000.0, 2000.0, 2800.0}) {
        CAPTURE(cfo);
        SignalSpec spec;
        spec.cfo_hz = cfo;
        spec.ebn0_db = 25.0;
        const TestSignal t = make_signal(spec);
        receiver::Pi4DqpskDemodulator d(cfg());
        const auto sym = demodulate(d, t.iq);
        const Comparison c = compare(sym, t, 4.0, 300);
        CHECK(c.errors == 0);
        CHECK(std::fabs(d.status().coarse_offset_hz - cfo) < 300.0);
        CHECK(std::fabs(d.status().frequency_offset_hz - cfo) < 5.0);
        CHECK(d.status().locked);
    }
    for (double cfo : {-1000.0, -400.0, 600.0, 1000.0}) {
        CAPTURE(cfo);
        SignalSpec spec;
        spec.cfo_hz = cfo;
        spec.ebn0_db = 25.0;
        const TestSignal t = make_signal(spec);
        auto c0 = cfg();
        c0.coarse_cfo = false;
        receiver::Pi4DqpskDemodulator d(c0);
        const auto sym = demodulate(d, t.iq);
        const Comparison c = compare(sym, t, 4.0, 400);
        CHECK(c.errors == 0);
        CHECK(d.status().coarse_offset_hz == 0.0);
        CHECK(std::fabs(d.status().frequency_offset_hz - cfo) < 5.0);
        CHECK(d.status().locked);
    }
}

TEST_CASE("Symbol timing follows a sample-clock error and reports it")
{
    for (double ppm : {-500.0, -100.0, 100.0, 500.0}) {
        CAPTURE(ppm);
        SignalSpec spec;
        spec.clock_ppm = ppm;
        spec.bursts = 160;  // 40800 symbols: 500 ppm drifts by 20 symbols
        spec.ebn0_db = 20.0;
        spec.cfo_hz = -800.0;
        const TestSignal t = make_signal(spec);
        receiver::Pi4DqpskDemodulator d(cfg());
        const auto sym = demodulate(d, t.iq, 4096);
        const Comparison c = compare(sym, t, 4.0, 400);
        CHECK(c.misaligned == 0);
        CHECK(c.errors == 0);
        CHECK(c.compared > 2 * 40000);
        CHECK(d.status().timing_rate_ppm == doctest::Approx(ppm).epsilon(0.1));
        CHECK(d.status().locked);
    }
}

TEST_CASE("MER follows Es/N0; noise alone never locks")
{
    for (double esn0 : {15.0, 20.0, 25.0}) {
        CAPTURE(esn0);
        SignalSpec spec;
        spec.ebn0_db = esn0 - 3.0103;
        spec.cfo_hz = 250.0;
        const TestSignal t = make_signal(spec);
        receiver::Pi4DqpskDemodulator d(cfg());
        demodulate(d, t.iq);
        CHECK(d.status().mer_db == doctest::Approx(esn0).epsilon(0.08));
        CHECK(d.status().locked == (esn0 >= 15.0));
    }
    std::vector<cf> noise(400000);
    gen::Awgn(1.0, 5).add(noise.data(), noise.size());
    receiver::Pi4DqpskDemodulator d(cfg());
    std::vector<receiver::DemodSymbol> out;
    for (std::size_t pos = 0; pos < noise.size(); pos += 1000) {
        d.process(noise.data() + pos, 1000, out);
        REQUIRE_FALSE(d.status().locked);
    }
    CHECK(d.status().mer_db < 6.0);
}

TEST_CASE("After a gap the demodulator loses lock and re-acquires a carrier with another offset")
{
    SignalSpec a;
    a.cfo_hz = 300.0;
    a.ebn0_db = 25.0;
    SignalSpec b = a;
    b.cfo_hz = -1700.0;
    b.seed = 9;
    b.start = {3, 5, 2, 0};
    const TestSignal ta = make_signal(a);
    const TestSignal tb = make_signal(b);
    std::vector<cf> gap(72000);  // 1 s of receiver noise only
    gen::Awgn(1e-3, 4).add(gap.data(), gap.size());
    receiver::Pi4DqpskDemodulator d(cfg());
    std::vector<receiver::DemodSymbol> sa;
    std::vector<receiver::DemodSymbol> sb;
    d.process(ta.iq.data(), ta.iq.size(), sa);
    CHECK(d.status().locked);
    d.process(gap.data(), gap.size(), sa);
    CHECK_FALSE(d.status().locked);
    d.process(tb.iq.data(), tb.iq.size(), sb);
    CHECK(d.status().locked);
    CHECK(std::fabs(d.status().frequency_offset_hz - b.cfo_hz) < 5.0);
    // Symbols of the second signal, timed from its own start.
    const double shift = static_cast<double>(ta.iq.size() + gap.size());
    for (auto& s : sb)
        s.sample_pos -= shift;
    const Comparison c = compare(sb, tb, 4.0, 1200);
    CHECK(c.errors == 0);
    CHECK(c.misaligned == 0);
    CHECK(c.compared > 2 * 8000);
}

TEST_CASE("Scalar and SIMD demodulation agree; input splitting and reset do not change the output")
{
    SignalSpec spec;
    spec.ebn0_db = 15.0;
    spec.cfo_hz = 700.0;
    spec.clock_ppm = 40.0;
    const TestSignal t = make_signal(spec);
    receiver::Pi4DqpskDemodulator simd(cfg(receiver::Isa::Simd));
    receiver::Pi4DqpskDemodulator scalar(cfg(receiver::Isa::Scalar));
    const auto a = demodulate(simd, t.iq, 1000);
    const auto b = demodulate(scalar, t.iq, 1000);
    REQUIRE(a.size() == b.size());
    double soft = 0.0;
    double pos = 0.0;
    std::size_t hard = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        soft = std::max({soft, std::fabs(static_cast<double>(a[i].soft[0] - b[i].soft[0])),
                         std::fabs(static_cast<double>(a[i].soft[1] - b[i].soft[1]))});
        pos = std::max(pos, std::fabs(a[i].sample_pos - b[i].sample_pos));
        hard += a[i].dibit() != b[i].dibit();
    }
    // Documented tolerance (docs/DSP.md): soft bits within 1e-3, symbol
    // times within 1e-3 samples; at this SNR no hard decision differs.
    CHECK(soft < 1e-3);
    CHECK(pos < 1e-3);
    CHECK(hard == 0);

    for (receiver::Isa isa : {receiver::Isa::Scalar, receiver::Isa::Simd}) {
        receiver::Pi4DqpskDemodulator one(cfg(isa));
        const auto whole = demodulate(one, t.iq, t.iq.size());
        receiver::Pi4DqpskDemodulator tiny(cfg(isa));
        const auto pieces = demodulate(tiny, t.iq, 7);
        REQUIRE(whole.size() == pieces.size());
        bool same = true;
        for (std::size_t i = 0; i < whole.size(); ++i)
            same = same && whole[i].z == pieces[i].z && whole[i].soft[0] == pieces[i].soft[0] &&
                   whole[i].sample_pos == pieces[i].sample_pos;
        CHECK(same);
        one.reset();
        const auto again = demodulate(one, t.iq, 333);
        REQUIRE(again.size() == whole.size());
        CHECK(again.back().z == whole.back().z);
        CHECK(one.status().symbols == whole.size());
    }
}

TEST_CASE("Demodulator refuses bad configurations")
{
    auto c = cfg();
    c.symbol_rate_hz = 0.0;
    CHECK_THROWS_AS(receiver::Pi4DqpskDemodulator{c}, std::invalid_argument);
    c = cfg();
    c.samples_per_symbol = 1;
    CHECK_THROWS_AS(receiver::Pi4DqpskDemodulator{c}, std::invalid_argument);
    c = cfg();
    c.rolloff = 0.0;
    CHECK_THROWS_AS(receiver::Pi4DqpskDemodulator{c}, std::invalid_argument);
    c = cfg();
    c.max_cfo_hz = 30000.0;
    CHECK_THROWS_AS(receiver::Pi4DqpskDemodulator{c}, std::invalid_argument);
    c = cfg();
    c.rrc_span_symbols = 7;
    CHECK_THROWS_AS(receiver::Pi4DqpskDemodulator{c}, std::invalid_argument);
}

TEST_CASE("A long stretch of zeros after a signal gives finite symbols (AGC floor)")
{
    // The block AGC decays towards zero while the input is off; without a
    // floor its 1/sqrt overflowed a float after ~30 s (NaN symbols).
    SignalSpec spec;
    spec.bursts = 40;
    const TestSignal t = make_signal(spec);
    receiver::Pi4DqpskDemodulator d(cfg());
    std::vector<receiver::DemodSymbol> sym;
    d.process(t.iq.data(), t.iq.size(), sym);
    std::vector<std::complex<float>> zeros(72000);
    for (int k = 0; k < 60; ++k)
        d.process(zeros.data(), zeros.size(), sym);
    bool finite = true;
    for (const auto& s : sym)
        finite = finite && std::isfinite(s.soft[0]) && std::isfinite(s.soft[1]) && std::isfinite(s.z.real());
    CHECK(finite);
    d.process(t.iq.data(), t.iq.size(), sym);
    CHECK(std::isfinite(d.status().mer_db));
}
