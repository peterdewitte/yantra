// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/include/yantra/tetra/
// decoder/quality.h, adapted for RAD-113.
//
// Downlink quality metrics of one carrier (RAD-113), reported by the
// CarrierDecoder once per quality window (the measurement engine sets it
// to its record interval, the level meter's window). Two different things
// share the abbreviation "MER" in
// TETRA practice; they are kept apart by name everywhere:
//  - message erasure rate (RAD-113): the share of AACH blocks that could
//    not be decoded in the last 100 timeslots (a running window);
//  - modulation error ratio (RAD-113): the power of the ideal constellation
//    points over the power of the error vectors, in dB (EVM is the same
//    error relative to the signal in % RMS).
// The other metrics:
//  - slot CRC error rate per logical channel (RAD-113) over the last 288
//    timeslots (4 multiframes, about 4.1 s);
//  - BER estimate (RAD-113): blocks whose CRC passed are encoded again and
//    compared with the hard decisions of the received bits, over the same
//    288 timeslots;
//  - SNR: second- and fourth-order moments (M2M4) of the symbol samples,
//    over the report window (non-data-aided, valid also where decisions
//    are often wrong);
//  - frequency error (RAD-113) of the carrier against the channel centre:
//    the demodulator's estimate averaged over the report window.
// docs/MEASUREMENT.md explains each metric, its estimator and the range
// in which it was verified against the generator.
#pragma once

#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/receiver/tdma.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

namespace yantra::receiver::tetra {

struct DecodedSlot;

struct QualityConfig {
    std::size_t erasure_window_slots = 100;  ///< message erasure rate window (RAD-113)
    std::size_t crc_window_slots = 288;      ///< CRC rates and BER: 4 multiframes
};

/// Blocks of one logical channel in the CRC window.
struct WindowCounts {
    uint32_t blocks = 0;
    uint32_t crc_errors = 0;
    std::optional<double> error_percent() const
    {
        if (blocks == 0)
            return std::nullopt;
        return 100.0 * static_cast<double>(crc_errors) / static_cast<double>(blocks);
    }
};

/// The quality of one carrier at the end of a report window.
struct QualityReport {
    double sample_pos = 0.0;  ///< end of the window (channel samples since the decoder's start)
    uint64_t samples = 0;     ///< length of the window
    bool burst_locked = false;  ///< burst sync at the end of the window
    bool demod_locked = false;  ///< demodulator lock at the end of the window
    std::optional<TdmaTime> time;  ///< H-M-F-S of the last timeslot decoded in the window, once known
    uint64_t slots_total = 0;      ///< timeslots the decoder reported before this report (DecoderOutput::slots)

    // Message erasure rate: the last erasure_window_slots timeslots of the
    // burst sync since it was (re)gained, counted once the cell's
    // scrambling code is known (from the first BSCH on). An erasure is an
    // AACH whose RM(30,14) code word could not be decoded, or a timeslot
    // whose burst was missed (the burst sync coasting through a fade).
    uint32_t erasure_slots = 0;    ///< timeslots in the window (<= erasure_window_slots)
    uint32_t aach_failures = 0;    ///< AACH received but not decodable
    uint32_t missed_slots = 0;     ///< burst missed (no training sequence)
    std::optional<double> message_erasure_rate_percent() const
    {
        if (erasure_slots == 0)
            return std::nullopt;
        return 100.0 * static_cast<double>(aach_failures + missed_slots) / static_cast<double>(erasure_slots);
    }

    // Slot CRC per logical channel and BER: the last crc_window_slots
    // timeslots whose burst was found (same start as above).
    uint32_t crc_slots = 0;
    std::array<WindowCounts, kLogicalChannelCount> channels{};  ///< AACH: RM(30,14) failures
    uint64_t ber_bits = 0;    ///< coded bits of blocks with a good CRC
    uint64_t ber_errors = 0;  ///< of those, received wrong
    std::optional<double> ber_percent() const
    {
        if (ber_bits == 0)
            return std::nullopt;
        return 100.0 * static_cast<double>(ber_errors) / static_cast<double>(ber_bits);
    }

    // Modulation (symbols of the report window; absent without them).
    uint64_t symbols = 0;      ///< symbols for the modulation error ratio
    uint64_t snr_symbols = 0;  ///< symbols for the SNR
    std::optional<double> modulation_error_ratio_db;
    std::optional<double> evm_percent;  ///< RMS error vector over the RMS signal
    std::optional<double> snr_db;       ///< M2M4, Es/N0
    /// Received minus nominal carrier: mean of the demodulator's estimate
    /// over the window while it was locked.
    std::optional<double> frequency_error_hz;
};

/// Frequency error in ppm of the nominal carrier frequency.
inline double frequency_error_ppm(double error_hz, double carrier_hz)
{
    return carrier_hz > 0.0 ? error_hz / carrier_hz * 1e6 : 0.0;
}

/// SNR (Es/N0, dB) of a constant-envelope signal in complex Gaussian noise
/// from the moments M2 = E|z|^2 and M4 = E|z|^4 of its symbol samples:
/// S = sqrt(2 M2^2 - M4), N = M2 - S. Nothing when the moments allow no
/// estimate (no noise visible: 99 dB is returned as the ceiling).
std::optional<double> m2m4_snr_db(double m2, double m4);

/// Collects what the quality report needs; the CarrierDecoder feeds it.
class QualityMeter {
public:
    explicit QualityMeter(const QualityConfig& config = {});

    /// Demodulated symbols: those while the demodulator is locked count
    /// for the modulation error ratio and EVM; those while it or the burst
    /// sync is locked (symbol timing right) for the SNR.
    void add_symbols(const DemodSymbol* s, std::size_t n, bool demod_locked, bool burst_locked);
    /// The demodulator's frequency estimate over `samples` input samples
    /// while it is locked: the report gives the sample-weighted mean.
    void add_frequency(double offset_hz, uint64_t samples);
    /// One timeslot from the burst sync. `counted`: the cell's scrambling
    /// code is known (the AACH can be decoded); slots before are left out.
    /// A slot with `lost` set restarts the windows after it.
    void add_slot(const DecodedSlot& slot, bool counted);
    /// Burst sync lost or acquisition restarted: the slot windows start
    /// again (the reports show no message erasure rate, CRC rate or BER
    /// until timeslots come again).
    void restart_windows();
    /// The report of the window that ends at `sample_pos`; the symbol
    /// window starts again.
    QualityReport report(double sample_pos, uint64_t samples, const DemodStatus& demod, bool burst_locked);
    void reset();

private:
    struct SlotEntry {
        bool found = false;
        bool aach_failed = false;
        std::array<uint8_t, kLogicalChannelCount> blocks{};
        std::array<uint8_t, kLogicalChannelCount> errors{};
        uint16_t bits = 0;
        uint16_t bit_errors = 0;
    };

    QualityConfig cfg_;
    std::deque<SlotEntry> slots_;  ///< newest at the back
    std::optional<TdmaTime> last_time_;
    // Symbol sums of the report window.
    uint64_t symbols_ = 0;  ///< demodulator locked
    double proj_ = 0.0;     ///< sum of Re(z * conj(ideal))
    double p2_ = 0.0;       ///< sum of |z|^2
    uint64_t snr_symbols_ = 0;  ///< demodulator or burst sync locked
    double m2_ = 0.0;
    double m4_ = 0.0;
    double freq_sum_ = 0.0;
    uint64_t freq_samples_ = 0;
};

}  // namespace yantra::receiver::tetra
