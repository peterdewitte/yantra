// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "receive.hpp"

#include "yantra/gen/channel.hpp"
#include "yantra/gen/random.hpp"
#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/gen/modulator.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numbers>
#include <set>

namespace yantra::sens::detail {
namespace {

using receiver::cf;
constexpr double kSymbolRate = receiver::tetra::kSymbolRateHz;
constexpr double kChannelRate = receiver::tetra::kChannelRateHz;
constexpr double kGridHz = 6250.0;

}  // namespace

ChannelSignal receive(const Scenario& s, double snr_db, double seconds, uint64_t seed, const gen::CellConfig& cell,
                      receiver::Isa isa)
{
    const double fin = s.wideband ? kWidebandRateHz : kChannelRate;
    const double scale = 1.0 + s.clock_ppm * 1e-6;
    gen::CellTransmitter tx(cell, fin * scale);
    std::vector<cf> x(static_cast<std::size_t>(seconds * fin * scale));
    tx.read(x.data(), x.size());

    gen::MultipathChannel channel(gen::propagation(s.propagation), fin * scale, kCarrierHz, gen::derive_seed(seed, {1}));
    channel.apply(x.data(), x.size());
    PhaseNoise(s.phase_noise_hz, fin * scale, gen::derive_seed(seed, {2})).apply(x.data(), x.size());
    const double carrier = s.offcentre_hz + s.cfo_ppm * 1e-6 * kCarrierHz;
    if (carrier != 0.0)
        gen::FrequencyShifter(carrier, fin).apply(x.data(), x.size());
    // Mean signal power 1; noise in 18 kHz = 1 / SNR, spread over fin.
    const double noise = fin / kSymbolRate / std::pow(10.0, snr_db / 10.0);
    gen::Awgn(noise, gen::derive_seed(seed, {3})).add(x.data(), x.size());

    ChannelSignal out;
    out.carrier_offset_hz = carrier;
    // Symbol k is centred at symbol_time(k) of the transmitter's clock,
    // fin * scale samples per second, plus the channel's bulk delay.
    const double first_input = gen::Pi4DqpskModulator::symbol_time(0) * fin * scale + channel.bulk_delay();
    const double input_step = fin * scale / kSymbolRate;
    if (!s.wideband) {
        out.iq = std::move(x);
        out.origin = first_input;
        out.step = input_step;
        return out;
    }
    // The carrier search's choice: the nearest point of the 6.25 kHz grid.
    const double tuned = std::round(carrier / kGridHz) * kGridHz;
    receiver::Channelizer ch(receiver::tetra::channelizer_config(fin, isa), {tuned});
    std::vector<std::vector<cf>> y;
    for (std::size_t i = 0; i < x.size(); i += 8192)
        ch.process(x.data() + i, std::min<std::size_t>(8192, x.size() - i), y);
    out.iq = std::move(y.at(0));
    out.carrier_offset_hz = carrier - tuned;
    // Output sample j shows the input at j / 72 kHz - delay.
    const double to_out = kChannelRate / fin;
    out.origin = (first_input / fin + ch.delay_seconds()) * kChannelRate;
    out.step = input_step * to_out;
    return out;
}


PhaseNoise::PhaseNoise(double linewidth_hz, double rate_hz, uint64_t seed)
    : sigma_(std::sqrt(2.0 * std::numbers::pi * std::max(0.0, linewidth_hz) / rate_hz)), rng_(seed)
{
    if (!(rate_hz > 0.0))
        throw std::invalid_argument("PhaseNoise: rate must be > 0");
}

void PhaseNoise::apply(cf* x, std::size_t n)
{
    if (sigma_ == 0.0)
        return;
    for (std::size_t i = 0; i < n; ++i) {
        phase_ = std::remainder(phase_ + sigma_ * rng_.gaussian(), 2.0 * std::numbers::pi);
        x[i] *= cf(static_cast<float>(std::cos(phase_)), static_cast<float>(std::sin(phase_)));
    }
}

RawBits raw_bit_errors(const ChannelSignal& sig, const gen::Bits& bits, long k_begin, long k_end, receiver::Isa isa)
{
    RawBits r;
    receiver::Pi4DqpskDemodulator demod(receiver::tetra::demod_config(isa));
    std::vector<receiver::DemodSymbol> sym;
    for (std::size_t i = 0; i < sig.iq.size(); i += 4096)
        demod.process(sig.iq.data() + i, std::min<std::size_t>(4096, sig.iq.size() - i), sym);
    std::set<long> seen;
    for (const receiver::DemodSymbol& d : sym) {
        const long k = std::lround((d.sample_pos - sig.origin) / sig.step);
        if (k < k_begin || k >= k_end || !seen.insert(k).second)
            continue;
        const uint8_t dib = d.dibit();
        r.errors += ((dib >> 1) & 1) != bits[static_cast<std::size_t>(2 * k)];
        r.errors += (dib & 1) != bits[static_cast<std::size_t>(2 * k + 1)];
        r.bits += 2;
    }
    // Symbols the demodulator skipped (timing slips) count as wrong.
    const auto missing = static_cast<uint64_t>(k_end - k_begin) - seen.size();
    r.bits += 2 * missing;
    r.errors += missing;  // half of the bits of a missing symbol are wrong on average
    return r;
}

}  // namespace yantra::sens::detail
