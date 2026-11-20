// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell transmitter of the TETRA test-signal generator (RAD-70): the
// continuous downlink main carrier of one cell (all four timeslots of every
// TDMA frame, frame 18 the control frame with synchronization bursts; the
// schedule is in downlink.hpp) as a stream of bursts and complex baseband
// samples, with a level, a frequency offset and a timing offset; and
// several such carriers summed in one wideband span (e.g. 500 ksps for
// carriers within +/-200 kHz).
//
// Getting IQ (the channel model, scenario engine and yantra-gen use this):
//   MultiCarrierTransmitter span(500000.0);             // output rate, sps
//   span.add(cfg_a, {.offset_hz = -50e3, .level_db = -10.0});
//   span.add(cfg_b, {.offset_hz = 75e3, .level_db = -20.0, .timing_offset_s = 1e-3});
//   std::vector<std::complex<float>> iq(4096);
//   span.read(iq.data(), iq.size());                    // call again for more
// or CellTransmitter tx(cfg, fs, settings); tx.read(...) for one carrier.
// Any sample rate above the occupied bandwidth works (not only whole
// samples per symbol); a carrier at level 0 dB has an average power of 1.
// Plain scalar code: the default and -DYANTRA_SIMD=OFF trees agree.
// Copied from yantra 68ea20a (plugins/tetra/gen cell_tx.h), with the
// schedule moved to downlink.hpp and level / frequency / timing offsets
// per carrier added.

#pragma once

#include "bits.hpp"
#include "downlink.hpp"
#include "modulator.hpp"
#include "timing.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace yantra::gen {

/// Where and how strong one carrier is in the output.
struct CarrierSettings {
    /// Carrier frequency relative to the centre of the output (Hz).
    double offset_hz = 0.0;
    /// Power in dB relative to 1 (0 dB: average power 1).
    double level_db = 0.0;
    /// Delay of every symbol in seconds (>= 0): the first burst's first
    /// symbol is centred at Pi4DqpskModulator::symbol_time(0) +
    /// timing_offset_s after sample 0.
    double timing_offset_s = 0.0;
};

/// Half the occupied bandwidth of a carrier: (1 + roll-off) * 18 kHz / 2 =
/// 12.15 kHz.
inline constexpr double kHalfOccupiedBandwidthHz = (1.0 + kRollOff) * kSymbolRate / 2.0;

/// One cell's main carrier: bursts one after the other from cfg.start,
/// modulated, scaled to its level and shifted to its offset.
class CellTransmitter {
public:
    /// Checks the configuration (validate()), the rate (see
    /// Pi4DqpskModulator) and the settings (finite; the carrier must fit
    /// the span: |offset| + 12.15 kHz < rate / 2). std::invalid_argument
    /// (or validate()'s exceptions) otherwise.
    CellTransmitter(CellConfig cfg, double sample_rate_hz, CarrierSettings settings = {});

    const CellConfig& config() const { return cfg_; }
    const CarrierSettings& settings() const { return settings_; }
    /// Time of the next burst.
    const TdmaTime& time() const { return time_; }
    /// Bursts made so far.
    uint64_t bursts() const { return bursts_; }
    /// Samples read so far.
    uint64_t samples() const { return modulator_.samples(); }
    double sample_rate() const { return modulator_.sample_rate(); }

    /// The next burst's bits; advances the time. Not for use together
    /// with read() (which takes its bursts from here).
    Bits next_burst();
    /// The next `n` samples.
    void read(std::complex<float>* out, size_t n);
    /// The next `n` samples added to `out` (for summing carriers).
    void add_to(std::complex<float>* out, size_t n);

private:
    void produce(size_t n);

    CellConfig cfg_;
    CarrierSettings settings_;
    TdmaTime time_;
    uint64_t bursts_ = 0;
    Pi4DqpskModulator modulator_;
    float gain_;
    double cycles_per_sample_;
    std::vector<std::complex<float>> scratch_;
};

/// Several cells' carriers in one complex baseband span.
class MultiCarrierTransmitter {
public:
    /// Output rate, e.g. 500 ksps (carriers within +/-200 kHz) or 1 Msps.
    /// std::invalid_argument when it is not above a carrier's occupied
    /// bandwidth.
    explicit MultiCarrierTransmitter(double sample_rate_hz);

    /// Adds a cell's carrier (see CellTransmitter for the checks). All
    /// carriers start at sample 0: std::logic_error once samples were read.
    void add(CellConfig cfg, CarrierSettings settings);
    size_t carriers() const { return carriers_.size(); }
    const CellTransmitter& carrier(size_t i) const { return *carriers_.at(i); }

    /// The next `n` samples: the sum of all carriers (zeros without any).
    void read(std::complex<float>* out, size_t n);
    /// Samples read so far.
    uint64_t samples() const { return samples_; }
    double sample_rate() const { return sample_rate_; }

private:
    double sample_rate_;
    uint64_t samples_ = 0;
    std::vector<std::unique_ptr<CellTransmitter>> carriers_;
};

}  // namespace yantra::gen
