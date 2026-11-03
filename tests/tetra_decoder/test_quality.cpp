// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quality windows of the carrier decoder (RAD-113) against the generator
// (fixed seeds; SNR = Es/N0 in the 18 kHz symbol-rate bandwidth):
// - message erasure rate, slot CRC error rate per logical channel and BER
//   bits: every report equals a recount of the decoder's timeslots;
// - modulation error ratio and M2M4 SNR within +/-1 dB of the set SNR;
//   EVM matches the modulation error ratio;
// - frequency error within +/-10 Hz of the set carrier offset, and ppm;
// - BER estimate: zero on a clean signal, near the true raw BER at 8 dB;
// - validity: no modulation figures, MER or BER without lock.
// Adapted from yantra 68ea20a: tests/tetra_quality/test_quality_truth.cpp
// (its sensitivity-tool runs replaced by the generator signals of
// signal.hpp).

#include "signal.hpp"

#include "yantra/receiver/quality.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <vector>

using namespace dectest;
using L = rx::LogicalChannel;

namespace {

/// Decodes a 4 s cell signal; quality windows of 0.5 s.
rx::DecoderOutput run(double snr_db, uint64_t seed, double cfo_hz = 0.0)
{
    CellSignal s;
    s.seconds = 4.0;
    s.snr_db = snr_db;
    s.seed = seed;
    s.cfo_hz = cfo_hz;
    rx::CarrierDecoderConfig cfg = test_config();
    cfg.quality_samples = 36000;
    return decode(make_cell(s), 4096, cfg);
}

/// The windows recounted from the decoder's timeslots, independently of
/// QualityMeter: slots count from the first good BSCH on; a slot with
/// `lost` empties the windows.
struct Recount {
    uint32_t erasure_slots = 0, aach_failures = 0, missed = 0, crc_slots = 0;
    uint32_t blocks[rx::kLogicalChannelCount] = {}, errors[rx::kLogicalChannelCount] = {};
    uint64_t bits = 0, bit_errors = 0;
};

Recount recount(const std::vector<rx::DecodedSlot>& slots, uint64_t upto)
{
    std::deque<const rx::DecodedSlot*> window;
    bool scrambling = false;
    for (uint64_t i = 0; i < upto && i < slots.size(); ++i) {
        const rx::DecodedSlot& s = slots[i];
        for (const rx::DecodedBlock& b : s.result.blocks)
            scrambling = scrambling || (s.found && b.channel == L::Bsch && b.crc_ok);
        if (s.lost) {
            window.clear();
            continue;
        }
        if (scrambling)
            window.push_back(&s);
    }
    Recount r;
    std::size_t k = 0;
    for (auto it = window.rbegin(); it != window.rend(); ++it, ++k) {
        const rx::DecodedSlot& s = **it;
        if (k < 100) {
            ++r.erasure_slots;
            r.missed += s.found ? 0 : 1;
            r.aach_failures += s.found && s.result.aach_present && !s.result.aach.decoded ? 1 : 0;
        }
        if (k >= 288 || !s.found)
            continue;
        ++r.crc_slots;
        if (s.result.aach_present) {
            ++r.blocks[static_cast<int>(L::Aach)];
            r.errors[static_cast<int>(L::Aach)] += s.result.aach.decoded ? 0 : 1;
        }
        for (const rx::DecodedBlock& b : s.result.blocks) {
            if (b.channel == L::Tch || b.channel == L::Unknown)
                continue;
            ++r.blocks[static_cast<int>(b.channel)];
            r.errors[static_cast<int>(b.channel)] += b.crc_ok ? 0 : 1;
            if (b.crc_ok) {
                r.bits += static_cast<uint64_t>(b.coded_bits);
                r.bit_errors += static_cast<uint64_t>(b.bit_errors);
            }
        }
    }
    return r;
}

/// Mean of a report field over the reports after the first second
/// (acquisition done).
template <typename F>
std::optional<double> mean_after_lock(const rx::DecoderOutput& out, F field)
{
    double sum = 0.0;
    int n = 0;
    for (const rx::QualityReport& r : out.quality) {
        if (r.sample_pos <= rx::kChannelRateHz)
            continue;
        if (const std::optional<double> v = field(r)) {
            sum += *v;
            ++n;
        }
    }
    if (n == 0)
        return std::nullopt;
    return sum / n;
}

}  // namespace

TEST_CASE("quality: message erasure rate, CRC per logical channel and BER windows equal a recount (RAD-113)")
{
    uint32_t reports_with_failures = 0;
    for (double snr : {5.0, 7.0, 20.0}) {
        CAPTURE(snr);
        const rx::DecoderOutput out = run(snr, 61);
        REQUIRE(out.quality.size() == 8);  // 0.5 s windows of the 4 s signal
        for (const rx::QualityReport& r : out.quality) {
            CAPTURE(r.sample_pos);
            CHECK(r.samples == 36000);
            const Recount c = recount(out.slots, r.slots_total);
            CHECK(r.erasure_slots == c.erasure_slots);
            CHECK(r.aach_failures == c.aach_failures);
            CHECK(r.missed_slots == c.missed);
            CHECK(r.crc_slots == c.crc_slots);
            for (int ch = 1; ch < rx::kLogicalChannelCount; ++ch) {
                CAPTURE(ch);
                CHECK(r.channels[static_cast<std::size_t>(ch)].blocks == c.blocks[ch]);
                CHECK(r.channels[static_cast<std::size_t>(ch)].crc_errors == c.errors[ch]);
            }
            CHECK(r.ber_bits == c.bits);
            CHECK(r.ber_errors == c.bit_errors);
            if (c.erasure_slots > 0) {
                const double mer = 100.0 * (c.aach_failures + c.missed) / c.erasure_slots;
                CHECK(r.message_erasure_rate_percent().value() == doctest::Approx(mer).epsilon(1e-12));
            } else {
                CHECK_FALSE(r.message_erasure_rate_percent().has_value());
            }
            CHECK(r.erasure_slots <= 100);
            CHECK(r.crc_slots <= 288);
            reports_with_failures += r.aach_failures > 0 ? 1 : 0;
        }
    }
    CHECK(reports_with_failures >= 2);  // the low-SNR runs exercise AACH failures
    // A clean signal: no erasures once locked, the broadcast channels seen.
    const rx::DecoderOutput clean = run(200.0, 1);
    const rx::QualityReport& last = clean.quality.back();
    REQUIRE(last.message_erasure_rate_percent().has_value());
    CHECK(*last.message_erasure_rate_percent() == 0.0);
    CHECK(last.erasure_slots == 100);
    for (L ch : {L::Bsch, L::Bnch, L::Aach}) {
        CAPTURE(static_cast<int>(ch));
        const rx::WindowCounts& w = last.channels[static_cast<std::size_t>(ch)];
        CHECK(w.blocks > 0);
        CHECK(w.error_percent().value() == 0.0);
    }
    CHECK(last.ber_percent().value() == 0.0);
    REQUIRE(last.time.has_value());
}

TEST_CASE("quality: modulation error ratio, EVM and SNR against the set SNR (RAD-113)")
{
    for (double snr : {6.0, 12.0, 18.0, 25.0}) {
        CAPTURE(snr);
        const rx::DecoderOutput out = run(snr, 23);
        const auto est_snr = mean_after_lock(out, [](const rx::QualityReport& r) { return r.snr_db; });
        REQUIRE(est_snr.has_value());
        CHECK(std::abs(*est_snr - snr) <= 1.0);
        if (snr < 10.0)
            continue;  // the demodulator locks from about 10 dB
        const auto mer = mean_after_lock(out, [](const rx::QualityReport& r) { return r.modulation_error_ratio_db; });
        REQUIRE(mer.has_value());
        CHECK(std::abs(*mer - snr) <= 1.0);
        for (const rx::QualityReport& r : out.quality) {
            if (!r.modulation_error_ratio_db)
                continue;
            CHECK(r.demod_locked);
            CHECK(*r.evm_percent ==
                  doctest::Approx(100.0 / std::sqrt(std::pow(10.0, *r.modulation_error_ratio_db / 10.0))));
        }
    }
}

TEST_CASE("quality: frequency error in Hz and ppm against the set carrier offset (RAD-113)")
{
    // Sign: positive when the received carrier is above the channel centre.
    for (double cfo : {-2400.0, -780.0, 0.0, 780.0, 2400.0}) {
        CAPTURE(cfo);
        const rx::DecoderOutput out = run(25.0, 5, cfo);
        int n = 0;
        for (const rx::QualityReport& r : out.quality) {
            if (r.sample_pos <= rx::kChannelRateHz)
                continue;
            REQUIRE(r.frequency_error_hz.has_value());
            CHECK(std::abs(*r.frequency_error_hz - cfo) <= 10.0);
            ++n;
        }
        CHECK(n >= 6);
    }
    CHECK(rx::frequency_error_ppm(780.0, 390e6) == doctest::Approx(2.0));
    CHECK(rx::frequency_error_ppm(-39.0, 390e6) == doctest::Approx(-0.1));
    CHECK(rx::frequency_error_ppm(10.0, 0.0) == 0.0);
}

TEST_CASE("quality: BER estimate near the raw BER of the channel (RAD-113)")
{
    // pi/4-DQPSK with differential detection at Es/N0 8 dB: a raw BER of a
    // few percent. The estimate counts only blocks whose CRC passed, so it
    // lies a little below the raw BER but well above zero.
    const rx::DecoderOutput out = run(8.0, 7);
    const auto ber = mean_after_lock(out, [](const rx::QualityReport& r) { return r.ber_percent(); });
    REQUIRE(ber.has_value());
    CHECK(*ber > 0.5);
    CHECK(*ber < 10.0);
}

TEST_CASE("quality: no modulation figures without lock (RAD-113)")
{
    const rx::DecoderOutput out = run(-5.0, 2);
    REQUIRE_FALSE(out.quality.empty());
    for (const rx::QualityReport& r : out.quality) {
        CHECK_FALSE(r.demod_locked);
        CHECK_FALSE(r.modulation_error_ratio_db.has_value());
        CHECK_FALSE(r.evm_percent.has_value());
        CHECK_FALSE(r.frequency_error_hz.has_value());
        CHECK_FALSE(r.time.has_value());
        CHECK_FALSE(r.message_erasure_rate_percent().has_value());
        CHECK_FALSE(r.ber_percent().has_value());
    }
}

TEST_CASE("quality: M2M4 SNR estimator")
{
    std::mt19937_64 rng(42);
    for (double snr_db : {0.0, 5.0, 10.0, 20.0}) {
        CAPTURE(snr_db);
        const double sigma = std::sqrt(0.5 / std::pow(10.0, snr_db / 10.0));
        std::normal_distribution<double> n(0.0, sigma);
        std::uniform_int_distribution<int> phase(0, 7);
        double m2 = 0.0;
        double m4 = 0.0;
        const int count = 200000;
        for (int k = 0; k < count; ++k) {
            const std::complex<double> z =
                std::polar(1.0, phase(rng) * M_PI / 4.0) + std::complex<double>(n(rng), n(rng));
            const double p = std::norm(z);
            m2 += p;
            m4 += p * p;
        }
        const auto est = rx::m2m4_snr_db(m2 / count, m4 / count);
        REQUIRE(est.has_value());
        CHECK(std::abs(*est - snr_db) <= 0.3);
    }
    CHECK(rx::m2m4_snr_db(1.0, 1.0).value() == 99.0);   // no noise
    CHECK_FALSE(rx::m2m4_snr_db(1.0, 2.0).has_value());  // Gaussian noise only
    CHECK_FALSE(rx::m2m4_snr_db(0.0, 0.0).has_value());
}
