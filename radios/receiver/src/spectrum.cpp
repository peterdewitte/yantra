// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yantra::receiver {

double SpectrumFrame::offset_hz(std::size_t bin) const noexcept
{
    const auto n = static_cast<double>(dbfs.size());
    return (static_cast<double>(bin) - std::floor(n / 2.0)) * bin_hz();
}

std::size_t SpectrumFrame::bin_of(double offset) const noexcept
{
    if (dbfs.empty() || bin_hz() <= 0.0)
        return 0;
    const double half = std::floor(static_cast<double>(dbfs.size()) / 2.0);
    const double b = std::round(offset / bin_hz() + half);
    return static_cast<std::size_t>(std::clamp(b, 0.0, static_cast<double>(dbfs.size() - 1)));
}

double SpectrumFrame::band_power_dbfs(double low_offset_hz, double high_offset_hz) const
{
    double sum = 0.0;
    std::size_t bins = 0;
    for (std::size_t i = 0; i < dbfs.size(); ++i) {
        const double f = offset_hz(i);
        if (f < low_offset_hz || f > high_offset_hz)
            continue;
        sum += std::pow(10.0, dbfs[i] / 10.0);
        ++bins;
    }
    if (bins == 0 || sum <= 0.0)
        return -std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(sum / enbw_bins);
}

std::vector<Peak> find_peaks(const SpectrumFrame& frame, double threshold_dbfs, std::size_t max_peaks,
                             double min_separation_hz)
{
    std::vector<Peak> candidates;
    const auto& d = frame.dbfs;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (d[i] < threshold_dbfs)
            continue;
        const float left = i > 0 ? d[i - 1] : -std::numeric_limits<float>::infinity();
        const float right = i + 1 < d.size() ? d[i + 1] : -std::numeric_limits<float>::infinity();
        // A plateau counts once, at its first bin.
        if (!(d[i] > left && d[i] >= right))
            continue;
        double delta = 0.0;
        if (i > 0 && i + 1 < d.size()) {
            const double den = static_cast<double>(left) - 2.0 * d[i] + right;
            if (den < 0.0)
                delta = std::clamp(0.5 * (static_cast<double>(left) - right) / den, -0.5, 0.5);
        }
        candidates.push_back({i, frame.offset_hz(i) + delta * frame.bin_hz(), d[i]});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Peak& a, const Peak& b) { return a.dbfs > b.dbfs; });
    std::vector<Peak> peaks;
    for (const Peak& c : candidates) {
        if (peaks.size() >= max_peaks)
            break;
        const bool close = std::any_of(peaks.begin(), peaks.end(), [&](const Peak& p) {
            return std::fabs(p.offset_hz - c.offset_hz) < min_separation_hz;
        });
        if (!close)
            peaks.push_back(c);
    }
    return peaks;
}

SpectrumAnalyzer::SpectrumAnalyzer(SpectrumConfig config)
    : config_(config), k_(&kernels(config.isa)), fft_(std::max<std::size_t>(config.fft_size, 1))
{
    if (config_.fft_size < 2)
        throw std::invalid_argument("spectrum: fft_size must be at least 2");
    if (config_.averages == 0)
        throw std::invalid_argument("spectrum: averages must be at least 1");
    if (config_.hop == 0)
        config_.hop = config_.fft_size;
    if (config_.hop > config_.fft_size)
        throw std::invalid_argument("spectrum: hop must not exceed fft_size");
    window_ = make_window(config_.window, config_.fft_size);
    const WindowGains g = window_gains(window_);
    const double sum_w = g.coherent * static_cast<double>(config_.fft_size);
    norm_ = 1.0 / (sum_w * sum_w);
    enbw_ = g.enbw_bins;
    work_.resize(config_.fft_size);
    acc_.assign(config_.fft_size, 0.0f);
}

void SpectrumAnalyzer::set_stream(double centre_hz, double sample_rate_hz) noexcept
{
    centre_hz_ = centre_hz;
    rate_hz_ = sample_rate_hz;
}

std::size_t SpectrumAnalyzer::push(const cf* samples, std::size_t n)
{
    const std::size_t size = config_.fft_size;
    std::size_t used = 0;
    while (used < n) {
        const std::size_t take = std::min(n - used, size - std::min(size, buffer_.size()));
        buffer_.insert(buffer_.end(), samples + used, samples + used + take);
        used += take;
        if (buffer_.size() < size)
            break;
        // One frame: window, FFT, add |X|^2.
        k_->apply_window(buffer_.data(), window_.data(), work_.data(), size);
        fft_.forward(work_.data());
        if (acc_frames_ == 0)
            acc_first_ = buffer_start_;
        k_->accumulate_power(work_.data(), acc_.data(), size);
        ++acc_frames_;
        if (acc_frames_ == config_.averages)
            frame_done();
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(config_.hop));
        buffer_start_ += config_.hop;
    }
    return ready_.size();
}

void SpectrumAnalyzer::frame_done()
{
    const std::size_t size = config_.fft_size;
    SpectrumFrame f;
    f.centre_hz = centre_hz_;
    f.sample_rate_hz = rate_hz_;
    f.first_sample = acc_first_;
    f.frames_averaged = acc_frames_;
    f.enbw_bins = enbw_;
    f.dbfs.resize(size);
    const double scale = norm_ / static_cast<double>(acc_frames_);
    const std::size_t half = size / 2;
    for (std::size_t i = 0; i < size; ++i) {
        // Shift: output bin i holds FFT bin (i + size - half) mod size, so
        // output bin `half` is DC (also for odd sizes).
        const std::size_t src = (i + size - half) % size;
        const double p = static_cast<double>(acc_[src]) * scale;
        f.dbfs[i] = p > 0.0 ? static_cast<float>(10.0 * std::log10(p)) : -300.0f;
    }
    ready_.push_back(std::move(f));
    std::fill(acc_.begin(), acc_.end(), 0.0f);
    acc_frames_ = 0;
}

bool SpectrumAnalyzer::pop(SpectrumFrame& frame)
{
    if (ready_.empty())
        return false;
    frame = std::move(ready_.front());
    ready_.pop_front();
    return true;
}

void SpectrumAnalyzer::reset()
{
    buffer_start_ += buffer_.size();
    buffer_.clear();
    std::fill(acc_.begin(), acc_.end(), 0.0f);
    acc_frames_ = 0;
    ready_.clear();
}

}  // namespace yantra::receiver
