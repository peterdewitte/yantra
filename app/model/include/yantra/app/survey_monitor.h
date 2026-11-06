// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The survey panels' model (UI-01..04, UI-08, UI-11, NFR-07; docs/APP.md
// "Survey panels"), without drawing: an observer of a measurement
// pipeline (the live radios' or a replay's) that keeps
//
// - the frame the panels show (UI-01 large values, UI-02 serving cell,
//   UI-03 neighbours, the scanner view): built from the newest cycle, or
//   from the cycle at the graph cursor (UI-04: the cursor synchronises
//   every panel to that time);
// - the history the graphs draw (UI-04): per cycle the serving cell and
//   up to 31 neighbours with level and C1, and the serving cell's first
//   quality value (the MER trace for TETRA), with per-cell colours from
//   the cell database;
// - the event list (serving-cell changes, sync losses, device lost and
//   back, data loss, the self-interference warning, markers and voice
//   notes) and the statistics panel (UI-11);
// - the self-interference warning (NFR-07, core measure/self_interference.h).
//
// Technology-neutral (PLG-01): rows hold the technology schema's fields as
// text (labels from the schema); the radio API's core fields (frequency,
// channel, level, la, mcc, mnc; RAD-21) are the large values; threshold
// lines are the serving record's dBm / dB broadcast fields the user picks
// (FRT, SRT and min RX access for TETRA come from its schema). Cell names
// and colours come from the project's cell database by its match fields.
//
// Times are pipeline-clock nanoseconds (monotonic; a replay's clock runs
// on the recorded times). Thread-safe: cycles come on the ticking thread
// (live) or the replay player's thread; the UI reads copies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/measure/self_interference.h"

namespace yantra::app {

/// Serving cell + 31 neighbours (UI-04).
inline constexpr std::size_t kMaxPlotCells = 32;

/// UI-03: a neighbour seen in the measurements, in the serving cell's
/// neighbour list, or both (shown in different colours).
enum class ListState { Both, MeasuredOnly, ListedOnly };
const char* list_state_label(ListState s) noexcept;

/// One field of a row as text (label and unit from the schema).
struct FieldText {
    std::string name;
    std::string label;
    std::string value;  ///< value with its unit
};

/// A carrier as a panel row (UI-02, UI-03, scanner view).
struct CellRow {
    std::string key;           ///< "<device>|<frequency>": stable for the graphs
    std::string device_key;
    measure::CarrierRole role = measure::CarrierRole::Other;
    std::uint64_t frequency_hz = 0;
    std::string channel;       ///< the core field `channel` as text ("" none)
    std::string identity;      ///< the KEY fields, "name=value;..."
    std::string name;          ///< from the cell database ("" unknown)
    std::string colour;        ///< "#rrggbb" from the cell database ("" automatic)
    double level = std::nan("");  ///< dBm (dBFS when uncalibrated); NaN none
    bool calibrated = false;
    std::optional<double> c1;  ///< dB
    bool c1_estimated = false; ///< estimated by Yantra (RF-06)
    std::optional<measure::QualityValue> quality;  ///< the first quality value
    std::vector<FieldText> large;       ///< la, mcc, mnc ... (UI-01)
    std::vector<FieldText> parameters;  ///< broadcast parameters (SYSTEM_INFO fields)
    std::uint32_t flags = 0;   ///< sample_flag bits
    std::string flags_text;
    ListState list = ListState::MeasuredOnly;  ///< neighbours (UI-03)
    std::int64_t age_ns = 0;
};

/// What the panels show at one instant.
struct SurveyFrame {
    bool valid = false;
    std::int64_t t_ns = 0;     ///< cycle time (pipeline clock)
    std::int64_t utc_ns = 0;
    std::uint64_t sequence = 0;
    bool at_cursor = false;    ///< the graph cursor's time, not the newest
    std::optional<CellRow> serving;
    std::vector<CellRow> neighbours;  ///< measured and listed (UI-03), strongest first
    std::vector<CellRow> scan;        ///< scan channels and other carriers (scanner view), by frequency
    std::uint64_t samples = 0;        ///< cycles with a serving level so far (UI-01 sample counter)
    std::uint32_t flags = 0;
};

/// A cell the graphs know (UI-04).
struct TraceCell {
    std::string key;
    std::string label;   ///< name, else the identity, else the frequency
    std::string colour;  ///< "#rrggbb" from the cell database ("" = palette by `index`)
    std::size_t index = 0;
};

struct TracePoint {
    std::uint16_t cell = 0;
    float level = 0.0f;
    float c1 = std::nanf("");
};

struct HistorySample {
    std::int64_t t_ns = 0;
    std::int64_t utc_ns = 0;
    int serving = -1;       ///< trace cell of the serving carrier, -1 none
    bool sync = false;
    float quality = std::nanf("");  ///< the serving cell's first quality value
    std::vector<TracePoint> points; ///< serving first, at most kMaxPlotCells
};

/// A threshold line the graphs can draw (UI-04): a broadcast field of the
/// serving record in dBm (level graph) or dB (C1 graph).
struct ThresholdLine {
    std::string name;
    std::string label;
    std::string unit;
    double value = 0.0;
    bool level_graph = true;  ///< dBm: the level graph; dB: the C1 graph
};

enum class EventKind {
    ServingChange,
    SyncLost,
    SyncBack,
    DeviceLost,
    DeviceBack,
    DataLoss,
    Interference,      ///< NFR-07 warning on
    InterferenceGone,  ///< NFR-07 warning off
    Radio,             ///< radio started, stopped or failed
    Marker,            ///< REC-05
    VoiceNote,         ///< REC-06
    Recording,         ///< recording started, stopped, new file
};
const char* event_kind_label(EventKind k) noexcept;

struct SurveyEvent {
    std::int64_t t_ns = 0;
    std::int64_t utc_ns = 0;
    EventKind kind = EventKind::Radio;
    std::string text;
    bool warning = false;
};

/// UI-11.
struct SurveyStats {
    std::uint64_t cycles = 0;
    std::uint64_t serving_changes = 0;
    std::uint64_t sync_losses = 0;
    double seconds_in_coverage = 0.0;   ///< serving in sync at or above the threshold
    double seconds_out_of_coverage = 0.0;
    double percent_above = 0.0;         ///< of the cycles with a serving level
    double threshold_dbm = -95.0;
    std::uint64_t records_dropped = 0;  ///< reported by the radios (RAD-24)
    std::uint64_t data_loss_cycles = 0;
};

/// What the graphs need, copied for one frame.
struct PlotData {
    std::vector<TraceCell> cells;
    std::vector<HistorySample> samples;  ///< oldest first
    std::vector<ThresholdLine> thresholds;
    std::uint64_t version = 0;
};

class SurveyMonitor final : public measure::MeasurementObserver {
public:
    struct Options {
        std::size_t history = 36000;   ///< graph samples kept (an hour at 10 Hz)
        std::size_t frames = 3000;     ///< cycles kept for the cursor (10 min at 0.2 s)
        std::size_t events = 2000;
        double threshold_dbm = -95.0;  ///< coverage threshold (UI-11)
        measure::SelfInterferenceOptions interference;
    };
    SurveyMonitor();
    explicit SurveyMonitor(Options options);
    ~SurveyMonitor() override;
    SurveyMonitor(const SurveyMonitor&) = delete;
    SurveyMonitor& operator=(const SurveyMonitor&) = delete;

    /// Cell names and colours (UI-02/03/04); may change at any time.
    void set_cell_database(std::shared_ptr<const celldb::CellDatabase> db);
    void set_threshold(double dbm);
    /// The noise with the antenna away from the PC as the NFR-07 reference (nullopt: thermal floor).
    void set_interference_reference(std::optional<double> noise_dbm);
    std::optional<double> interference_reference() const;

    /// Forgets everything (a new source).
    void clear();
    /// Replay: the history, events and statistics come from a pass over
    /// the whole recording (frozen); later cycles only set the frame.
    void freeze_history(bool frozen);
    bool history_frozen() const;
    /// Replay seek: the frame is this cycle (nullopt: none yet).
    void show_cycle(const std::optional<measure::MeasurementCycle>& cycle);

    /// UI-04 cursor: panels show the kept cycle at or before `t` (live);
    /// nullopt follows the newest. Replays seek instead (ReplaySession).
    void set_cursor(std::optional<std::int64_t> t_ns);
    std::optional<std::int64_t> cursor() const;

    /// An event from outside the pipeline (markers, voice notes, recording, radio).
    void note(EventKind kind, const std::string& text, std::int64_t t_ns = 0, bool warning = false);

    // -- reading (UI thread) ---------------------------------------------
    SurveyFrame frame() const;
    PlotData plot() const;
    /// Only the version: graphs copy plot() when it changed.
    std::uint64_t version() const;
    std::vector<SurveyEvent> events() const;
    SurveyStats stats() const;
    measure::SelfInterferenceWarning interference() const;
    /// The newest cycle's time (0 none).
    std::int64_t last_time() const;
    /// The newest cycle's flags (sample_flag bits; 0 none), cheap (status bar).
    std::uint32_t last_flags() const;

    // -- measure::MeasurementObserver ---------------------------------------
    void on_event(const measure::RadioEvent& event) override;
    void on_cycle(const measure::MeasurementCycle& cycle) override;

private:
    struct History;
    void add_history(History& h, const measure::MeasurementCycle& c, const std::optional<CellRow>& serving,
                     const std::vector<CellRow>& rows);
    SurveyFrame build(const measure::MeasurementCycle& c) const;
    CellRow row(const measure::CarrierSample& s) const;
    std::string cell_name(const measure::MappedRecord& r, std::string* colour) const;

    Options options_;
    mutable std::mutex mutex_;
    std::shared_ptr<const celldb::CellDatabase> db_;
    std::deque<measure::MeasurementCycle> frames_;
    std::optional<measure::MeasurementCycle> shown_;  // replay: the cycle at the player's position
    std::optional<std::int64_t> cursor_;
    bool frozen_ = false;
    std::unique_ptr<History> history_;
    measure::SelfInterferenceMonitor interference_;
    std::uint64_t samples_ = 0;
};

/// The cell database entry a measured record pairs with (by the
/// database's match fields for its technology, else the frequency);
/// nullptr when none.
const celldb::CellEntry* match_cell(const celldb::CellDatabase& db, const measure::MappedRecord& record);

// -- graph arithmetic (Yantra's own plotting, UI-13) -------------------------
// Copied in part from yantra-v1 c24e8f5: app/core/plot_math.h/.cpp
// (nice ticks, time ticks, autoscale), adapted.

struct Range {
    double lo = 0.0, hi = 1.0;
    double span() const { return hi - lo; }
};
/// Multiples of a 1/2/5 x 10^n step inside [lo, hi], at most about max_ticks.
std::vector<double> nice_ticks(double lo, double hi, int max_ticks);
/// [min, max] padded by 5 % and widened to min_span, rounded out to the
/// tick step; `fallback` without finite data.
Range autoscale(double min, double max, double min_span, Range fallback);
/// Time-axis step (ns) from 1/2/5/10/15/30 s, 1/2/5/10/15/30 min, 1/2/3/6/12/24 h.
std::int64_t time_step_ns(std::int64_t span_ns, int max_ticks);
/// The graph colour of a trace cell: its database colour, else a palette
/// entry by index (12 hues readable on dark and light backgrounds).
std::uint32_t trace_colour(const TraceCell& c, bool light_background);  ///< 0xAARRGGBB
/// "#rrggbb" -> 0xFFRRGGBB; nullopt when malformed.
std::optional<std::uint32_t> parse_colour(const std::string& hex);

}  // namespace yantra::app
