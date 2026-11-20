// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Complex FFT of any length (RAD-100) on pocketfft (third_party/pocketfft,
// RAD-04). The plan is made once per object; transforms are in place
// and unscaled: forward X[k] = sum_n x[n] e^{-2 pi i k n / N}, inverse
// with e^{+...}, so inverse(forward(x)) = N x. const methods may run from
// several threads at once.
#pragma once

#include <complex>
#include <cstddef>
#include <memory>

namespace yantra::receiver {

class Fft {
public:
    /// std::invalid_argument for size 0.
    explicit Fft(std::size_t size);
    ~Fft();
    Fft(Fft&&) noexcept;
    Fft& operator=(Fft&&) noexcept;

    std::size_t size() const noexcept { return size_; }
    void forward(std::complex<float>* data) const;
    void inverse(std::complex<float>* data) const;

private:
    struct Plan;
    std::size_t size_;
    std::unique_ptr<Plan> plan_;
};

}  // namespace yantra::receiver
