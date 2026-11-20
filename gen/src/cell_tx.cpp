// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell transmitter of the TETRA test-signal generator (RAD-70). Copied from
// yantra 68ea20a (plugins/tetra/gen/src/cell_tx.cpp), with level,
// frequency and timing offsets per carrier.

#include "yantra/gen/cell_tx.hpp"

#include "yantra/gen/burst.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace yantra::gen {

namespace {

// Samples between two exact NCO phasors; in between the phasor is turned
// step by step (double precision, so the error stays far below float).
constexpr size_t kNcoBlock = 256;

std::complex<double> phasor(double cycles)
{
    cycles -= std::floor(cycles);
    const double phi = 2.0 * std::numbers::pi * cycles;
    return {std::cos(phi), std::sin(phi)};
}

void check_settings(const CarrierSettings& s, double sample_rate_hz)
{
    if (!std::isfinite(s.offset_hz) || !std::isfinite(s.level_db) || !std::isfinite(s.timing_offset_s))
        throw std::invalid_argument("carrier settings must be finite");
    if (!(std::fabs(s.offset_hz) + kHalfOccupiedBandwidthHz < sample_rate_hz / 2.0)) {
        throw std::invalid_argument("carrier at " + std::to_string(s.offset_hz) + " Hz does not fit a " +
                                    std::to_string(sample_rate_hz) + " sps span");
    }
}

}  // namespace

CellTransmitter::CellTransmitter(CellConfig cfg, double sample_rate_hz, CarrierSettings settings)
    : cfg_(std::move(cfg)),
      settings_(settings),
      time_(cfg_.start),
      modulator_(sample_rate_hz, settings.timing_offset_s),
      gain_(static_cast<float>(std::pow(10.0, settings.level_db / 20.0))),
      cycles_per_sample_(settings.offset_hz / sample_rate_hz)
{
    check_settings(settings_, sample_rate_hz);
    if (!time_.valid())
        throw std::invalid_argument("cell transmitter: start time out of range");
    validate(cfg_);
}

Bits CellTransmitter::next_burst()
{
    Bits b = downlink_burst(cfg_, time_);
    time_.advance();
    ++bursts_;
    return b;
}

void CellTransmitter::produce(size_t n)
{
    while (modulator_.available() < n)
        modulator_.push_bits(next_burst());
}

void CellTransmitter::read(std::complex<float>* out, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = {0.0f, 0.0f};
    add_to(out, n);
}

void CellTransmitter::add_to(std::complex<float>* out, size_t n)
{
    const uint64_t first = modulator_.samples();
    produce(n);
    scratch_.resize(n);
    modulator_.read(scratch_.data(), n);
    if (settings_.offset_hz == 0.0) {
        for (size_t i = 0; i < n; ++i)
            out[i] += gain_ * scratch_[i];
        return;
    }
    const std::complex<double> step = phasor(cycles_per_sample_);
    for (size_t i = 0; i < n; i += kNcoBlock) {
        // The exact phase of sample first + i, from the sample index.
        const double cycles = cycles_per_sample_ * static_cast<double>(first + i);
        std::complex<double> lo = phasor(cycles - std::floor(cycles));
        const size_t end = std::min(n, i + kNcoBlock);
        for (size_t j = i; j < end; ++j) {
            const std::complex<float> l(static_cast<float>(lo.real()), static_cast<float>(lo.imag()));
            out[j] += gain_ * scratch_[j] * l;
            lo *= step;
        }
    }
}

MultiCarrierTransmitter::MultiCarrierTransmitter(double sample_rate_hz) : sample_rate_(sample_rate_hz)
{
    if (!(sample_rate_hz > 2.0 * kHalfOccupiedBandwidthHz) || !std::isfinite(sample_rate_hz))
        throw std::invalid_argument("span sample rate " + std::to_string(sample_rate_hz) +
                                    " is below a carrier's occupied bandwidth (24.3 kHz)");
}

void MultiCarrierTransmitter::add(CellConfig cfg, CarrierSettings settings)
{
    if (samples_ != 0)
        throw std::logic_error("carriers must be added before the first read");
    carriers_.push_back(std::make_unique<CellTransmitter>(std::move(cfg), sample_rate_, settings));
}

void MultiCarrierTransmitter::read(std::complex<float>* out, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        out[i] = {0.0f, 0.0f};
    for (auto& c : carriers_)
        c->add_to(out, n);
    samples_ += n;
}

}  // namespace yantra::gen
