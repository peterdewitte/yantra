// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window functions for the FFT spectrum (RAD-100). Windows are periodic
// (DFT-even): w[n] for n = 0..N-1 of a period-N window.
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace yantra::receiver {

enum class Window {
    Rectangular,
    Hann,
    BlackmanHarris,  ///< 4-term, -92 dB side lobes (default for spectra)
    FlatTop,         ///< 5-term flat top: amplitude error < 0.02 dB anywhere in a bin
};

std::vector<float> make_window(Window window, std::size_t size);

/// Gains of a window: coherent gain sum(w)/N (a tone at a bin centre is
/// scaled by it) and the equivalent noise bandwidth in bins
/// N*sum(w^2)/sum(w)^2.
struct WindowGains {
    double coherent = 1.0;
    double enbw_bins = 1.0;
};
WindowGains window_gains(const std::vector<float>& w);

std::string_view window_name(Window window);

}  // namespace yantra::receiver
