// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/window.hpp"

#include <cmath>
#include <numbers>

namespace yantra::receiver {
namespace {

std::vector<float> cosine_sum(std::size_t n, const double* a, int terms)
{
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double x = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
        double v = 0.0;
        double sign = 1.0;
        for (int k = 0; k < terms; ++k) {
            v += sign * a[k] * std::cos(k * x);
            sign = -sign;
        }
        w[i] = static_cast<float>(v);
    }
    return w;
}

}  // namespace

std::vector<float> make_window(Window window, std::size_t size)
{
    static constexpr double kHann[] = {0.5, 0.5};
    static constexpr double kBlackmanHarris[] = {0.35875, 0.48829, 0.14128, 0.01168};
    // The common 5-term flat-top coefficients (scalloping loss < 0.01 dB).
    static constexpr double kFlatTop[] = {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368};
    switch (window) {
    case Window::Rectangular:
        return std::vector<float>(size, 1.0f);
    case Window::Hann:
        return cosine_sum(size, kHann, 2);
    case Window::BlackmanHarris:
        return cosine_sum(size, kBlackmanHarris, 4);
    case Window::FlatTop:
        return cosine_sum(size, kFlatTop, 5);
    }
    return std::vector<float>(size, 1.0f);
}

WindowGains window_gains(const std::vector<float>& w)
{
    double s1 = 0.0;
    double s2 = 0.0;
    for (float v : w) {
        s1 += v;
        s2 += static_cast<double>(v) * v;
    }
    WindowGains g;
    if (w.empty() || s1 == 0.0)
        return g;
    const double n = static_cast<double>(w.size());
    g.coherent = s1 / n;
    g.enbw_bins = n * s2 / (s1 * s1);
    return g;
}

std::string_view window_name(Window window)
{
    switch (window) {
    case Window::Rectangular:
        return "rectangular";
    case Window::Hann:
        return "hann";
    case Window::BlackmanHarris:
        return "blackman-harris";
    case Window::FlatTop:
        return "flat-top";
    }
    return "unknown";
}

}  // namespace yantra::receiver
