// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoder sensitivity sweep (RAD-126, RAD-124; the receiver (RAD-100, RAD-102) against
// the generator of RAD-70): for each scenario (AWGN, carrier frequency
// error, off-centre carrier, sample-clock error, TU50, HT200, phase noise,
// combined) and each SNR of a tier, generator -> channel -> receiver chain
// -> CarrierDecoder, measuring against what the generator sent:
//   - decode rate per logical channel: BSCH (MAC-SYNC), BNCH (SYSINFO),
//     SCH/F and AACH blocks decoded with a good CRC (AACH: RM(30,14)
//     decoded) / blocks sent, over the measurement window (from
//     kWindowStartS to the end; acquisition losses after that count);
//     1 - rate is EN 300 392-2's message erasure rate;
//   - burst sync: slots in which the decoder found a burst (training
//     sequence) / slots sent;
//   - slot CRC error rate: CRC failures / CRC-protected blocks the decoder
//     found (bursts it synchronised to);
//   - raw bit error rate of the demodulator's hard decisions (a second
//     demodulator with the decoder's configuration) against the
//     transmitted bits;
//   - lock time: time from the start of the signal to the first BSCH
//     decoded (the cell identity known; the signal starts two frames
//     before frame 18), and the demodulator's MER.
// SNR is the mean signal power over the noise power in the symbol-rate
// bandwidth (18 kHz), i.e. Es/N0; Eb/N0 = SNR - 3 dB.
//
// Receiver chain: scenarios with a carrier within the demodulator's pull-in
// range feed the 72 kHz channel directly; "wideband" scenarios generate a
// 288 kHz capture with the carrier off its centre and run the TETRA
// channelizer tuned to the nearest 6.25 kHz grid point, as the carrier
// search does (receiver/carrier_search.hpp: spectrum peak on the grid, the
// demodulator takes the rest).
//
// Every point is deterministic for (base seed, scenario, SNR, run).
#pragma once

#include "yantra/receiver/kernels.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace yantra::sens {

inline constexpr double kCarrierHz = 390.0e6;  ///< carrier the ppm values refer to
inline constexpr double kWindowStartS = 0.25;  ///< measurement window start
inline constexpr double kWidebandRateHz = 288000.0;

struct Scenario {
    std::string name;
    std::string description;
    std::string propagation = "static";  ///< gen::propagation name (gen/channel.hpp)
    double cfo_ppm = 0.0;        ///< carrier frequency error at kCarrierHz
    double clock_ppm = 0.0;      ///< sample clock error (receiver clock fast)
    double phase_noise_hz = 0.0; ///< Wiener linewidth
    double offcentre_hz = 0.0;   ///< carrier position in the wideband capture
    bool wideband = false;       ///< through the channelizer (288 kHz capture)
};

/// The RAD-126 scenarios: awgn, cfo2, cfo20, cfo50, offcentre, clock50,
/// tu50, ht200, phasenoise, combined.
std::vector<Scenario> scenarios();
/// One of scenarios() by name; std::invalid_argument when unknown.
Scenario scenario(const std::string& name);

struct Tier {
    std::string name;
    std::vector<double> snr_db;
    int runs = 1;          ///< independent runs (seeds, start times) per point
    double seconds = 2.0;  ///< signal per run
};

/// "quick" (every test run and on demand in CI: 4, 8, 12, 20 dB, one run
/// of 4 s) or "full" (before a release: 0..14 dB in 1 dB steps and 16..30
/// dB in 2 dB steps, 8 runs of 6 s); std::invalid_argument otherwise.
Tier tier(const std::string& name);

struct Counts {
    uint64_t sent = 0;
    uint64_t ok = 0;
    double rate() const { return sent ? static_cast<double>(ok) / static_cast<double>(sent) : 0.0; }
};

struct PointResult {
    std::string scenario;
    double snr_db = 0.0;
    int runs = 0;
    Counts bsch, bnch, schf, aach;
    Counts sync;              ///< slots sent / slots with a burst found (training sequence)
    uint64_t crc_blocks = 0;  ///< CRC-protected blocks the decoder found
    uint64_t crc_errors = 0;
    uint64_t bits = 0;        ///< demodulated bits compared
    uint64_t bit_errors = 0;
    int locked_runs = 0;      ///< runs with a BSCH decoded
    double lock_ms_sum = 0.0; ///< over locked runs
    double mer_db_sum = 0.0;  ///< over measurement periods
    int mer_periods = 0;

    double crc_error_rate() const
    {
        return crc_blocks ? static_cast<double>(crc_errors) / static_cast<double>(crc_blocks) : 1.0;
    }
    double ber() const { return bits ? static_cast<double>(bit_errors) / static_cast<double>(bits) : 0.5; }
    double lock_ms() const { return locked_runs ? lock_ms_sum / locked_runs : -1.0; }
    double mer_db() const { return mer_periods ? mer_db_sum / mer_periods : 0.0; }
    void add(const PointResult& o);
};

/// One run of one point (seed derived from base_seed, scenario, SNR, run).
PointResult run_once(const Scenario& s, double snr_db, double seconds, int run, uint64_t base_seed,
                     receiver::Isa isa = receiver::Isa::Simd);
/// `runs` runs of one point, added up.
PointResult run_point(const Scenario& s, double snr_db, const Tier& t, uint64_t base_seed,
                      receiver::Isa isa = receiver::Isa::Simd);

struct SweepPoint {
    Scenario scenario;
    double snr_db;
};
/// Runs every point with `threads` worker threads (0: hardware
/// concurrency); results in the order of `points`, independent of the
/// thread count.
std::vector<PointResult> run_sweep(const std::vector<SweepPoint>& points, const Tier& t, uint64_t base_seed,
                                   unsigned threads = 0, receiver::Isa isa = receiver::Isa::Simd);

/// Metric names of the CSV, the Markdown tables and the baseline:
/// bsch, bnch (SYSINFO), schf, aach (decode rates, %), sync (slots with a
/// burst found, %), crc_err (slot CRC
/// error rate, %), ber (%), lock_ms, mer_db. std::invalid_argument for
/// another name.
double metric(const PointResult& r, const std::string& name);
std::vector<std::string> metric_names();

/// CSV with one row per point (header line first).
void write_csv(std::ostream& os, const std::vector<PointResult>& results);
/// Markdown report: per scenario a table of the metrics over SNR, then the
/// lowest SNR per scenario at which SYSINFO and SCH/F reach 90 % and 99 %.
void write_markdown(std::ostream& os, const std::vector<PointResult>& results, const Tier& t, uint64_t base_seed);

/// A baseline row: at `snr_db` the `metric` of `scenario` must be at least
/// `min` (or at most `max` for crc_err, ber and lock_ms). File format
/// (tests/sens/): CSV "scenario,snr_db,metric,bound,limit" with bound
/// "min" or "max"; '#' starts a comment line.
struct Expectation {
    std::string scenario;
    double snr_db = 0.0;
    std::string metric;
    bool is_min = true;
    double limit = 0.0;
};
/// std::runtime_error with the line number for a malformed line.
std::vector<Expectation> parse_baseline(std::istream& is);

struct Verdict {
    Expectation expectation;
    double measured = 0.0;
    bool pass = false;
};
/// Runs the points the expectations need (tier settings) and compares.
std::vector<Verdict> check_baseline(const std::vector<Expectation>& e, const Tier& t, uint64_t base_seed,
                                    unsigned threads = 0, receiver::Isa isa = receiver::Isa::Simd);

}  // namespace yantra::sens
