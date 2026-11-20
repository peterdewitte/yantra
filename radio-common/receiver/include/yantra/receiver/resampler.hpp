// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polyphase resampler for complex samples (RAD-100), decimating by any
// ratio r = input rate / output rate >= 1 with a Kaiser low-pass prototype.
// When r * L is a whole number for some L <= max_phases, it is exact: L
// phases, output k at input time k * r with no rate error. Otherwise it
// uses kFallbackPhases phases and the nearest phase to the exact time
// (time error <= 1 / (2 * kFallbackPhases) input samples, no rate error).
#pragma once

#include "yantra/receiver/kernels.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace yantra::receiver {

struct ResamplerSpec {
    double input_rate_hz = 0.0;
    double output_rate_hz = 0.0;
    /// Flat up to this frequency; at least atten_db down from stopband_hz.
    double passband_hz = 0.0;
    double stopband_hz = 0.0;
    double atten_db = 70.0;
    std::size_t max_phases = 512;
    Isa isa = Isa::Simd;
};

class Resampler {
public:
    static constexpr std::size_t kFallbackPhases = 256;

    /// std::invalid_argument unless 0 < passband < stopband, the stopband
    /// is below input_rate/2 and output_rate <= input_rate.
    explicit Resampler(const ResamplerSpec& spec);

    const ResamplerSpec& spec() const noexcept { return spec_; }
    double ratio() const noexcept { return ratio_; }
    bool exact() const noexcept { return exact_; }
    std::size_t phases() const noexcept { return phases_; }
    std::size_t taps_per_phase() const noexcept { return taps_; }
    /// Output k is the filtered signal at input time k * ratio() - delay()
    /// (in input samples; input 0 is the first sample after construction or
    /// reset()).
    double delay() const noexcept { return delay_; }
    /// Appends the outputs due for `n` more inputs.
    void process(const cf* in, std::size_t n, std::vector<cf>& out);
    void reset();

private:
    ResamplerSpec spec_;
    double ratio_;
    bool exact_ = false;
    std::size_t phases_ = 1;
    std::size_t taps_ = 1;
    int64_t step_units_ = 0;  // exact: output step in 1/phases input samples
    double delay_ = 0.0;
    const Kernels* k_;
    std::vector<float> bank_;  // phases_ x (2 * taps_), reversed, interleaved
    std::vector<cf> hist_;     // taps-1 history, then new inputs
    // Time of the next output relative to the newest history sample's
    // successor (= input 0 of the next call), exact in 1/phases units or
    // as a double.
    int64_t next_units_ = 0;
    double next_time_ = 0.0;
};

}  // namespace yantra::receiver
