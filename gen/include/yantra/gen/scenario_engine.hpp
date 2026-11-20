// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario engine of the TETRA test-signal generator (RAD-74 with RAD-72,
// RAD-73): runs a Scenario and gives the complex baseband samples at the
// receiver and the ground truth per time step.
//
// Signal chain per cell: CellTransmitter (unit power, rendered at the true
// sample rate fs (1 + clock_ppm), started at the cell's TDMA time offset,
// shifted to its span offset plus its frequency error: cell ppm, receiver
// ppm, Doppler shift) x amplitude of its mean level (multi-wall model on
// the receiver's floor scene, log-distance for a site, or the fixed
// level) x flat fading, or through a TU50/HT200 tapped delay line; the
// cells are summed, AWGN from the noise figure is added, then the front
// end (gain, IQ imbalance, swap, DC, clipping, quantisation) and sample
// drops. Amplitudes and fading gains are computed every 1 ms of signal
// time and interpolated in between, so the output does not depend on how
// read() is called. Events (cell off/on, level steps, receiver gain steps)
// act from their sample on.
//
// The simulator plugin (RAD-61) drives the same engine with an endless
// scenario (duration 0), the host's scene (set_scene) and the cursor or
// walk position (set_position), and reads levels_at() for its sensors;
// yantra-gen (RAD-71) writes read() to IQ files and take_truth() to the
// ground-truth CSV and record files. Formulas: docs/CHANNEL_MODEL.md.
// The engine structure of yantra 68ea20a
// (plugins/tetra/scenario/include/yantra/tetra/scenario/engine.h) was the
// starting point.
#pragma once

#include "cell_tx.hpp"
#include "channel.hpp"
#include "multiwall.hpp"
#include "scenario.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yantra::gen {

/// Noise bandwidth of the SNR in the ground truth: the TETRA symbol rate
/// (the noise bandwidth of the receiver's root-raised-cosine filter).
inline constexpr double kChannelNoiseBandwidthHz = 18000.0;

/// A receiver position on a floor (plan frame metres).
struct ReceiverPosition {
    std::string floor;
    double x_m = 0.0;
    double y_m = 0.0;
    bool operator==(const ReceiverPosition&) const = default;
};

/// How a cell's level was worked out.
enum class LevelSource { fixed, scene, site, off };
const char* to_string(LevelSource s);

/// Ground truth of one cell at one time.
struct CellTruth {
    std::string cell;
    uint16_t mcc = 0, mnc = 0, location_area = 0;
    uint8_t colour_code = 0;
    double frequency_hz = 0.0;  ///< nominal downlink frequency
    double offset_hz = 0.0;     ///< nominal offset from the span centre
    double cfo_hz = 0.0;        ///< frequency error in the samples (cell + receiver + Doppler shift)
    bool on_air = true;
    LevelSource source = LevelSource::fixed;
    /// Local mean level at the antenna connector (path loss, walls,
    /// shadowing, level events; no fast fading), dBm; -inf when off.
    double level_dbm = 0.0;
    /// With flat fading: what the samples carry at this time, dBm (the
    /// mean level for the tapped-delay-line models).
    double level_inst_dbm = 0.0;
    /// level_dbm - noise in kChannelNoiseBandwidthHz (inf without noise).
    double snr_db = 0.0;
    /// Scene cells: the strongest antenna and its walls crossed.
    std::string best_antenna;
    int walls = 0;
};

struct TruthRow {
    double t = 0.0;             ///< signal time, seconds
    uint64_t sample = 0;        ///< signal sample index (before drops)
    uint64_t output_sample = 0; ///< index in the output (after drops)
    std::optional<ReceiverPosition> position;
    double noise_dbm = 0.0;     ///< in kChannelNoiseBandwidthHz (-inf without noise)
    double rx_gain_db = 0.0;
    std::vector<CellTruth> cells;
};

class ScenarioEngine {
public:
    /// Validates the scenario and sets up every cell. ScenarioError or
    /// std::invalid_argument when it cannot run.
    explicit ScenarioEngine(Scenario s);
    ~ScenarioEngine();
    ScenarioEngine(const ScenarioEngine&) = delete;
    ScenarioEngine& operator=(const ScenarioEngine&) = delete;

    const Scenario& scenario() const { return s_; }
    /// The labelled output rate (the scenario's sample_rate_hz).
    double sample_rate() const { return s_.sample_rate_hz; }
    /// The rate the signal is really sampled at: fs (1 + clock_ppm 1e-6).
    double true_sample_rate() const { return fs_true_; }
    double centre_frequency_hz() const { return centre_hz_; }
    /// Signal samples of the scenario (0: endless).
    uint64_t total_samples() const { return total_; }
    /// Signal samples made so far (dropped ones included).
    uint64_t position() const { return pos_; }
    /// Output samples delivered so far.
    uint64_t output_samples() const { return out_; }
    bool done() const { return total_ > 0 && pos_ >= total_; }

    /// Writes up to `n` output samples (fewer only at the end) and records
    /// the ground-truth rows whose sample falls in the signal made.
    /// Returns the number written.
    size_t read(std::complex<float>* out, size_t n);
    /// Ground-truth rows recorded since the last call (one every
    /// truth_interval_s of signal time, the first at t = 0).
    std::vector<TruthRow> take_truth();

    /// The ground truth at signal time t (what read() records). With
    /// fading the instantaneous level is from the same seeded generator.
    TruthRow truth_at(double t) const;
    /// The receiver position at signal time t: the override when set,
    /// else the scenario path; nullopt without either.
    std::optional<ReceiverPosition> receiver_at(double t) const;
    /// Mean levels of every cell at a position (or without one), as the
    /// truth reports them, at signal time t (events up to t applied).
    std::vector<CellTruth> levels_at(const std::optional<ReceiverPosition>& pos, double t) const;

    /// Simulator hooks (RAD-61). A fixed position replacing the path from
    /// now on (nullopt: back to the path).
    void set_position(std::optional<ReceiverPosition> pos);
    /// Adds or replaces the scene of a floor (the host's get_scene, with
    /// the scenario's antenna overrides applied). Scene cells the scenario
    /// does not know are ignored (see add_scene_cells to add them before
    /// building the engine).
    void set_scene(const radios::Scene& scene);

    /// Mean power of the noise in the samples (0 without noise).
    double noise_power() const { return noise_power_; }
    /// Samples clipped by the front end so far.
    uint64_t clipped() const;
    /// Signal samples dropped so far.
    uint64_t dropped() const;

    size_t cells() const;
    const ScenarioCell& cell(size_t i) const;
    double cell_offset_hz(size_t i) const;
    double cell_cfo_hz(size_t i) const;
    /// Generator state of cell i: TDMA time of its next burst and the
    /// bursts it has sent so far.
    TdmaTime cell_tdma_time(size_t i) const;
    uint64_t cell_bursts(size_t i) const;

private:
    struct Cell;
    struct EventState {
        std::vector<bool> on;
        std::vector<double> level_db;
        double rx_gain_db = 0.0;
    };
    EventState events_at(double t) const;
    CellTruth cell_truth(size_t i, const std::optional<ReceiverPosition>& pos, const EventState& ev,
                         double t, bool instantaneous) const;
    std::complex<double> grid_gain(size_t i, uint64_t k) const;
    void produce(std::complex<float>* x, size_t n);
    void record_truth_until(uint64_t end);

    Scenario s_;
    double centre_hz_ = 0.0;
    double fs_true_ = 0.0;
    uint64_t total_ = 0;
    uint64_t pos_ = 0;
    uint64_t out_ = 0;
    uint64_t grid_ = 1;  ///< signal samples between gain updates (1 ms)
    double noise_power_ = 0.0;
    double noise_dbm_channel_ = 0.0;
    double max_speed_mps_ = 0.0;
    std::optional<ReceiverPosition> override_;
    std::vector<radios::Scene> scenes_;  ///< with overrides applied
    MultiWallModel multiwall_;
    std::vector<std::unique_ptr<Cell>> cells_;
    std::unique_ptr<Awgn> awgn_;
    std::unique_ptr<FrontEnd> front_end_;
    std::unique_ptr<SampleDropper> dropper_;
    size_t next_event_ = 0;
    uint64_t next_truth_ = 0;
    std::vector<TruthRow> truth_;
    std::vector<std::complex<float>> scratch_, sum_;
};

}  // namespace yantra::gen
