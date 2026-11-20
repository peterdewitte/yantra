// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wideband test spans for the carrier search and the measurement engine:
// several generator cells (RAD-70) at chosen offsets, levels and timing
// offsets in one capture, plus white noise, read in blocks with an
// optional gate (zeros while "off").
#pragma once

#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/channel.hpp"
#include "yantra/gen/downlink.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace enginetest {

namespace gen = yantra::gen;
using cf = std::complex<float>;

/// One cell of a span.
struct SpanCell {
    double offset_hz = 0.0;  ///< from the capture centre
    double level_dbfs = -20.0;  ///< average power, dBFS (0 dBFS = power 1)
    uint8_t colour_code = 1;
    uint16_t mnc = 1;
    uint16_t location_area = 1;
    uint16_t main_carrier = 3600;
    uint8_t offset_code = 0;
    bool encrypted = false;
    double timing_offset_s = 0.0;
};

inline gen::CellConfig cell_config(const SpanCell& c)
{
    gen::CellConfig cfg = gen::CellConfig::defaults();
    cfg.sync.colour_code = c.colour_code;
    cfg.sync.mle.mnc = c.mnc;
    cfg.sysinfo.mle.location_area = c.location_area;
    cfg.sysinfo.main_carrier = c.main_carrier;
    cfg.sysinfo.offset = c.offset_code;
    cfg.sysinfo.mle.bs_service_details.air_interface_encryption = c.encrypted;
    return cfg;
}

/// A capture of several cells plus white noise.
class Span {
public:
    Span(double rate_hz, const std::vector<SpanCell>& cells, double noise_dbfs = -90.0, uint64_t seed = 1)
        : rate_(rate_hz), tx_(rate_hz),
          noise_(noise_dbfs < -200.0 ? nullptr : std::make_unique<gen::Awgn>(std::pow(10.0, noise_dbfs / 10.0), seed))
    {
        for (const SpanCell& c : cells)
            tx_.add(cell_config(c), {c.offset_hz, c.level_dbfs, c.timing_offset_s});
    }
    double rate() const { return rate_; }
    double seconds() const { return static_cast<double>(pos_) / rate_; }
    /// The next `n` samples; zeros (noise only) where gate(t) is false.
    void read(cf* x, std::size_t n, const std::function<bool(double)>& gate = {})
    {
        tx_.read(x, n);
        if (gate && !gate(seconds()))
            for (std::size_t i = 0; i < n; ++i)
                x[i] = {};
        if (noise_)
            noise_->add(x, n);
        pos_ += n;
    }

private:
    double rate_;
    gen::MultiCarrierTransmitter tx_;
    std::unique_ptr<gen::Awgn> noise_;
    uint64_t pos_ = 0;
};

}  // namespace enginetest
