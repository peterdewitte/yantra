// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The receiver side of the sensitivity and quality harnesses (internal to
// yantra::sens): generator -> channel -> (channelizer) -> channel IQ
// at 72 kHz, and the raw bit errors of a free-running demodulator against
// the transmitted bits.
#pragma once

#include "yantra/gen/random.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/sens/sweep.hpp"

#include <cstdint>
#include <vector>

namespace yantra::sens::detail {

/// What the receiver chain delivers: channel IQ at 72 kHz, and where in it
/// the transmitted symbol k is centred: origin + k * step samples.
struct ChannelSignal {
    std::vector<receiver::cf> iq;
    double origin = 0.0;
    double step = 4.0;
    /// The carrier against the channel centre (ground truth of the
    /// frequency error): the set CFO, minus the channelizer's grid point
    /// for wideband scenarios.
    double carrier_offset_hz = 0.0;
};

ChannelSignal receive(const Scenario& s, double snr_db, double seconds, uint64_t seed, const gen::CellConfig& cell,
                      receiver::Isa isa);

/// Wiener (random-walk) phase noise with a two-sided 3-dB linewidth
/// `linewidth_hz`: phase increments of variance 2 pi linewidth / rate.
/// std::invalid_argument for a rate <= 0.
class PhaseNoise {
public:
    PhaseNoise(double linewidth_hz, double rate_hz, uint64_t seed);
    void apply(receiver::cf* x, std::size_t n);

private:
    double sigma_;
    double phase_ = 0.0;
    gen::Rng rng_;
};

struct RawBits {
    uint64_t bits = 0;
    uint64_t errors = 0;
};

/// Hard-decision bit errors of a demodulator with the decoder's settings
/// over the transmitted symbols [k_begin, k_end); skipped symbols count
/// as one wrong bit of two.
RawBits raw_bit_errors(const ChannelSignal& sig, const gen::Bits& sent, long k_begin, long k_end, receiver::Isa isa);

}  // namespace yantra::sens::detail
