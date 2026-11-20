// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numerically controlled oscillator (RAD-100): mixes samples by
// e^{2 pi i (f n / fs + phase0)}. The phase is kept in double precision
// and the phasor restarts from it every kBlock samples counted from the
// first sample, so the phase error stays below 2e-5 rad (float step
// rounding over one block) and does not grow over long runs.
#pragma once

#include "yantra/receiver/kernels.hpp"

#include <cstddef>
#include <cstdint>

namespace yantra::receiver {

class Nco {
public:
    static constexpr std::size_t kBlock = 256;

    Nco(double frequency_hz, double sample_rate_hz, Isa isa = Isa::Simd);

    double frequency_hz() const noexcept { return step_ * rate_; }
    double sample_rate_hz() const noexcept { return rate_; }
    /// Takes effect from the next sample; the phase stays continuous.
    void set_frequency(double frequency_hz) noexcept;
    /// Phase of the next sample in cycles, 0..1.
    double phase_cycles() const noexcept { return cycles_; }
    /// out[k] = in[k] * e^{2 pi i phase(k)}; in == out is allowed.
    void mix(const cf* in, cf* out, std::size_t n);
    /// Phase back to `phase_cycles`, block counter to 0.
    void reset(double phase_cycles = 0.0) noexcept;

private:
    double rate_;
    double step_;        // cycles per sample
    double cycles_ = 0;  // phase of the next sample
    std::size_t in_block_ = 0;  // samples since the last phasor restart
    cf phasor_{1.0f, 0.0f};
    cf step_phasor_{1.0f, 0.0f};
    const Kernels* k_;
};

}  // namespace yantra::receiver
