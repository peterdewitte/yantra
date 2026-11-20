// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// FFT spectrum (RAD-100): windowed FFT frames with optional overlap, linear
// power averaging over a number of frames, levels in dBFS per bin, band
// power and a peak list. Calibrated levels (dBm) come from the stream's
// level chain (yantra::stream::LevelChain::levels, HW-09).
//
// Level scale: a complex tone of amplitude A (|x| = A, A = 1 is full
// scale) centred on a bin reads 20 log10(A) dBFS in that bin, for every
// window (the coherent gain is divided out). Noise and wideband signals
// spread over bins; their power over a band is band_power_dbfs(), which
// divides the summed bins by the window's noise bandwidth (Parseval).
#pragma once

#include "yantra/receiver/fft.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/receiver/window.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace yantra::receiver {

struct SpectrumConfig {
    std::size_t fft_size = 4096;
    Window window = Window::BlackmanHarris;
    /// Frames averaged (linear power) into one output spectrum.
    std::size_t averages = 1;
    /// Samples between frame starts; 0 = fft_size (no overlap). Smaller
    /// values overlap frames (e.g. fft_size / 2).
    std::size_t hop = 0;
    Isa isa = Isa::Simd;
};

struct SpectrumFrame {
    double centre_hz = 0.0;
    double sample_rate_hz = 0.0;
    /// Index of the first input sample of the first averaged frame.
    uint64_t first_sample = 0;
    std::size_t frames_averaged = 0;
    /// Equivalent noise bandwidth of the window in bins.
    double enbw_bins = 1.0;
    /// Level per bin in dBFS, FFT-shifted: bin i is at offset
    /// (i - size/2) * bin_hz() from the centre (bin size/2 = DC).
    std::vector<float> dbfs;

    std::size_t size() const noexcept { return dbfs.size(); }
    double bin_hz() const noexcept { return dbfs.empty() ? 0.0 : sample_rate_hz / static_cast<double>(dbfs.size()); }
    double offset_hz(std::size_t bin) const noexcept;
    double frequency_hz(std::size_t bin) const noexcept { return centre_hz + offset_hz(bin); }
    /// Nearest bin to an offset from the centre, clamped to the spectrum.
    std::size_t bin_of(double offset_hz) const noexcept;
    /// Mean power (dBFS) of the signal between two offsets from the centre
    /// (bins whose centres lie inside, inclusive); -inf if none or silent.
    double band_power_dbfs(double low_offset_hz, double high_offset_hz) const;
};

struct Peak {
    std::size_t bin = 0;
    /// Offset from the centre, refined by parabolic interpolation on the
    /// dB values of the bin and its neighbours.
    double offset_hz = 0.0;
    /// Bin level (dBFS) at the peak bin.
    double dbfs = 0.0;
};

/// Local maxima at or above `threshold_dbfs`, strongest first, at most
/// `max_peaks`, no two closer than `min_separation_hz` (the weaker one is
/// dropped).
std::vector<Peak> find_peaks(const SpectrumFrame& frame, double threshold_dbfs, std::size_t max_peaks,
                             double min_separation_hz);

/// Streaming analyser: push samples, take finished spectra.
class SpectrumAnalyzer {
public:
    /// std::invalid_argument for fft_size < 2, averages 0 or hop > fft_size.
    explicit SpectrumAnalyzer(SpectrumConfig config);

    const SpectrumConfig& config() const noexcept { return config_; }
    /// Centre and rate written into the following frames; does not reset.
    void set_stream(double centre_hz, double sample_rate_hz) noexcept;
    /// Adds samples; returns the number of finished spectra waiting.
    std::size_t push(const cf* samples, std::size_t n);
    /// Takes the oldest finished spectrum; false if none.
    bool pop(SpectrumFrame& frame);
    /// Drops buffered samples, the partial average and waiting spectra
    /// (e.g. after a retune or a gap). The sample count goes on.
    void reset();

private:
    void frame_done();

    SpectrumConfig config_;
    const Kernels* k_;
    Fft fft_;
    std::vector<float> window_;
    double norm_;  // 1 / (sum w)^2
    double enbw_;
    double centre_hz_ = 0.0;
    double rate_hz_ = 0.0;
    std::vector<cf> buffer_;  // samples of the next frame(s)
    uint64_t buffer_start_ = 0;  // sample index of buffer_[0]
    std::vector<cf> work_;
    std::vector<float> acc_;
    std::size_t acc_frames_ = 0;
    uint64_t acc_first_ = 0;
    std::deque<SpectrumFrame> ready_;
};

}  // namespace yantra::receiver
