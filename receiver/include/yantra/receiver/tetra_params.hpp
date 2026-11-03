// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA parameter set of the receiver DSP (RAD-100, RAD-101): EN 300
// 392-2 clause 5 downlink, pi/4-DQPSK at 18 ksymbol/s, RRC roll-off 0.35,
// 25 kHz carrier raster. The channelizer delivers 4 samples per symbol
// (72 kHz); its passband covers the signal (+/-12.15 kHz) plus a
// frequency error of up to +/-2.85 kHz (about 7 ppm at 400 MHz), and the
// matched RRC filter does the final adjacent-channel selection: its span
// of 24 symbols gives > 70 dB rejection of a neighbour 25 kHz away (16
// symbols: about 63 dB); tests/receiver_tetra measures it.
#pragma once

#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"

namespace yantra::receiver::tetra {

inline constexpr double kSymbolRateHz = 18000.0;
inline constexpr double kRollOff = 0.35;
inline constexpr double kChannelSpacingHz = 25000.0;
inline constexpr int kSamplesPerSymbol = 4;
inline constexpr double kChannelRateHz = kSymbolRateHz * kSamplesPerSymbol;

inline receiver::ChannelizerConfig channelizer_config(double input_rate_hz, receiver::Isa isa = receiver::Isa::Simd)
{
    receiver::ChannelizerConfig c;
    c.input_rate_hz = input_rate_hz;
    c.output_rate_hz = kChannelRateHz;
    c.passband_hz = 15000.0;
    c.stopband_hz = 22000.0;
    c.atten_db = 70.0;
    c.isa = isa;
    return c;
}

inline receiver::Pi4DqpskConfig demod_config(receiver::Isa isa = receiver::Isa::Simd)
{
    receiver::Pi4DqpskConfig c;
    c.symbol_rate_hz = kSymbolRateHz;
    c.samples_per_symbol = kSamplesPerSymbol;
    c.rolloff = kRollOff;
    c.rrc_span_symbols = 24;
    c.max_cfo_hz = 3000.0;
    c.isa = isa;
    return c;
}

}  // namespace yantra::receiver::tetra
