// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/nco.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace yantra::receiver {
namespace {

cf unit(double cycles)
{
    const double a = 2.0 * std::numbers::pi * cycles;
    return {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
}

double wrap(double c) { return c - std::floor(c); }

}  // namespace

Nco::Nco(double frequency_hz, double sample_rate_hz, Isa isa) : rate_(sample_rate_hz), step_(0.0), k_(&kernels(isa))
{
    if (!(sample_rate_hz > 0.0))
        throw std::invalid_argument("Nco: sample rate must be positive");
    set_frequency(frequency_hz);
    reset();
}

void Nco::set_frequency(double frequency_hz) noexcept
{
    step_ = frequency_hz / rate_;
    step_phasor_ = unit(step_);
    phasor_ = unit(cycles_);
}

void Nco::reset(double phase_cycles) noexcept
{
    cycles_ = wrap(phase_cycles);
    in_block_ = 0;
    phasor_ = unit(cycles_);
}

void Nco::mix(const cf* in, cf* out, std::size_t n)
{
    std::size_t done = 0;
    while (done < n) {
        if (in_block_ == kBlock) {
            phasor_ = unit(cycles_);
            in_block_ = 0;
        }
        const std::size_t m = std::min(n - done, kBlock - in_block_);
        phasor_ = k_->rotate(in + done, out + done, m, phasor_, step_phasor_);
        cycles_ = wrap(cycles_ + step_ * static_cast<double>(m));
        in_block_ += m;
        done += m;
    }
}

}  // namespace yantra::receiver
