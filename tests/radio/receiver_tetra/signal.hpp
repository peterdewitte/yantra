// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test signals for the DSP tests: TETRA downlink bursts from the generator
// (RAD-70) with a carrier offset, a sample-clock error and noise, plus
// helpers to compare demodulated symbols with the transmitted bits.
#pragma once

#include "yantra/gen/channel.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace dsptest {

using yantra::receiver::cf;
using yantra::gen::Bits;

struct TestSignal {
    std::vector<cf> iq;
    Bits bits;  // transmitted, 2 per symbol
    double rate_scale = 1.0;  // true sample rate / nominal
};

struct SignalSpec {
    double fs = 72000.0;
    int bursts = 40;
    double cfo_hz = 0.0;
    double clock_ppm = 0.0;  // sample clock fast by this much
    double ebn0_db = 200.0;  // noise; 200 = none
    uint64_t seed = 1;
    yantra::gen::TdmaTime start{1, 16, 1, 0};
};

inline TestSignal make_signal(const SignalSpec& s)
{
    using namespace yantra::gen;
    CellConfig cfg = CellConfig::defaults();
    cfg.start = s.start;
    TestSignal t;
    t.rate_scale = 1.0 + s.clock_ppm * 1e-6;
    CellTransmitter tx(cfg, s.fs * t.rate_scale);
    CellTransmitter ref(cfg, s.fs);
    for (int i = 0; i < s.bursts; ++i) {
        const Bits b = ref.next_burst();
        t.bits.insert(t.bits.end(), b.begin(), b.end());
    }
    const auto symbols = static_cast<double>(t.bits.size() / 2);
    // 48 symbols more than the bits: the generator's delay (8) plus the
    // receiver's filter delays, so every transmitted symbol comes out.
    t.iq.resize(static_cast<std::size_t>((symbols + 48.0) * s.fs / kSymbolRate * t.rate_scale));
    tx.read(t.iq.data(), t.iq.size());
    if (s.cfo_hz != 0.0)
        yantra::gen::FrequencyShifter(s.cfo_hz, s.fs).apply(t.iq.data(), t.iq.size());
    if (s.ebn0_db < 150.0) {
        // Unit signal power: Eb/N0 = (fs / Rs) / (2 * noise power).
        const double sps = s.fs / kSymbolRate;
        const double noise = sps / (2.0 * std::pow(10.0, s.ebn0_db / 10.0));
        yantra::gen::Awgn(noise, s.seed).add(t.iq.data(), t.iq.size());
    }
    return t;
}

inline std::vector<yantra::receiver::DemodSymbol> demodulate(yantra::receiver::Pi4DqpskDemodulator& d, const std::vector<cf>& iq,
                                                        std::size_t piece = 1000)
{
    std::vector<yantra::receiver::DemodSymbol> out;
    for (std::size_t pos = 0; pos < iq.size();) {
        const std::size_t n = std::min(iq.size() - pos, piece);
        d.process(iq.data() + pos, n, out);
        pos += n;
    }
    return out;
}

// Transmitted symbol number of a demodulated symbol from its time: symbol
// k is centred at (k + 8) symbol periods (generator documentation).
inline long symbol_number(const yantra::receiver::DemodSymbol& s, double samples_per_symbol, double rate_scale)
{
    return std::lround(s.sample_pos / (samples_per_symbol * rate_scale) - 8.0);
}

struct Comparison {
    std::size_t compared = 0;  // bits
    std::size_t errors = 0;
    long first_symbol = -1;    // first transmitted symbol compared
    long misaligned = 0;       // symbols whose time maps before the burst train
};

// Compares the hard bits of symbols from `skip` on with the transmitted bits.
inline Comparison compare(const std::vector<yantra::receiver::DemodSymbol>& sym, const TestSignal& t, double sps,
                          std::size_t skip)
{
    Comparison c;
    for (std::size_t i = skip; i < sym.size(); ++i) {
        const long k = symbol_number(sym[i], sps, t.rate_scale);
        if (static_cast<std::size_t>(2 * k + 1) >= t.bits.size())
            continue;  // the generator's next bursts after the compared ones
        if (k < 1) {
            ++c.misaligned;
            continue;
        }
        if (c.first_symbol < 0)
            c.first_symbol = k;
        const uint8_t dib = sym[i].dibit();
        c.errors += ((dib >> 1) & 1) != t.bits[static_cast<std::size_t>(2 * k)];
        c.errors += (dib & 1) != t.bits[static_cast<std::size_t>(2 * k + 1)];
        c.compared += 2;
    }
    return c;
}

}  // namespace dsptest
