// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/resampler.hpp"

#include "yantra/receiver/fir.hpp"

#include <cmath>
#include <stdexcept>

namespace yantra::receiver {

Resampler::Resampler(const ResamplerSpec& spec) : spec_(spec), ratio_(0.0), k_(&kernels(spec.isa))
{
    if (!(spec.input_rate_hz > 0.0) || !(spec.output_rate_hz > 0.0) || spec.output_rate_hz > spec.input_rate_hz)
        throw std::invalid_argument("Resampler: needs 0 < output rate <= input rate");
    if (!(spec.passband_hz > 0.0) || !(spec.stopband_hz > spec.passband_hz) ||
        !(spec.stopband_hz <= spec.input_rate_hz / 2.0))
        throw std::invalid_argument("Resampler: needs 0 < passband < stopband <= input rate / 2");
    ratio_ = spec.input_rate_hz / spec.output_rate_hz;
    for (std::size_t l = 1; l <= std::max<std::size_t>(spec.max_phases, 1); ++l) {
        const double units = ratio_ * static_cast<double>(l);
        if (std::fabs(units - std::round(units)) <= 1e-9 * units) {
            exact_ = true;
            phases_ = l;
            step_units_ = static_cast<int64_t>(std::llround(units));
            break;
        }
    }
    if (!exact_)
        phases_ = kFallbackPhases;
    // Prototype at phases * input rate; gain `phases` so every phase has
    // DC gain 1.
    const double proto_rate = spec.input_rate_hz * static_cast<double>(phases_);
    const double cutoff = 0.5 * (spec.passband_hz + spec.stopband_hz);
    std::vector<float> proto =
        kaiser_lowpass(cutoff, spec.stopband_hz - spec.passband_hz, spec.atten_db, proto_rate, phases_);
    // The prototype is centred at (size - 1) / 2 of its own samples.
    delay_ = 0.5 * static_cast<double>(proto.size() - 1) / static_cast<double>(phases_);
    taps_ = (proto.size() + phases_ - 1) / phases_;
    proto.resize(taps_ * phases_, 0.0f);
    // Output time n0 + p/L uses taps h[j*L + p] on x[n0 - j].
    bank_.assign(phases_ * 2 * taps_, 0.0f);
    for (std::size_t p = 0; p < phases_; ++p) {
        float* dst = bank_.data() + p * 2 * taps_;
        for (std::size_t j = 0; j < taps_; ++j) {
            const float h = proto[j * phases_ + p] * static_cast<float>(phases_);
            const std::size_t r = taps_ - 1 - j;  // reversed: oldest sample first
            dst[2 * r] = dst[2 * r + 1] = h;
        }
    }
    reset();
}

void Resampler::reset()
{
    hist_.assign(taps_ - 1, cf{});
    next_units_ = 0;
    next_time_ = 0.0;
}

void Resampler::process(const cf* in, std::size_t n, std::vector<cf>& out)
{
    if (n == 0)
        return;
    hist_.insert(hist_.end(), in, in + n);
    // Input i of this call is hist_[taps - 1 + i]; the window of an output
    // at input n0 starts at hist_[n0].
    const auto L = static_cast<int64_t>(phases_);
    if (exact_) {
        for (;;) {
            const int64_t n0 = next_units_ / L;  // whole input samples (>= 0)
            const int64_t p = next_units_ % L;
            if (n0 >= static_cast<int64_t>(n))
                break;
            out.push_back(k_->dot(hist_.data() + n0, bank_.data() + static_cast<std::size_t>(p) * 2 * taps_, taps_));
            next_units_ += step_units_;
        }
        next_units_ -= static_cast<int64_t>(n) * L;
    } else {
        for (;;) {
            double whole = std::floor(next_time_);
            auto p = static_cast<int64_t>(std::llround((next_time_ - whole) * static_cast<double>(L)));
            if (p == L) {
                p = 0;
                whole += 1.0;
            }
            if (whole >= static_cast<double>(n))
                break;
            const auto n0 = static_cast<std::size_t>(whole);
            out.push_back(k_->dot(hist_.data() + n0, bank_.data() + static_cast<std::size_t>(p) * 2 * taps_, taps_));
            next_time_ += ratio_;
        }
        next_time_ -= static_cast<double>(n);
    }
    hist_.erase(hist_.begin(), hist_.begin() + static_cast<std::ptrdiff_t>(n));
}

}  // namespace yantra::receiver
