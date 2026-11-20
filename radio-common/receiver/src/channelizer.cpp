// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/channelizer.hpp"

#include <cmath>
#include <stdexcept>

namespace yantra::receiver {
namespace {

// Stage A decimation: largest D with input / D >= 2 * output (so stage B
// keeps a ratio of at least 2); among D .. D/2, prefer one that makes stage
// B exact with few phases.
std::size_t choose_decimation(double in, double out)
{
    const auto d_max = static_cast<std::size_t>(std::floor(in / (2.0 * out)));
    if (d_max < 2)
        return 1;
    for (std::size_t d = d_max; d >= std::max<std::size_t>(2, d_max / 2); --d) {
        const double r = in / static_cast<double>(d) / out;
        for (std::size_t l = 1; l <= 64; ++l) {
            const double u = r * static_cast<double>(l);
            if (std::fabs(u - std::round(u)) <= 1e-9 * u)
                return d;
        }
    }
    return d_max;
}

}  // namespace

Channelizer::Channelizer(const ChannelizerConfig& c, const std::vector<double>& offsets_hz) : config_(c)
{
    if (!(c.input_rate_hz > 0.0) || !(c.output_rate_hz > 0.0) || c.output_rate_hz > c.input_rate_hz)
        throw std::invalid_argument("Channelizer: needs 0 < output rate <= input rate");
    if (!(c.passband_hz > 0.0) || !(c.stopband_hz > c.passband_hz) ||
        c.stopband_hz > c.output_rate_hz - c.passband_hz)
        throw std::invalid_argument("Channelizer: needs 0 < passband < stopband <= output rate - passband");
    for (double f : offsets_hz)
        if (!std::isfinite(f) || std::fabs(f) + c.passband_hz > c.input_rate_hz / 2.0)
            throw std::invalid_argument("Channelizer: channel passband outside the capture");

    decimation_ = choose_decimation(c.input_rate_hz, c.output_rate_hz);
    const double rate_a = c.input_rate_hz / static_cast<double>(decimation_);
    std::vector<float> taps_a;
    double delay_in = 0.0;  // in input samples
    if (decimation_ > 1) {
        // Keep the passband flat and stop what folds into stage B's band:
        // after decimation, rate_a - stopband aliases to stopband.
        const double stop_a = rate_a - c.stopband_hz;
        taps_a = kaiser_lowpass(0.5 * (c.passband_hz + stop_a), stop_a - c.passband_hz, c.atten_db, c.input_rate_hz);
        delay_in = 0.5 * static_cast<double>(taps_a.size() - 1);
    }
    ResamplerSpec spec_b;
    spec_b.input_rate_hz = rate_a;
    spec_b.output_rate_hz = c.output_rate_hz;
    spec_b.passband_hz = c.passband_hz;
    spec_b.stopband_hz = std::min(c.stopband_hz, rate_a / 2.0);
    spec_b.atten_db = c.atten_db;
    spec_b.isa = c.isa;
    for (double f : offsets_hz) {
        Channel ch{f, Nco(-f, c.input_rate_hz, c.isa), std::nullopt, Resampler(spec_b)};
        if (decimation_ > 1)
            ch.a.emplace(taps_a, decimation_, c.isa);
        channels_.push_back(std::move(ch));
    }
    if (channels_.empty()) {
        Resampler probe(spec_b);
        delay_s_ = delay_in / c.input_rate_hz + probe.delay() / rate_a;
    } else {
        delay_s_ = delay_in / c.input_rate_hz + channels_[0].b.delay() / rate_a;
    }
}

void Channelizer::process(const cf* in, std::size_t n, std::vector<std::vector<cf>>& out)
{
    out.resize(channels_.size());
    mixed_.resize(n);
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        Channel& ch = channels_[i];
        ch.nco.mix(in, mixed_.data(), n);
        if (ch.a) {
            stage_a_.clear();
            ch.a->process(mixed_.data(), n, stage_a_);
            ch.b.process(stage_a_.data(), stage_a_.size(), out[i]);
        } else {
            ch.b.process(mixed_.data(), n, out[i]);
        }
    }
}

void Channelizer::reset()
{
    for (Channel& ch : channels_) {
        ch.nco.reset();
        if (ch.a)
            ch.a->reset();
        ch.b.reset();
    }
}

std::vector<double> channel_offsets(const std::vector<double>& centres_hz, double capture_centre_hz)
{
    std::vector<double> out;
    out.reserve(centres_hz.size());
    for (double f : centres_hz)
        out.push_back(f - capture_centre_hz);
    return out;
}

}  // namespace yantra::receiver
