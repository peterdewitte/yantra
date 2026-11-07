// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The survey panels' model, see survey_monitor.h.
#include "yantra/app/survey_monitor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

#include <yantra/radio.h>

#include "yantra/core/celldb/network.h"
#include "yantra/core/plugin/record.h"

namespace yantra::app {

namespace ym = yantra::measure;
namespace sf = yantra::measure::sample_flag;

const char* list_state_label(ListState s) noexcept {
    switch (s) {
    case ListState::Both: return "listed and received";
    case ListState::MeasuredOnly: return "received, not in the neighbour list";
    case ListState::ListedOnly: return "listed, not received";
    }
    return "";
}

const char* event_kind_label(EventKind k) noexcept {
    switch (k) {
    case EventKind::ServingChange: return "Serving cell";
    case EventKind::SyncLost: return "Sync lost";
    case EventKind::SyncBack: return "Sync";
    case EventKind::DeviceLost: return "Device lost";
    case EventKind::DeviceBack: return "Device back";
    case EventKind::DataLoss: return "Data loss";
    case EventKind::Interference: return "Self-interference";
    case EventKind::InterferenceGone: return "Self-interference";
    case EventKind::Radio: return "Radio";
    case EventKind::Marker: return "Marker";
    case EventKind::VoiceNote: return "Voice note";
    case EventKind::Recording: return "Recording";
    }
    return "";
}

namespace {

bool is_large(const std::string& n) { return n == "la" || n == "mcc" || n == "mnc" || n == "colour_code"; }

std::string with_unit(const std::string& v, const std::string& unit) {
    return unit.empty() || v.empty() ? v : v + " " + unit;
}

std::optional<double> number(const plugin::Value& v) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    return std::nullopt;
}

std::string frequency_text(std::uint64_t hz) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f MHz", static_cast<double>(hz) / 1e6);
    return b;
}

celldb::FieldMap received_fields(const ym::MappedRecord& r) {
    celldb::FieldMap m;
    if (r.schema)
        for (std::size_t i = 0; i < r.schema->fields.size() && i < r.record.values.size(); ++i)
            if (auto v = celldb::from_record_value(r.record.values[i])) m[r.schema->fields[i].name] = *v;
    if (r.frequency_hz != 0) m["frequency_hz"] = static_cast<std::int64_t>(r.frequency_hz);
    return m;
}

}  // namespace

const celldb::CellEntry* match_cell(const celldb::CellDatabase& db, const ym::MappedRecord& r) {
    const std::string& tech = r.provenance.technology_id;
    std::vector<std::string> fields = db.match_fields(tech);
    if (fields.empty()) fields = {"frequency_hz"};
    const celldb::FieldMap got = received_fields(r);
    for (const celldb::CellEntry& e : db.entries()) {
        if (!e.cell.technology_id.empty() && !tech.empty() && e.cell.technology_id != tech) continue;
        if (celldb::fields_pair(e.fields(), got, fields)) return &e;
    }
    return nullptr;
}

// -- history ------------------------------------------------------------------------------

struct SurveyMonitor::History {
    std::vector<TraceCell> cells;
    std::map<std::string, std::uint16_t> index;
    std::deque<HistorySample> samples;
    std::vector<ThresholdLine> thresholds;
    std::deque<SurveyEvent> events;
    SurveyStats stats;
    ym::SelfInterferenceMonitor interference;
    std::uint64_t version = 0;
    // The previous cycle.
    std::optional<std::string> serving_key;
    std::string serving_label;
    bool sync = false;
    bool device_lost = false;
    bool data_loss = false;
    bool warned = false;
    std::uint64_t level_cycles = 0, above = 0;

    explicit History(const ym::SelfInterferenceOptions& o) : interference(o) {}

    std::uint16_t intern(const CellRow& r) {
        const auto it = index.find(r.key);
        if (it != index.end()) {
            TraceCell& c = cells[it->second];
            if (!r.name.empty()) c.label = r.name;
            if (!r.colour.empty()) c.colour = r.colour;
            return it->second;
        }
        TraceCell c;
        c.key = r.key;
        c.label = !r.name.empty() ? r.name : !r.identity.empty() ? r.identity : frequency_text(r.frequency_hz);
        c.colour = r.colour;
        c.index = cells.size();
        cells.push_back(c);
        const auto i = static_cast<std::uint16_t>(cells.size() - 1);
        index[r.key] = i;
        return i;
    }
};

SurveyMonitor::SurveyMonitor() : SurveyMonitor(Options{}) {}

SurveyMonitor::SurveyMonitor(Options options)
    : options_(options), history_(std::make_unique<History>(options.interference)), interference_(options.interference) {
    history_->stats.threshold_dbm = options_.threshold_dbm;
}

SurveyMonitor::~SurveyMonitor() = default;

void SurveyMonitor::set_cell_database(std::shared_ptr<const celldb::CellDatabase> db) {
    const std::lock_guard lock(mutex_);
    db_ = std::move(db);
}

void SurveyMonitor::set_threshold(double dbm) {
    const std::lock_guard lock(mutex_);
    options_.threshold_dbm = dbm;
    history_->stats.threshold_dbm = dbm;
}

void SurveyMonitor::set_interference_reference(std::optional<double> noise_dbm) {
    const std::lock_guard lock(mutex_);
    interference_.set_reference(noise_dbm);
    history_->interference.set_reference(noise_dbm);
}

std::optional<double> SurveyMonitor::interference_reference() const {
    const std::lock_guard lock(mutex_);
    return interference_.reference();
}

void SurveyMonitor::clear() {
    const std::lock_guard lock(mutex_);
    const auto reference = interference_.reference();
    const std::uint64_t version = history_->version + 1;
    history_ = std::make_unique<History>(options_.interference);
    history_->stats.threshold_dbm = options_.threshold_dbm;
    history_->interference.set_reference(reference);
    history_->version = version;
    interference_.reset();
    frames_.clear();
    if (observations_) observations_ = std::make_unique<celldb::Observations>(observations_->technologies());
    shown_.reset();
    cursor_.reset();
    frozen_ = false;
    samples_ = 0;
}

void SurveyMonitor::freeze_history(bool frozen) {
    const std::lock_guard lock(mutex_);
    frozen_ = frozen;
}

bool SurveyMonitor::history_frozen() const {
    const std::lock_guard lock(mutex_);
    return frozen_;
}

void SurveyMonitor::show_cycle(const std::optional<ym::MeasurementCycle>& cycle) {
    const std::lock_guard lock(mutex_);
    shown_ = cycle;
    if (cycle && observations_) observations_->add(*cycle);
    interference_.reset();
}

void SurveyMonitor::set_technologies(celldb::Technologies technologies) {
    const std::lock_guard lock(mutex_);
    observations_ = std::make_unique<celldb::Observations>(std::move(technologies));
}

std::vector<celldb::ObservedCell> SurveyMonitor::observed_cells() const {
    const std::lock_guard lock(mutex_);
    return observations_ ? observations_->cells() : std::vector<celldb::ObservedCell>{};
}

void SurveyMonitor::set_cursor(std::optional<std::int64_t> t_ns) {
    const std::lock_guard lock(mutex_);
    cursor_ = t_ns;
}

std::optional<std::int64_t> SurveyMonitor::cursor() const {
    const std::lock_guard lock(mutex_);
    return cursor_;
}

void SurveyMonitor::note(EventKind kind, const std::string& text, std::int64_t t_ns, bool warning) {
    const std::lock_guard lock(mutex_);
    if (t_ns == 0) t_ns = frames_.empty() ? 0 : frames_.back().time.monotonic_ns;
    SurveyEvent e{t_ns, 0, kind, text, warning};
    if (!frames_.empty()) e.utc_ns = frames_.back().time.utc_ns + (t_ns - frames_.back().time.monotonic_ns);
    // A replay's markers and voice notes are in the list from the pass over
    // the recording already.
    if (frozen_) return;
    history_->events.push_back(std::move(e));
    while (history_->events.size() > options_.events) history_->events.pop_front();
    ++history_->version;
}

// -- rows ---------------------------------------------------------------------------------

std::string SurveyMonitor::cell_name(const ym::MappedRecord& r, std::string* colour) const {
    if (!db_) return {};
    const celldb::CellEntry* e = match_cell(*db_, r);
    if (e == nullptr) return {};
    if (colour) *colour = e->colour;
    return e->display_name();
}

CellRow SurveyMonitor::row(const ym::CarrierSample& s) const {
    const ym::MappedRecord& r = s.latest;
    CellRow c;
    c.device_key = r.device_key;
    c.role = r.role;
    c.frequency_hz = r.frequency_hz;
    c.key = r.device_key + "|" + std::to_string(r.frequency_hz);
    c.level = r.level;
    c.calibrated = (s.flags & sf::kUncalibrated) == 0;
    c.flags = s.flags;
    c.flags_text = ym::sample_flags_text(s.flags);
    c.age_ns = s.age_ns;
    c.c1_estimated = r.c1_source == ym::C1Source::Estimated;
    if (!r.quality.empty()) c.quality = r.quality.front();
    if (r.schema) {
        const plugin::Schema& sc = *r.schema;
        if (!sc.key_fields().empty() && r.record.values.size() == sc.fields.size())
            c.identity = plugin::record_key(r.record, sc);
        for (std::size_t i = 0; i < sc.fields.size() && i < r.record.values.size(); ++i) {
            const plugin::Field& f = sc.fields[i];
            const plugin::Value& v = r.record.values[i];
            if (std::holds_alternative<std::monostate>(v)) continue;
            if (f.name == "c1_db") {
                c.c1 = number(v);
                continue;
            }
            if (f.name == "channel") c.channel = plugin::value_text(v, f);
            if (f.category != plugin::Category::SystemInfo || f.name == "frequency_hz" || f.name == "role") continue;
            FieldText t{f.name, f.label.empty() ? f.name : f.label, with_unit(plugin::value_text(v, f), f.unit)};
            if (is_large(f.name)) c.large.push_back(t);
            if (f.name != "channel") c.parameters.push_back(std::move(t));
        }
    }
    c.name = cell_name(r, &c.colour);
    return c;
}

SurveyFrame SurveyMonitor::build(const ym::MeasurementCycle& cy) const {
    SurveyFrame f;
    f.valid = true;
    f.t_ns = cy.time.monotonic_ns;
    f.utc_ns = cy.time.utc_ns;
    f.sequence = cy.sequence;
    f.flags = cy.flags;
    f.samples = samples_;
    if (const ym::CarrierSample* s = cy.serving()) f.serving = row(*s);
    std::vector<CellRow> measured, listed;
    for (const ym::CarrierSample& s : cy.samples) {
        switch (s.latest.role) {
        case ym::CarrierRole::Neighbour: measured.push_back(row(s)); break;
        case ym::CarrierRole::NeighbourEntry: listed.push_back(row(s)); break;
        case ym::CarrierRole::ScanChannel:
        case ym::CarrierRole::Other: f.scan.push_back(row(s)); break;
        case ym::CarrierRole::Serving:
            if (f.serving && f.serving->key != s.latest.device_key + "|" + std::to_string(s.latest.frequency_hz))
                f.scan.push_back(row(s));  // the serving carrier of another device
            break;
        }
    }
    // UI-03: a measured neighbour in the list is Both; list entries not
    // received are ListedOnly (no level).
    for (CellRow& m : measured) {
        const auto it = std::find_if(listed.begin(), listed.end(), [&](const CellRow& l) {
            return l.frequency_hz == m.frequency_hz && l.device_key == m.device_key;
        });
        if (it != listed.end()) {
            m.list = ListState::Both;
            for (FieldText& p : it->parameters)
                if (std::none_of(m.parameters.begin(), m.parameters.end(), [&](const FieldText& q) { return q.name == p.name; }))
                    m.parameters.push_back(p);
            if (m.name.empty()) m.name = it->name;
            listed.erase(it);
        }
    }
    for (CellRow& l : listed) {
        l.list = ListState::ListedOnly;
        l.level = std::nan("");
    }
    std::stable_sort(measured.begin(), measured.end(), [](const CellRow& a, const CellRow& b) {
        const double x = std::isnan(a.level) ? -1e9 : a.level, y = std::isnan(b.level) ? -1e9 : b.level;
        return x > y;
    });
    f.neighbours = std::move(measured);
    for (CellRow& l : listed) f.neighbours.push_back(std::move(l));
    std::stable_sort(f.scan.begin(), f.scan.end(),
                     [](const CellRow& a, const CellRow& b) { return a.frequency_hz < b.frequency_hz; });
    return f;
}

// -- input --------------------------------------------------------------------------------

void SurveyMonitor::on_event(const ym::RadioEvent& e) {
    const std::lock_guard lock(mutex_);
    if (frozen_) return;
    History& h = *history_;
    const std::int64_t t = e.monotonic_ns;
    auto add = [&](EventKind k, std::string text, bool warning) {
        h.events.push_back({t, 0, k, std::move(text), warning});
        while (h.events.size() > options_.events) h.events.pop_front();
        ++h.version;
    };
    using K = ym::RadioEvent::Kind;
    switch (e.kind) {
    case K::Stream:
        if (e.event == YR_EVENT_DROPPED && e.value > 0) h.stats.records_dropped += static_cast<std::uint64_t>(e.value);
        break;
    case K::Started: add(EventKind::Radio, "Radio " + e.device_key + " started.", false); break;
    case K::Stopped: add(EventKind::Radio, "Radio " + e.device_key + " stopped." + (e.message.empty() ? "" : " " + e.message), false); break;
    case K::Lost: add(EventKind::DeviceLost, "Radio " + e.device_key + " unplugged: waiting for it.", true); break;
    case K::Reopened: add(EventKind::DeviceBack, "Radio " + e.device_key + " is back.", false); break;
    case K::Failed: add(EventKind::Radio, "Radio plugin " + e.plugin_id + " failed: " + e.message, true); break;
    }
}

void SurveyMonitor::add_history(History& h, const ym::MeasurementCycle& c, const std::optional<CellRow>& serving,
                                const std::vector<CellRow>& rows) {
    const std::int64_t t = c.time.monotonic_ns;
    auto add = [&](EventKind k, std::string text, bool warning) {
        h.events.push_back({t, c.time.utc_ns, k, std::move(text), warning});
        while (h.events.size() > options_.events) h.events.pop_front();
    };
    HistorySample s;
    s.t_ns = t;
    s.utc_ns = c.time.utc_ns;
    const bool has_level = serving && !std::isnan(serving->level);
    s.sync = serving && (serving->flags & sf::kNoSync) == 0;
    if (serving) {
        s.serving = h.intern(*serving);
        if (serving->quality) s.quality = static_cast<float>(serving->quality->value);
        s.points.push_back({static_cast<std::uint16_t>(s.serving), static_cast<float>(serving->level),
                            serving->c1 ? static_cast<float>(*serving->c1) : std::nanf("")});
    }
    for (const CellRow& r : rows) {
        if (s.points.size() >= kMaxPlotCells) break;
        if (r.list == ListState::ListedOnly || std::isnan(r.level)) continue;
        s.points.push_back({h.intern(r), static_cast<float>(r.level), r.c1 ? static_cast<float>(*r.c1) : std::nanf("")});
    }
    h.samples.push_back(std::move(s));
    while (h.samples.size() > options_.history) h.samples.pop_front();

    // Events (serving changes, sync, device, data loss).
    ++h.stats.cycles;
    if (serving) {
        const std::string label = !serving->name.empty() ? serving->name
                                  : !serving->identity.empty() ? serving->identity
                                                               : frequency_text(serving->frequency_hz);
        const std::string key = serving->key + "#" + serving->identity;
        if (h.serving_key && *h.serving_key != key) {
            ++h.stats.serving_changes;
            add(EventKind::ServingChange, "Serving cell " + h.serving_label + " -> " + label + ".", false);
        }
        h.serving_key = key;
        h.serving_label = label;
    }
    const bool sync = serving && (serving->flags & sf::kNoSync) == 0;
    if (h.sync && !sync) {
        ++h.stats.sync_losses;
        add(EventKind::SyncLost, serving ? "Sync lost on " + h.serving_label + "." : std::string("The serving cell is gone."), true);
    } else if (!h.sync && sync && h.stats.cycles > 1) {
        add(EventKind::SyncBack, "In sync with " + h.serving_label + ".", false);
    }
    h.sync = sync;
    const bool lost = (c.flags & sf::kDeviceLost) != 0;
    if (lost && !h.device_lost) add(EventKind::DeviceLost, "The radio was unplugged.", true);
    if (!lost && h.device_lost) add(EventKind::DeviceBack, "The radio is back.", false);
    h.device_lost = lost;
    const bool loss = (c.flags & sf::kDataLoss) != 0;
    if (loss) ++h.stats.data_loss_cycles;
    if (loss && !h.data_loss) add(EventKind::DataLoss, "The radio dropped records (RAD-24).", true);
    h.data_loss = loss;
    // Statistics (UI-11).
    const double period = static_cast<double>(c.period_ns) / 1e9;
    const bool covered = sync && has_level && serving->level >= options_.threshold_dbm;
    (covered ? h.stats.seconds_in_coverage : h.stats.seconds_out_of_coverage) += period;
    if (has_level) {
        ++h.level_cycles;
        if (serving->level >= options_.threshold_dbm) ++h.above;
    }
    h.stats.percent_above = h.level_cycles > 0 ? 100.0 * static_cast<double>(h.above) / static_cast<double>(h.level_cycles) : 0.0;
    h.stats.threshold_dbm = options_.threshold_dbm;
    // NFR-07.
    const ym::SelfInterferenceWarning& w = h.interference.on_cycle(c);
    if (w.active && !h.warned) add(EventKind::Interference, w.text, true);
    if (!w.active && h.warned) add(EventKind::InterferenceGone, "The self-interference warning is off.", false);
    h.warned = w.active;
    // Threshold lines: the serving record's dBm and dB broadcast fields (UI-04).
    if (const ym::CarrierSample* sv = c.serving(); sv && sv->latest.schema) {
        h.thresholds.clear();
        const plugin::Schema& sc = *sv->latest.schema;
        for (std::size_t i = 0; i < sc.fields.size() && i < sv->latest.record.values.size(); ++i) {
            const plugin::Field& f = sc.fields[i];
            if (f.category != plugin::Category::SystemInfo || (f.unit != "dBm" && f.unit != "dB")) continue;
            if (const auto v = number(sv->latest.record.values[i]))
                h.thresholds.push_back({f.name, f.label.empty() ? f.name : f.label, f.unit, *v, f.unit == "dBm"});
        }
    }
    ++h.version;
}

void SurveyMonitor::on_cycle(const ym::MeasurementCycle& c) {
    const std::lock_guard lock(mutex_);
    const ym::CarrierSample* sv = c.serving();
    if (sv && !std::isnan(sv->latest.level)) ++samples_;
    (void)interference_.on_cycle(c);
    if (observations_) observations_->add(c);
    if (frozen_) {
        shown_ = c;
        return;
    }
    frames_.push_back(c);
    while (frames_.size() > options_.frames) frames_.pop_front();
    const SurveyFrame f = build(c);
    add_history(*history_, c, f.serving, f.neighbours);
}

// -- reading ------------------------------------------------------------------------------

SurveyFrame SurveyMonitor::frame() const {
    const std::lock_guard lock(mutex_);
    if (frozen_) return shown_ ? build(*shown_) : SurveyFrame{};
    if (frames_.empty()) return {};
    if (cursor_) {
        const ym::MeasurementCycle* best = &frames_.front();
        for (const ym::MeasurementCycle& c : frames_)
            if (c.time.monotonic_ns <= *cursor_) best = &c;
        SurveyFrame f = build(*best);
        f.at_cursor = true;
        return f;
    }
    return build(frames_.back());
}

PlotData SurveyMonitor::plot() const {
    const std::lock_guard lock(mutex_);
    PlotData p;
    p.cells = history_->cells;
    p.samples.assign(history_->samples.begin(), history_->samples.end());
    p.thresholds = history_->thresholds;
    p.version = history_->version;
    return p;
}

std::uint64_t SurveyMonitor::version() const {
    const std::lock_guard lock(mutex_);
    return history_->version;
}

std::vector<SurveyEvent> SurveyMonitor::events() const {
    const std::lock_guard lock(mutex_);
    return {history_->events.begin(), history_->events.end()};
}

SurveyStats SurveyMonitor::stats() const {
    const std::lock_guard lock(mutex_);
    return history_->stats;
}

ym::SelfInterferenceWarning SurveyMonitor::interference() const {
    const std::lock_guard lock(mutex_);
    return interference_.warning();
}

std::int64_t SurveyMonitor::last_time() const {
    const std::lock_guard lock(mutex_);
    if (frozen_) return history_->samples.empty() ? 0 : history_->samples.back().t_ns;
    return frames_.empty() ? 0 : frames_.back().time.monotonic_ns;
}

std::uint32_t SurveyMonitor::last_flags() const {
    const std::lock_guard lock(mutex_);
    if (frozen_) return shown_ ? shown_->flags : 0;
    return frames_.empty() ? 0 : frames_.back().flags;
}

// -- graph arithmetic ---------------------------------------------------------------------

namespace {

double nice_step(double span, int max_ticks) {
    if (!(span > 0) || !std::isfinite(span)) return 1.0;
    const double raw = span / std::max(max_ticks, 1);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0})
        if (m * mag >= raw * (1 - 1e-9)) return m * mag;
    return 10 * mag;
}

}  // namespace

std::vector<double> nice_ticks(double lo, double hi, int max_ticks) {
    std::vector<double> out;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return out;
    const double step = nice_step(hi - lo, max_ticks);
    for (double v = std::ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9 && out.size() < 1000; v += step)
        out.push_back(std::fabs(v) < step * 1e-9 ? 0.0 : v);
    return out;
}

Range autoscale(double min, double max, double min_span, Range fallback) {
    if (!std::isfinite(min) || !std::isfinite(max) || max < min) return fallback;
    double span = max - min;
    if (span < min_span) {
        const double mid = (min + max) / 2;
        min = mid - min_span / 2;
        max = mid + min_span / 2;
        span = min_span;
    }
    min -= span * 0.05;
    max += span * 0.05;
    const double step = nice_step(max - min, 6);
    return Range{std::floor(min / step) * step, std::ceil(max / step) * step};
}

std::int64_t time_step_ns(std::int64_t span_ns, int max_ticks) {
    static constexpr std::array<std::int64_t, 18> ladder = {1,   2,   5,    10,   15,   30,    60,    120,   300,
                                                            600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400};
    max_ticks = std::max(max_ticks, 1);
    for (const std::int64_t s : ladder)
        if (s * 1'000'000'000 * max_ticks >= span_ns) return s * 1'000'000'000;
    return ladder.back() * 1'000'000'000;
}

std::optional<std::uint32_t> parse_colour(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#') return std::nullopt;
    std::uint32_t v = 0;
    for (std::size_t i = 1; i < 7; ++i) {
        const char c = hex[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return std::nullopt;
        v = v << 4 | static_cast<std::uint32_t>(d);
    }
    return 0xFF000000u | v;
}

std::uint32_t trace_colour(const TraceCell& c, bool light) {
    if (const auto v = parse_colour(c.colour)) return *v;
    // Each at least 3:1 against its window background (yantra-v1 plot_palette).
    static constexpr std::array<std::uint32_t, 12> dark = {0x4DABF7, 0xFF922B, 0x69DB7C, 0xFF6B6B, 0xCC99FF, 0xFFD43B,
                                                           0x38D9D9, 0xF783AC, 0xA9E34B, 0xFFA878, 0x91A7FF, 0xE0E0E0};
    static constexpr std::array<std::uint32_t, 12> bright = {0x1965C1, 0xCC5500, 0x248038, 0xC92A2A, 0x843FC4, 0x8C6900,
                                                             0x0C7A80, 0xB02466, 0x567800, 0x9C481E, 0x4254C4, 0x505050};
    const auto& p = light ? bright : dark;
    return 0xFF000000u | p[c.index % p.size()];
}

}  // namespace yantra::app
