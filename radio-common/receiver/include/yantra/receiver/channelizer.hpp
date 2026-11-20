// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Channelizer (RAD-100): N narrow channels at any list of centre offsets
// from one wideband capture. Each channel is an NCO (shift to 0 Hz), an
// integer decimating FIR (stage A, only when the input is at least four
// times the output rate) and a polyphase resampler (stage B) to the output
// rate, which callers choose as a multiple of their symbol rate. Stage A
// only removes what would alias into stage B's band, so it is short;
// stage B gives the channel selectivity. The cost per channel grows with
// the input rate (the NCO) plus a small, rate-independent part; docs/RECEIVER_DSP.md
// compares it with a polyphase FFT filter bank and has measured numbers.
//
// Technology-free: passband, stopband and output rate are parameters
// (TETRA's set is in tetra_params.hpp).
#pragma once

#include "yantra/receiver/fir.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/receiver/nco.hpp"
#include "yantra/receiver/resampler.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace yantra::receiver {

struct ChannelizerConfig {
    double input_rate_hz = 0.0;
    double output_rate_hz = 0.0;
    /// Flat (< 0.05 dB ripple) up to passband_hz from the channel centre,
    /// at least atten_db down from stopband_hz on.
    double passband_hz = 0.0;
    double stopband_hz = 0.0;
    double atten_db = 70.0;
    Isa isa = Isa::Simd;
};

class Channelizer {
public:
    /// std::invalid_argument for rates/bands that do not fit (output rate
    /// above the input rate, stopband above output_rate - passband, so
    /// that aliases would land in the passband) or a channel whose passband
    /// is outside the capture.
    Channelizer(const ChannelizerConfig& config, const std::vector<double>& offsets_hz);

    const ChannelizerConfig& config() const noexcept { return config_; }
    std::size_t channels() const noexcept { return channels_.size(); }
    double offset_hz(std::size_t channel) const { return channels_.at(channel).offset_hz; }
    /// Stage A decimation (1 = no stage A).
    std::size_t decimation() const noexcept { return decimation_; }
    /// Stage B resampler properties (same for all channels).
    const Resampler& stage_b() const { return channels_.at(0).b; }
    /// Group delay: output sample k shows the input at time
    /// k / output_rate - delay_seconds() (input 0 at time 0).
    double delay_seconds() const noexcept { return delay_s_; }
    /// Appends each channel's new outputs to out[channel] (out is resized
    /// to channels()); all channels get the same number of outputs.
    void process(const cf* in, std::size_t n, std::vector<std::vector<cf>>& out);
    void reset();

private:
    struct Channel {
        double offset_hz;
        Nco nco;
        std::optional<FirFilter> a;
        Resampler b;
    };
    ChannelizerConfig config_;
    std::size_t decimation_ = 1;
    double delay_s_ = 0.0;
    std::vector<Channel> channels_;
    std::vector<cf> mixed_;
    std::vector<cf> stage_a_;
};

/// Offsets of absolute channel centres from a capture centre.
std::vector<double> channel_offsets(const std::vector<double>& centres_hz, double capture_centre_hz);

}  // namespace yantra::receiver
