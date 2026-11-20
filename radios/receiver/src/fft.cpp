// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/fft.hpp"

// GCC's flow analysis reports a false -Wnull-dereference inside pocketfft
// (scratch buffer of a generic radix pass, allocated with the plan) in
// some inlining decisions; the vendored header is not modified.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
#include <pocketfft_hdronly.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <stdexcept>

namespace yantra::receiver {

struct Fft::Plan {
    explicit Plan(std::size_t n) : plan(n) {}
    pocketfft::detail::pocketfft_c<float> plan;
};

Fft::Fft(std::size_t size) : size_(size)
{
    if (size == 0)
        throw std::invalid_argument("FFT size must be at least 1");
    plan_ = std::make_unique<Plan>(size);
}

Fft::~Fft() = default;
Fft::Fft(Fft&&) noexcept = default;
Fft& Fft::operator=(Fft&&) noexcept = default;

// pocketfft's cmplx<float> is two floats like std::complex<float> (its own
// c2c() casts the same way).
void Fft::forward(std::complex<float>* data) const
{
    plan_->plan.exec(reinterpret_cast<pocketfft::detail::cmplx<float>*>(data), 1.0f, true);
}

void Fft::inverse(std::complex<float>* data) const
{
    plan_->plan.exec(reinterpret_cast<pocketfft::detail::cmplx<float>*>(data), 1.0f, false);
}

}  // namespace yantra::receiver
