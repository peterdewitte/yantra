// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test signals for the decoder tests: one TETRA cell from the generator
// (RAD-70) at 72 kHz with optional noise, carrier offset and timing
// offset, bursts as soft bits, and a helper that runs the carrier decoder
// over a signal. Copied from yantra 68ea20a: tests/tetra_decoder/signal.h.
#pragma once

#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/channel.hpp"
#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace dectest {

namespace gen = yantra::gen;
namespace rx = yantra::receiver::tetra;
namespace dec = yantra::receiver::tetra;  // the name the 4.3 tests copied from yantra use
using cf = std::complex<float>;

struct CellSignal {
    gen::CellConfig cell = gen::CellConfig::defaults();
    double seconds = 1.5;
    double fs = rx::kChannelRateHz;
    double cfo_hz = 0.0;
    double timing_offset_s = 0.0;  ///< the cell starts this late (a fraction of a symbol or more)
    double snr_db = 200.0;         ///< in the 18 kHz symbol-rate bandwidth; 200 = none
    uint64_t seed = 1;
};

inline std::vector<cf> make_cell(const CellSignal& s)
{
    gen::CellTransmitter tx(s.cell, s.fs, {.timing_offset_s = s.timing_offset_s});
    std::vector<cf> iq(static_cast<std::size_t>(s.seconds * s.fs));
    tx.read(iq.data(), iq.size());
    if (s.cfo_hz != 0.0)
        gen::FrequencyShifter(s.cfo_hz, s.fs).apply(iq.data(), iq.size());
    if (s.snr_db < 150.0) {
        // Unit signal power; noise over the whole sample rate for the
        // wanted SNR in 18 kHz.
        const double noise = 1.0 / std::pow(10.0, s.snr_db / 10.0) * (s.fs / 18000.0);
        gen::Awgn(noise, s.seed).add(iq.data(), iq.size());
    }
    return iq;
}

inline rx::CarrierDecoderConfig test_config()
{
    rx::CarrierDecoderConfig c;
    c.utc_from_system_clock = false;
    return c;
}

/// Runs the decoder over `iq` in blocks of `block` samples.
inline rx::DecoderOutput decode(const std::vector<cf>& iq, std::size_t block = 4096,
                                const rx::CarrierDecoderConfig& cfg = test_config())
{
    rx::CarrierDecoder d(cfg);
    rx::DecoderOutput out;
    for (std::size_t i = 0; i < iq.size(); i += block)
        d.process(iq.data() + i, std::min(block, iq.size() - i), out);
    return out;
}

/// Blocks with a CRC per logical channel over all decoded slots.
struct CrcCount {
    int blocks = 0;
    int ok = 0;
};

inline CrcCount crc_of(const rx::DecoderOutput& out, rx::LogicalChannel c)
{
    CrcCount n;
    for (const rx::DecodedSlot& s : out.slots)
        for (const rx::DecodedBlock& b : s.result.blocks)
            if (b.channel == c) {
                ++n.blocks;
                n.ok += b.crc_ok ? 1 : 0;
            }
    return n;
}

/// A decoder burst from transmitted bits.
inline rx::Burst burst_of(const gen::Bits& bits, rx::BurstType type)
{
    rx::Burst b;
    b.type = type;
    for (std::size_t i = 0; i < b.soft.size(); ++i)
        b.soft[i] = bits[i] ? -64 : 64;
    return b;
}

/// The burst type of a transmitted downlink burst (by its training
/// sequence).
inline rx::BurstType type_of(const gen::Bits& b)
{
    auto matches = [&](rx::BurstType t) {
        const auto seq = rx::training_sequence(t);
        const auto at = static_cast<std::size_t>(rx::training_sequence_bit(t));
        for (std::size_t i = 0; i < seq.size(); ++i)
            if (b[at + i] != seq[i])
                return false;
        return true;
    };
    if (matches(rx::BurstType::Sync))
        return rx::BurstType::Sync;
    return matches(rx::BurstType::NormalHalf) ? rx::BurstType::NormalHalf : rx::BurstType::NormalFull;
}

/// The bursts of `n` slots of a cell.
inline std::vector<gen::Bits> bursts(const gen::CellConfig& cell, int n)
{
    gen::CellTransmitter tx(cell, 72000.0);
    std::vector<gen::Bits> out;
    for (int i = 0; i < n; ++i)
        out.push_back(tx.next_burst());
    return out;
}

}  // namespace dectest
