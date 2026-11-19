// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio windows' model, see radio_windows.h.
#include "yantra/app/radio_windows.h"
#include "yantra/core/platform/thread_name.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <variant>

#include <yantra/radio.h>

#include "yantra/core/calibration/table.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/measure/self_interference.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/plugin/state_store.h"
#include "yantra/core/platform/time.h"

namespace yantra::app {

namespace fs = std::filesystem;
using plugin::Value;
using radio::SettingDef;

const char* radio_window_id(RadioWindow w) noexcept {
    switch (w) {
    case RadioWindow::List: return "radio.list";
    case RadioWindow::Settings: return "radio.settings";
    case RadioWindow::Debug: return "radio.debug";
    case RadioWindow::Calibration: return "radio.calibration";
    case RadioWindow::Info: return "radio.info";
    }
    return "radio.list";
}

const char* radio_window_title(RadioWindow w) noexcept {
    switch (w) {
    case RadioWindow::List: return tr("Radios");
    case RadioWindow::Settings: return tr("Radio settings");
    case RadioWindow::Debug: return tr("Radio debug");
    case RadioWindow::Calibration: return tr("Calibration");
    case RadioWindow::Info: return tr("Device info and self-test");
    }
    return "";
}

// -- generated settings -------------------------------------------------------------------

namespace {

bool is_number(std::uint32_t t) { return t == YR_TYPE_INT || t == YR_TYPE_UINT || t == YR_TYPE_DOUBLE; }

std::optional<double> number_of(const Value& v) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    return std::nullopt;
}

std::string number_text(double x) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", x);
    return b;
}

bool same_value(const Value& a, const Value& b) {
    const auto x = number_of(a), y = number_of(b);
    if (x && y) return std::fabs(*x - *y) <= 1e-9 * std::max(1.0, std::fabs(*x));
    return a == b;
}

}  // namespace

SettingWidget widget_of(const SettingDef& d) noexcept {
    switch (d.type) {
    case YR_TYPE_BOOL: return SettingWidget::Checkbox;
    case YR_TYPE_ENUM: return SettingWidget::Combo;
    case YR_TYPE_STRING: return SettingWidget::Text;
    case YR_TYPE_ACTION: return SettingWidget::Button;
    default:
        return std::isfinite(d.minimum) && std::isfinite(d.maximum) && d.maximum > d.minimum ? SettingWidget::Slider
                                                                                             : SettingWidget::Number;
    }
}

std::string validate_setting(const SettingDef& d, const Value& v) {
    switch (d.type) {
    case YR_TYPE_ACTION: return tr("An action has no value.");
    case YR_TYPE_BOOL:
        return std::holds_alternative<bool>(v) ? std::string() : tr("Choose on or off.");
    case YR_TYPE_STRING: {
        const auto* s = std::get_if<std::string>(&v);
        if (s == nullptr) return tr("Enter a text.");
        if (s->size() > YR_MAX_STRING_VALUE_BYTES)
            return tr("At most ") + std::to_string(YR_MAX_STRING_VALUE_BYTES) + " bytes.";
        return {};
    }
    case YR_TYPE_ENUM: {
        const auto* e = std::get_if<plugin::EnumValue>(&v);
        if (e == nullptr) return tr("Choose one of the items.");
        for (const radio::EnumItem& i : d.items)
            if (i.value == e->value) return {};
        return tr("Choose one of the items.");
    }
    default: break;
    }
    if (!is_number(d.type)) return tr("Unknown setting type.");
    const bool type_ok = (d.type == YR_TYPE_DOUBLE && std::holds_alternative<double>(v)) ||
                         (d.type == YR_TYPE_INT && std::holds_alternative<std::int64_t>(v)) ||
                         (d.type == YR_TYPE_UINT && std::holds_alternative<std::uint64_t>(v));
    if (!type_ok) return d.type == YR_TYPE_DOUBLE ? tr("Enter a number.") : tr("Enter a whole number.");
    const double x = *number_of(v);
    if (!std::isfinite(x)) return tr("Enter a finite number.");
    const std::string unit = d.unit.empty() ? std::string() : " " + d.unit;
    if (std::isfinite(d.minimum) && x < d.minimum - 1e-9 * std::max(1.0, std::fabs(d.minimum)))
        return tr("At least ") + number_text(d.minimum) + unit + ".";
    if (std::isfinite(d.maximum) && x > d.maximum + 1e-9 * std::max(1.0, std::fabs(d.maximum)))
        return tr("At most ") + number_text(d.maximum) + unit + ".";
    if (d.type != YR_TYPE_DOUBLE && d.step > 0.0) {
        const double base = std::isfinite(d.minimum) ? d.minimum : 0.0;
        const double k = (x - base) / d.step;
        if (std::fabs(k - std::round(k)) > 1e-9) return tr("In steps of ") + number_text(d.step) + unit + ".";
    }
    return {};
}

std::string setting_value_text(const SettingDef& d, const Value& v) {
    if (std::holds_alternative<std::monostate>(v)) return {};
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "on" : "off";
    if (const auto* s = std::get_if<std::string>(&v)) return *s;
    if (const auto* e = std::get_if<plugin::EnumValue>(&v)) {
        for (const radio::EnumItem& i : d.items)
            if (i.value == e->value) return i.label.empty() ? i.name : i.label;
        return std::to_string(e->value);
    }
    std::string t;
    if (const auto* i = std::get_if<std::int64_t>(&v)) t = std::to_string(*i);
    else if (const auto* u = std::get_if<std::uint64_t>(&v)) t = std::to_string(*u);
    else if (const auto* x = std::get_if<double>(&v)) t = number_text(*x);
    return d.unit.empty() ? t : t + " " + d.unit;
}

std::string setting_edit_text(const SettingDef& d, const Value& v) {
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return std::to_string(*u);
    if (const auto* x = std::get_if<double>(&v)) {
        char b[64];
        std::snprintf(b, sizeof b, "%.10g", *x);
        return b;
    }
    if (const auto* s = std::get_if<std::string>(&v)) return *s;
    return setting_value_text(d, v);
}

std::optional<Value> parse_setting(const SettingDef& d, const std::string& text, std::string& error) {
    error.clear();
    Value v;
    if (d.type == YR_TYPE_STRING) {
        v = text;
    } else if (is_number(d.type)) {
        const auto a = text.find_first_not_of(" \t");
        const auto b = text.find_last_not_of(" \t");
        const std::string t = a == std::string::npos ? std::string() : text.substr(a, b - a + 1);
        if (t.empty()) {
            error = tr("Enter a value.");
            return std::nullopt;
        }
        errno = 0;
        char* end = nullptr;
        if (d.type == YR_TYPE_DOUBLE) v = std::strtod(t.c_str(), &end);
        else if (d.type == YR_TYPE_INT) v = static_cast<std::int64_t>(std::strtoll(t.c_str(), &end, 10));
        else if (t[0] != '-') v = static_cast<std::uint64_t>(std::strtoull(t.c_str(), &end, 10));
        if (end == nullptr || *end != '\0' || errno == ERANGE) {
            error = d.type == YR_TYPE_DOUBLE ? tr("Enter a number.") : tr("Enter a whole number.");
            if (d.type == YR_TYPE_UINT && t[0] == '-') error = tr("Enter a whole number of 0 or more.");
            return std::nullopt;
        }
    } else {
        error = tr("This setting has no input field.");
        return std::nullopt;
    }
    error = validate_setting(d, v);
    if (!error.empty()) return std::nullopt;
    return v;
}

std::vector<SettingGroup> group_settings(const std::vector<SettingDef>& defs, bool advanced, std::size_t* hidden) {
    std::vector<SettingGroup> out;
    std::size_t n = 0;
    for (const SettingDef& d : defs) {
        if (d.has(YR_SETTING_ADVANCED) && !advanced) {
            ++n;
            continue;
        }
        auto it = std::find_if(out.begin(), out.end(), [&](const SettingGroup& g) { return g.name == d.group; });
        if (it == out.end()) {
            out.push_back(SettingGroup{d.group, {}});
            it = out.end() - 1;
        }
        it->settings.push_back(&d);
    }
    for (SettingGroup& g : out)
        std::stable_sort(g.settings.begin(), g.settings.end(),
                         [](const SettingDef* a, const SettingDef* b) { return a->sort_order < b->sort_order; });
    if (hidden != nullptr) *hidden = n;
    return out;
}

SettingDef translate_setting(SettingDef d, const std::string& prefix,
                             const std::function<std::string(const std::string&)>& tr) {
    if (!tr) return d;
    const std::string base = prefix + "." + d.key + ".";
    auto use = [&](std::string& field, const char* part) {
        if (std::string t = tr(base + part); !t.empty()) field = std::move(t);
    };
    use(d.label, "label");
    use(d.help, "help");
    use(d.warning, "warning");
    use(d.unavailable_reason, "unavailable");
    for (radio::EnumItem& i : d.items)
        if (std::string t = tr(base + "item." + i.name); !t.empty()) i.label = std::move(t);
    return d;
}

// -- the pipeline tap: records, events, cycles of the open device ---------------------------

namespace {

constexpr std::int64_t kRateWindowNs = 10'000'000'000;
constexpr double kTuneToleranceHz = 12'500.0;  // a reading belongs to the target carrier

enum class Collect { None, Level, FreqError, Noise };

const char* event_name(std::uint32_t e) {
    switch (e) {
    case YR_EVENT_SYNC_FOUND: return tr("sync found");
    case YR_EVENT_SYNC_LOST: return tr("sync lost");
    case YR_EVENT_SERVING_CHANGED: return tr("serving changed");
    case YR_EVENT_OVERFLOW: return "overflow";
    case YR_EVENT_DROPPED: return "dropped";
    case YR_EVENT_PLAN_APPLIED: return tr("plan applied");
    case YR_EVENT_SCAN_DONE: return tr("scan done");
    case YR_EVENT_END_OF_STREAM: return tr("end of stream");
    case YR_EVENT_DEVICE_LOST: return tr("device lost");
    case YR_EVENT_DEVICE_BACK: return tr("device back");
    case YR_EVENT_ERROR: return "error";
    default: return "event";
    }
}

struct Tap final : measure::MeasurementObserver {
    struct Carrier {
        measure::MappedRecord latest;
        std::deque<std::int64_t> arrivals;
    };

    mutable std::mutex m;
    std::string plugin, key;
    std::map<std::tuple<std::uint64_t, int, std::string, std::uint64_t>, Carrier> carriers;
    std::deque<std::string> events;
    std::optional<double> last_noise;
    // The measurement asked for.
    Collect mode = Collect::None;
    double freq_hz = 0.0;
    std::size_t want = 0;
    calibration::LevelAverager level;
    std::vector<double> values;  // FreqError: Hz; Noise: dBm

    void target(const std::string& p, const std::string& k) {
        const std::lock_guard lock(m);
        if (p == plugin && k == key) return;
        plugin = p;
        key = k;
        carriers.clear();
        events.clear();
        mode = Collect::None;
    }

    void start(Collect c, double hz, std::size_t n) {
        const std::lock_guard lock(m);
        mode = c;
        freq_hz = hz;
        want = std::max<std::size_t>(1, n);
        level.reset();
        values.clear();
    }

    void stop() {
        const std::lock_guard lock(m);
        mode = Collect::None;
    }

    // How far the measurement is (have, want); `done` when finished.
    Collecting progress(bool* done = nullptr) const {
        const std::lock_guard lock(m);
        Collecting c;
        c.active = mode != Collect::None;
        c.want = want;
        c.have = mode == Collect::Level ? level.count() : values.size();
        if (done != nullptr) *done = c.active && c.have >= c.want;
        return c;
    }

    void on_record(const measure::MappedRecord& r) override {
        const std::lock_guard lock(m);
        if (r.radio_plugin_id != plugin || r.device_key != key) return;
        Carrier& c = carriers[{r.frequency_hz, static_cast<int>(r.role), r.record.schema_id, r.entry_index}];
        c.latest = r;
        c.arrivals.push_back(r.arrival_ns);
        while (!c.arrivals.empty() && c.arrivals.front() < r.arrival_ns - kRateWindowNs) c.arrivals.pop_front();
        const bool measured = r.role == measure::CarrierRole::Serving || r.role == measure::CarrierRole::Neighbour ||
                              r.role == measure::CarrierRole::Other;
        if (!measured || std::fabs(static_cast<double>(r.frequency_hz) - freq_hz) > kTuneToleranceHz) return;
        if (mode == Collect::Level && r.radio_dbfs && std::isfinite(r.radio_level) && level.count() < want) {
            double snr = calibration::kNaN;
            for (const measure::QualityValue& q : r.quality)
                if (q.field == YR_FIELD_SNR_DB) snr = q.value;
            level.add(r.radio_level, (r.flags & measure::sample_flag::kNoSync) != 0,
                      (r.flags & measure::sample_flag::kOverload) != 0, snr);
        } else if (mode == Collect::FreqError && values.size() < want) {
            for (const measure::QualityValue& q : r.quality)
                if (q.field == YR_FIELD_FREQ_ERROR_HZ && std::isfinite(q.value)) values.push_back(q.value);
        }
    }

    void on_event(const measure::RadioEvent& e) override {
        const std::lock_guard lock(m);
        if (e.plugin_id != plugin || (!e.device_key.empty() && e.device_key != key)) return;
        std::string t;
        switch (e.kind) {
        case measure::RadioEvent::Kind::Stream: t = std::string(event_name(e.event)) + " " + std::to_string(e.value); break;
        case measure::RadioEvent::Kind::Started: t = tr("stream started"); break;
        case measure::RadioEvent::Kind::Stopped: t = tr("stream stopped"); break;
        case measure::RadioEvent::Kind::Lost: t = tr("device lost"); break;
        case measure::RadioEvent::Kind::Reopened: t = tr("device reopened"); break;
        case measure::RadioEvent::Kind::Failed: t = tr("plugin failed"); break;
        }
        if (!e.message.empty()) t += ": " + e.message;
        events.push_back(std::move(t));
        while (events.size() > 100) events.pop_front();
    }

    void on_cycle(const measure::MeasurementCycle& c) override {
        const std::optional<double> noise = measure::cycle_noise_dbm(c);
        const std::lock_guard lock(m);
        if (noise) last_noise = noise;
        if (mode == Collect::Noise && noise && values.size() < want) values.push_back(*noise);
    }
};

std::string date_text(std::int64_t unix_s) {
    const std::time_t t = static_cast<std::time_t>(unix_s);
    const std::tm tm = platform::utc_tm(t);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}

std::string mhz(double hz) {
    char b[48];
    std::snprintf(b, sizeof b, "%.4f MHz", hz / 1e6);
    return b;
}

bool parse_list(const std::string& text, double scale, std::vector<double>& out) {
    out.clear();
    std::string item;
    std::stringstream ss(text);
    while (std::getline(ss, item, ',')) {
        const auto a = item.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        char* end = nullptr;
        const double x = std::strtod(item.c_str() + a, &end);
        if (end == item.c_str() + a || !std::isfinite(x)) return false;
        while (*end == ' ' || *end == '\t') ++end;
        if (*end != '\0') return false;
        out.push_back(x * scale);
    }
    return !out.empty();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::string flags_text(std::uint32_t flags) {
    std::string t;
    auto add = [&](std::uint32_t bit, const char* name) {
        if ((flags & bit) == 0) return;
        t += (t.empty() ? "" : ", ");
        t += name;
    };
    add(YR_DEVICE_IN_USE, tr("in use"));
    add(YR_DEVICE_NO_PERMISSION, tr("no permission"));
    add(YR_DEVICE_SIMULATED, "simulated");
    add(YR_DEVICE_LEVEL_DBM, "dBm");  // radio API 1.1: levels in dBm (HW-01b)
    return t.empty() ? std::string("none") : t;
}

}  // namespace

// -- the model -----------------------------------------------------------------------------------

struct RadioWindows::Impl {
    LiveSurvey& live;
    std::shared_ptr<Tap> tap = std::make_shared<Tap>();
    std::string lang = "en";
    std::set<RadioWindow> open;
    bool advanced = false;
    std::optional<Confirmation> confirmation;

    // The device the windows show (UI thread).
    std::uint64_t gen_seen = ~std::uint64_t{0};
    std::uint64_t changed_seen = 0;
    std::shared_ptr<radio::RadioDevice> device;
    std::string plugin_id, device_key, title;
    std::uint32_t caps = 0;
    double last_poll = -1e9;

    // What the worker found (guarded by sm).
    struct Snap {
        std::vector<SettingDef> defs;
        std::map<std::string, Value> actual;
        std::vector<radio::Preset> presets;
        std::vector<SettingDef> sensor_defs;
        bool sensors_listed = false;
        std::map<std::string, Value> sensors;
        bool stats_valid = false;
        radio::DeviceStats stats;
        std::vector<radio::SelfTestItem> self_test;
        bool self_test_ran = false;
        std::map<std::string, std::string> errors, results;
        std::string message;
        bool message_error = false;
    };
    mutable std::mutex sm;
    Snap snap;
    std::uint64_t snap_gen = 0;  // guarded by sm: the device generation the snapshot belongs to

    // The worker.
    std::thread worker;
    mutable std::mutex qm;
    std::condition_variable qcv;
    mutable std::condition_variable idle_cv;
    std::deque<std::function<void()>> jobs;
    bool job_running = false;
    bool stop = false;
    bool poll_queued = false;  // guarded by qm

    // Calibration (UI thread).
    std::optional<calibration::Wizard> wizard;
    std::optional<calibration::Entry> result;
    Collect collecting = Collect::None;
    std::optional<double> verify_error, ppm;
    std::string ui_message;
    bool ui_error = false;

    explicit Impl(LiveSurvey& l) : live(l) {
        live.add_observer(tap);
        worker = std::thread([this] {
            platform::set_thread_name("yt-radio-window");
            work();
        });
    }

    ~Impl() {
        {
            const std::lock_guard lock(qm);
            stop = true;
            jobs.clear();
        }
        qcv.notify_all();
        if (worker.joinable()) worker.join();
        live.remove_observer(tap);
    }

    void work() {
        std::unique_lock lock(qm);
        for (;;) {
            qcv.wait(lock, [&] { return stop || !jobs.empty(); });
            if (stop) return;
            std::function<void()> job = std::move(jobs.front());
            jobs.pop_front();
            job_running = true;
            lock.unlock();
            try {
                job();
            } catch (...) {
            }
            lock.lock();
            job_running = false;
            if (jobs.empty()) idle_cv.notify_all();
        }
    }

    void post(std::function<void()> job) {
        {
            const std::lock_guard lock(qm);
            jobs.push_back(std::move(job));
        }
        qcv.notify_all();
    }

    bool busy() const {
        const std::lock_guard lock(qm);
        return job_running || !jobs.empty();
    }

    std::function<std::string(const std::string&)> translator() const {
        radio::RadioHost* host = live.host();
        if (host == nullptr || lang == "en" || lang.empty()) return {};
        if (lang == language_key(kPseudoLanguage))  // TST-52: the English text, made longer
            return [host, pid = plugin_id](const std::string& k) { return pseudo_text(host->translation(pid, k, "en")); };
        return [host, pid = plugin_id, lg = lang](const std::string& k) { return host->translation(pid, k, lg); };
    }

    // Publishes into the snapshot of generation `gen` only (a job for an
    // older device is dropped).
    template <class F>
    void publish(std::uint64_t gen, F f) {
        const std::lock_guard lock(sm);
        if (gen == snap_gen) f(snap);
    }

    void say(std::uint64_t gen, const std::string& text, bool error) {
        publish(gen, [&](Snap& s) {
            s.message = text;
            s.message_error = error;
        });
    }

    std::string why(radio::RadioDevice& dev, yr_status st) const {
        std::string e = dev.last_error();
        return std::string(yr_status_name(st)) + (e.empty() ? std::string() : ": " + e);
    }

    // -- jobs (worker thread) ----------------------------------------------------------
    void read_settings(const std::shared_ptr<radio::RadioDevice>& dev, std::uint64_t gen) {
        std::vector<SettingDef> defs;
        if (const yr_status st = dev->list_settings(defs); st != YR_OK) {
            say(gen, tr("The settings could not be read: ") + why(*dev, st), true);
            return;
        }
        std::map<std::string, Value> actual;
        for (const SettingDef& d : defs) {
            if (d.type == YR_TYPE_ACTION) continue;
            Value v;
            if (dev->get_setting(d.key, v) == YR_OK) actual[d.key] = v;
        }
        std::vector<radio::Preset> presets;
        (void)dev->list_presets(presets);
        publish(gen, [&](Snap& s) {
            s.defs = std::move(defs);
            s.actual = std::move(actual);
            s.presets = std::move(presets);
        });
    }

    void poll(const std::shared_ptr<radio::RadioDevice>& dev, std::uint64_t gen) {
        {
            const std::lock_guard lock(qm);
            poll_queued = false;
        }
        bool listed = false;
        std::vector<SettingDef> defs;
        {
            const std::lock_guard lock(sm);
            listed = snap.sensors_listed;
            defs = snap.sensor_defs;
        }
        if (!listed) {
            defs.clear();
            (void)dev->list_sensors(defs);
        }
        std::map<std::string, Value> values;
        for (const SettingDef& d : defs) {
            Value v;
            if (dev->read_sensor(d.key, v) == YR_OK) values[d.key] = v;
        }
        radio::DeviceStats stats = dev->stats();
        publish(gen, [&](Snap& s) {
            s.sensor_defs = defs;
            s.sensors_listed = true;
            s.sensors = std::move(values);
            s.stats = std::move(stats);
            s.stats_valid = true;
        });
    }

    void set(const std::shared_ptr<radio::RadioDevice>& dev, std::uint64_t gen, const std::string& key,
             const Value& value, const std::string& label) {
        const yr_status st = dev->set_setting(key, value);
        if (st == YR_OK) {
            publish(gen, [&](Snap& s) { s.errors.erase(key); });
            say(gen, label + " set.", false);
        } else {
            const std::string e = why(*dev, st);
            publish(gen, [&](Snap& s) { s.errors[key] = e; });
            say(gen, label + tr(" not set: ") + e, true);
        }
        read_settings(dev, gen);
    }

    // -- UI thread -------------------------------------------------------------------------
    void follow() {
        const std::uint64_t gen = live.device_generation();
        std::shared_ptr<radio::RadioDevice> dev = live.device();
        if (gen == gen_seen && dev == device) return;
        gen_seen = gen;
        device = std::move(dev);
        plugin_id = live.device_plugin();
        device_key = live.device_key();
        const RadioChoice* c = live.chosen();
        title = c != nullptr ? c->label : std::string();
        caps = 0;
        if (radio::RadioHost* host = live.host(); host != nullptr && !plugin_id.empty())
            if (const auto e = host->entry(plugin_id)) caps = e->capabilities;
        {
            const std::lock_guard lock(sm);
            snap = Snap{};
            ++snap_gen;
        }
        tap->target(plugin_id, device_key);
        wizard.reset();
        result.reset();
        collecting = Collect::None;
        verify_error.reset();
        ppm.reset();
        confirmation.reset();
        if (radio::RadioHost* host = live.host(); host != nullptr && !plugin_id.empty())
            changed_seen = host->settings_changed_count(plugin_id);
        if (device) {
            queue_read();
            queue_poll();
        }
    }

    std::uint64_t current_gen() const {
        const std::lock_guard lock(sm);
        return snap_gen;
    }

    void queue_read() {
        if (!device) return;
        post([this, dev = device, gen = current_gen()] { read_settings(dev, gen); });
    }

    void queue_poll() {
        if (!device) return;
        {
            const std::lock_guard lock(qm);
            if (poll_queued) return;
            poll_queued = true;
        }
        post([this, dev = device, gen = current_gen()] { poll(dev, gen); });
    }

    DeviceHeader header() const {
        DeviceHeader h;
        h.connected = device != nullptr;
        h.title = title;
        if (!live.radios_searched()) h.reason = tr("Find the radios first (Radio > Radios...).");
        else if (!device) h.reason = tr("Connect the radio first (Radio > Radios...): its windows show the open device.");
        h.busy = busy();
        {
            const std::lock_guard lock(sm);
            h.message = snap.message;
            h.message_error = snap.message_error;
        }
        if (!ui_message.empty()) {
            h.message = ui_message;
            h.message_error = ui_error;
        }
        return h;
    }

    Result refuse(const std::string& why) {
        ui_message = why;
        ui_error = true;
        return Result{false, why};
    }
    Result note(const std::string& text) {
        ui_message = text;
        ui_error = false;
        return Result{true, text};
    }

    std::vector<SensorRow> sensor_rows() const {
        std::vector<SettingDef> defs;
        std::map<std::string, Value> values;
        {
            const std::lock_guard lock(sm);
            defs = snap.sensor_defs;
            values = snap.sensors;
        }
        const auto tr = translator();
        std::vector<SensorRow> out;
        for (const SettingDef& raw : defs) {
            const SettingDef d = translate_setting(raw, "sensor", tr);
            SensorRow r{d.key, d.label, d.unit, d.help, {}};
            if (const auto it = values.find(d.key); it != values.end()) r.value = setting_value_text(d, it->second);
            out.push_back(std::move(r));
        }
        return out;
    }

    const SettingDef* find_def(const std::vector<SettingDef>& defs, const std::string& key) const {
        for (const SettingDef& d : defs)
            if (d.key == key) return &d;
        return nullptr;
    }

    std::shared_ptr<const calibration::Key> key() const { return device ? device->calibration_key() : nullptr; }
    // The radio delivers dBm itself (HW-01b): a SELF_CALIBRATED plugin, or
    // a device whose entry has LEVEL_DBM (radio API 1.1, e.g. the
    // simulator's "Levels: dBm"); no calibration applies to its levels.
    bool self_calibrated() const {
        if ((caps & YR_CAP_SELF_CALIBRATED) != 0) return true;
        if (!device) return false;
        const auto i = device->info();
        return i && (i->flags & YR_DEVICE_LEVEL_DBM) != 0;
    }
    bool simulated() const { return (caps & YR_CAP_SIMULATED) != 0; }

    std::string measure_reason() const {
        if (!device) return tr("Connect the radio first.");
        if (!live.radio_running())
            return tr("Start the radio first (Walk tab > Start radio): the readings come from its records.");
        if (collecting != Collect::None) return tr("A measurement is running.");
        return {};
    }

    // NFR-07: the noise is compared in dBm, so the radio must deliver dBm
    // (self-calibrated or a calibration table entry for its key).
    std::string noise_reason() const {
        if (!device) return tr("Connect the radio first.");
        if (!self_calibrated()) {
            const auto t = live.calibration_table();
            const auto k = key();
            if (!t || !k || t->find(*k) == nullptr)
                return tr("Calibrate the radio first: the reference noise is in dBm and this radio measures dBFS "
                       "without a calibration.");
        }
        if (!live.radio_running()) return tr("Start the radio first (Walk tab > Start radio), with the antenna away from the PC.");
        if (collecting != Collect::None) return tr("A measurement is running.");
        return {};
    }
};

RadioWindows::RadioWindows(LiveSurvey& live) : impl_(std::make_unique<Impl>(live)) {}
RadioWindows::~RadioWindows() = default;

// -- windows -----------------------------------------------------------------------------------

Result RadioWindows::open(RadioWindow w, std::optional<std::size_t> radio) {
    Impl& m = *impl_;
    m.open.insert(w);
    m.ui_message.clear();
    if (!radio) return {};
    LiveSurvey& live = m.live;
    if (*radio >= live.radios().size()) return m.refuse(tr("There is no such radio."));
    const RadioChoice* c = live.chosen();
    const bool same = c != nullptr && c == &live.radios()[*radio];
    if (!same) {
        if (live.radio_running() || live.running())
            return m.refuse(tr("Stop the radio first: ") + live.radio_state().text + ".");
        if (const Result r = live.choose_radio(*radio); !r.ok) return m.refuse(r.message);
    }
    if (!live.connected()) {
        const Result r = live.connect();
        if (!r.ok) return m.refuse(r.message);
    }
    m.follow();
    return {};
}

void RadioWindows::close(RadioWindow w) {
    impl_->open.erase(w);
    if (w == RadioWindow::Settings) impl_->confirmation.reset();
}

bool RadioWindows::is_open(RadioWindow w) const { return impl_->open.count(w) != 0; }

void RadioWindows::set_language(std::string lang) { impl_->lang = std::move(lang); }

void RadioWindows::tick(double now_s) {
    Impl& m = *impl_;
    m.follow();
    if (!m.device) return;
    // RAD-44: the plugin changed its settings itself.
    if (radio::RadioHost* host = m.live.host(); host != nullptr) {
        const std::uint64_t c = host->settings_changed_count(m.plugin_id);
        if (c != m.changed_seen) {
            m.changed_seen = c;
            m.queue_read();
        }
    }
    // Sensors and counters at most once per second (RAD-45) while a window shows them.
    const bool shows = is_open(RadioWindow::Debug) || is_open(RadioWindow::Info) || is_open(RadioWindow::Calibration);
    if (shows && now_s - m.last_poll >= 1.0) {
        m.last_poll = now_s;
        m.queue_poll();
    }
    // A measurement that has its readings.
    bool done = false;
    (void)m.tap->progress(&done);
    if (!done || m.collecting == Collect::None) return;
    const Collect what = m.collecting;
    m.collecting = Collect::None;
    calibration::LevelAverager avg;
    std::vector<double> values;
    {
        const std::lock_guard lock(m.tap->m);
        avg = m.tap->level;
        values = m.tap->values;
        m.tap->mode = Collect::None;
    }
    switch (what) {
    case Collect::Level:
        if (m.wizard) {
            const calibration::Reading& r = m.wizard->record(avg);
            char b[160];
            std::snprintf(b, sizeof b, "%s %.4f MHz, %.1f dBm: %.2f dBFS", r.accepted ? tr("Accepted") : tr("Refused"),
                          r.frequency_hz / 1e6, r.level_dbm, r.measured_dbfs);
            if (r.accepted) (void)m.note(b);
            else (void)m.refuse(std::string(b) + " (" + r.problem + ")");
            if (m.wizard->done()) {
                m.result = m.wizard->result(platform::utc_now_ns() / 1'000'000'000);
                if (m.result && m.ppm) m.result->ppm = *m.ppm;
            }
        } else {
            // Verify mode.
            const auto t = m.live.calibration_table();
            const auto k = m.key();
            const calibration::Lookup l = t && k ? t->lookup(*k, impl_->live.radios().empty() ? 0.0 : form_.verify_mhz * 1e6,
                                                             platform::utc_now_ns() / 1'000'000'000)
                                                 : calibration::Lookup{};
            const double e = calibration::verify_error_db(l, avg.mean_dbfs(), form_.verify_dbm);
            if (std::isfinite(e)) {
                m.verify_error = e;
                char b[96];
                std::snprintf(b, sizeof b, tr("Verify: the table reads %+.2f dB off the reference."), e);
                (void)m.note(b);
            } else {
                (void)m.refuse(tr("Verify: no usable reading or no table entry for this key and frequency."));
            }
        }
        break;
    case Collect::FreqError: {
        const double true_hz = form_.carrier_mhz * 1e6;
        const double p = values.empty() ? calibration::kNaN
                                        : calibration::ppm_from_carrier(true_hz, true_hz + median(values));
        if (std::isfinite(p)) {
            m.ppm = p;
            if (m.result) m.result->ppm = p;
            char b[96];
            std::snprintf(b, sizeof b, tr("Frequency error: %+.3f ppm."), p);
            (void)m.note(b);
        } else {
            (void)m.refuse(tr("Frequency error: the radio's records have no frequency error."));
        }
        break;
    }
    case Collect::Noise:
        if (!values.empty()) {
            const double ref = median(values);
            m.live.monitor()->set_interference_reference(ref);
            char b[128];
            std::snprintf(b, sizeof b, tr("Reference noise %.1f dBm taken (median of %zu cycles)."), ref, values.size());
            (void)m.note(b);
        }
        break;
    case Collect::None: break;
    }
}

// -- radios ---------------------------------------------------------------------------------------

RadioListView RadioWindows::list_view() const {
    const Impl& m = *impl_;
    LiveSurvey& live = m.live;
    RadioListView v;
    v.searched = live.radios_searched();
    v.note = live.radio_note();
    if (radio::RadioHost* host = live.host(); host != nullptr)
        for (const radio::RadioPluginEntry& e : host->list()) {
            PluginRow p;
            p.id = e.id;
            p.name = e.name.empty() ? e.path.filename().string() : e.name;
            p.version = e.version;
            p.api = std::to_string(e.api_version.major) + "." + std::to_string(e.api_version.minor);
            p.author = e.author;
            p.licence = e.licence;
            p.status = radio::status_text(e.status);
            p.message = e.message;
            p.capabilities = radio::capabilities_text(e.capabilities);
            // PLG-16: usable when the search found a usable device of it.
            const auto& radios = live.radios();
            const auto usable = std::find_if(radios.begin(), radios.end(),
                                             [&](const RadioChoice& c) { return c.plugin_id == e.id && c.usable; });
            const auto any = std::find_if(radios.begin(), radios.end(), [&](const RadioChoice& c) { return c.plugin_id == e.id; });
            if (e.status == radio::RadioPluginStatus::Disabled) {
                p.reason = tr("The plugin is switched off (File > Settings... > Plugins).");
            } else if (e.status != radio::RadioPluginStatus::Enabled) {
                p.reason = tr("The plugin could not be loaded: ") + e.message;
            } else if (!v.searched) {
                p.reason = tr("Find the radios first (Radio > Find radios).");
            } else if (any == radios.end()) {
                const std::string why = live.search_note(e.id);
                p.reason = why.empty() ? std::string(tr("No device found.")) : tr("No device found: ") + why;
            } else if (usable == radios.end()) {
                p.reason = tr("No usable device: ") + any->reason;
            }
            p.usable = p.reason.empty();
            v.plugins.push_back(std::move(p));
        }
    const RadioChoice* chosen = live.chosen();
    for (std::size_t i = 0; i < live.radios().size(); ++i) {
        const RadioChoice& c = live.radios()[i];
        RadioRow r;
        r.index = i;
        r.label = c.label;
        r.plugin_id = c.plugin_id;
        r.device_key = c.device_key;
        r.simulated = c.simulated;
        r.usable = c.usable;
        r.reason = c.reason;
        r.chosen = &c == chosen;
        r.connected = r.chosen && live.connected();
        r.measuring = r.chosen && live.radio_running();
        v.radios.push_back(std::move(r));
    }
    const RadioState s = live.radio_state();
    v.state = s.text;
    if (live.connected() && !s.running) v.state = s.label + tr(": connected");
    if (!v.searched) v.connect_reason = tr("Find the radios first.");
    else if (chosen == nullptr) v.connect_reason = tr("Choose a radio first.");
    else if (live.connected()) v.connect_reason = tr("The radio is connected.");
    v.can_connect = v.connect_reason.empty();
    if (!live.connected()) v.disconnect_reason = tr("No radio is connected.");
    else if (live.radio_running()) v.disconnect_reason = tr("Stop the radio first: it is measuring.");
    v.can_disconnect = v.disconnect_reason.empty();
    return v;
}

void RadioWindows::load_plugins() {
    if (impl_->live.host() == nullptr) (void)impl_->live.load_radio_plugins();
}

Result RadioWindows::find_radios() {
    const Result r = impl_->live.find_radios();
    impl_->follow();
    return r.ok ? impl_->note(r.message) : impl_->refuse(r.message);
}

Result RadioWindows::choose(std::size_t index) {
    const Result r = impl_->live.choose_radio(index);
    impl_->follow();
    return r.ok ? impl_->note(r.message) : impl_->refuse(r.message);
}

Result RadioWindows::connect() {
    const Result r = impl_->live.connect();
    impl_->follow();
    return r.ok ? impl_->note(r.message) : impl_->refuse(r.message);
}

Result RadioWindows::disconnect() {
    const Result r = impl_->live.disconnect();
    impl_->follow();
    return r.ok ? impl_->note(r.message) : impl_->refuse(r.message);
}

// -- settings -------------------------------------------------------------------------------------

SettingsView RadioWindows::settings_view() const {
    const Impl& m = *impl_;
    SettingsView v;
    v.header = m.header();
    v.advanced = m.advanced;
    v.confirmation = m.confirmation;
    if (!m.device) return v;
    Impl::Snap s;
    {
        const std::lock_guard lock(m.sm);
        s.defs = m.snap.defs;
        s.actual = m.snap.actual;
        s.presets = m.snap.presets;
        s.errors = m.snap.errors;
        s.results = m.snap.results;
    }
    const auto tr = m.translator();
    std::vector<SettingDef> defs;
    defs.reserve(s.defs.size());
    for (const SettingDef& d : s.defs) defs.push_back(translate_setting(d, "setting", tr));
    const auto stored = m.live.state().device_settings(m.plugin_id, m.device_key);
    for (const SettingGroup& g : group_settings(defs, m.advanced, &v.advanced_hidden)) {
        SettingSection sec{g.name, {}};
        for (const SettingDef* d : g.settings) {
            SettingRow r;
            r.def = *d;
            r.widget = widget_of(*d);
            r.requested = d->default_value;
            for (const auto& [k, val] : stored)
                if (k == d->key) r.requested = val;
            if (const auto it = s.actual.find(d->key); it != s.actual.end()) r.actual = it->second;
            r.requested_text = setting_value_text(*d, r.requested);
            r.actual_text = setting_value_text(*d, r.actual);
            r.differs = d->type != YR_TYPE_ACTION && !std::holds_alternative<std::monostate>(r.actual) &&
                        !std::holds_alternative<std::monostate>(r.requested) && !same_value(r.requested, r.actual);
            r.enabled = !d->has(YR_SETTING_UNAVAILABLE) && !d->has(YR_SETTING_READ_ONLY);
            if (d->has(YR_SETTING_UNAVAILABLE))
                r.disabled_reason = d->unavailable_reason.empty() ? std::string(tr("Not available now.")) : d->unavailable_reason;
            else if (d->has(YR_SETTING_READ_ONLY))
                r.disabled_reason = tr("Read only.");
            if (const auto it = s.errors.find(d->key); it != s.errors.end()) r.error = it->second;
            if (const auto it = s.results.find(d->key); it != s.results.end()) r.result = it->second;
            sec.rows.push_back(std::move(r));
        }
        v.sections.push_back(std::move(sec));
    }
    for (const radio::Preset& p : s.presets) {
        PresetRow r{p.id, p.label, p.help};
        if (tr) {
            if (std::string t = tr("preset." + p.id + ".label"); !t.empty()) r.label = t;
            if (std::string t = tr("preset." + p.id + ".help"); !t.empty()) r.help = t;
        }
        v.presets.push_back(std::move(r));
    }
    return v;
}

void RadioWindows::set_advanced(bool on) { impl_->advanced = on; }

Result RadioWindows::request(const std::string& key, const Value& value) {
    Impl& m = *impl_;
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    SettingDef d;
    {
        const std::lock_guard lock(m.sm);
        const SettingDef* p = m.find_def(m.snap.defs, key);
        if (p == nullptr) return m.refuse(tr("The radio has no setting '") + key + "'.");
        d = translate_setting(*p, "setting", m.translator());
    }
    if (d.has(YR_SETTING_UNAVAILABLE)) return m.refuse(d.label + ": " + d.unavailable_reason);
    if (d.has(YR_SETTING_READ_ONLY)) return m.refuse(d.label + tr(" is read only."));
    if (const std::string e = validate_setting(d, value); !e.empty()) {
        const std::lock_guard lock(m.sm);
        m.snap.errors[key] = e;
        return m.refuse(d.label + ": " + e);
    }
    {
        const std::lock_guard lock(m.sm);
        m.snap.errors.erase(key);
    }
    m.ui_message.clear();
    if (d.has(YR_SETTING_WARNING)) {
        m.confirmation = Confirmation{Confirmation::Kind::Warning, key, value,
                                      d.warning.empty() ? tr("Change ") + d.label + "?" : d.warning};
        return Result{true, tr("Confirm the change.")};
    }
    if (d.has(YR_SETTING_RESTART) && m.live.radio_running()) {
        m.confirmation = Confirmation{Confirmation::Kind::Restart, key, value,
                                      d.label + tr(" changes only while the radio is stopped: the stream stops, the "
                                                "setting is applied and the stream starts again.")};
        return Result{true, tr("Confirm the restart.")};
    }
    m.post([&m, dev = m.device, gen = m.current_gen(), key, value, label = d.label] { m.set(dev, gen, key, value, label); });
    return Result{true, tr("Setting ") + d.label + "..."};
}

Result RadioWindows::confirm() {
    Impl& m = *impl_;
    if (!m.confirmation) return m.refuse(tr("Nothing to confirm."));
    const Confirmation c = *m.confirmation;
    m.confirmation.reset();
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    const std::uint64_t gen = m.current_gen();
    if (c.kind == Confirmation::Kind::Reset) {
        m.post([&m, dev = m.device, gen] {
            std::vector<SettingDef> defs;
            if (dev->list_settings(defs) != YR_OK) return;
            std::size_t n = 0, refused = 0;
            for (const SettingDef& d : defs) {
                if (d.type == YR_TYPE_ACTION || d.has(YR_SETTING_READ_ONLY) ||
                    std::holds_alternative<std::monostate>(d.default_value))
                    continue;
                if (dev->set_setting(d.key, d.default_value) == YR_OK) ++n;
                else ++refused;
            }
            m.say(gen,
                  std::to_string(n) + tr(" settings back to their defaults") +
                      (refused != 0 ? " (" + std::to_string(refused) + tr(" not available now).") : "."),
                  false);
            m.read_settings(dev, gen);
        });
        return Result{true, tr("Resetting to defaults...")};
    }
    std::string label = c.key;
    {
        const std::lock_guard lock(m.sm);
        if (const SettingDef* p = m.find_def(m.snap.defs, c.key)) label = p->label;
    }
    m.post([&m, dev = m.device, gen, key = c.key, value = c.value, label] { m.set(dev, gen, key, value, label); });
    return Result{true, tr("Setting ") + label + "..."};
}

void RadioWindows::cancel_confirmation() { impl_->confirmation.reset(); }

const std::optional<Confirmation>& RadioWindows::confirmation() const { return impl_->confirmation; }

Result RadioWindows::run_action(const std::string& key) {
    Impl& m = *impl_;
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    std::string label = key;
    {
        const std::lock_guard lock(m.sm);
        const SettingDef* p = m.find_def(m.snap.defs, key);
        if (p == nullptr || p->type != YR_TYPE_ACTION) return m.refuse(tr("The radio has no action '") + key + "'.");
        if (p->has(YR_SETTING_UNAVAILABLE)) return m.refuse(p->label + ": " + p->unavailable_reason);
        label = p->label;
    }
    m.ui_message.clear();
    m.post([&m, dev = m.device, gen = m.current_gen(), key, label] {
        std::string text;
        const yr_status st = dev->run_action(key, text);
        const std::string shown = st == YR_OK ? text : m.why(*dev, st);
        m.publish(gen, [&](Impl::Snap& s) { s.results[key] = shown; });
        m.say(gen, label + ": " + shown, st != YR_OK);
        m.read_settings(dev, gen);
    });
    return Result{true, label + "..."};
}

Result RadioWindows::apply_preset(const std::string& id) {
    Impl& m = *impl_;
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    m.ui_message.clear();
    m.post([&m, dev = m.device, gen = m.current_gen(), id] {
        const yr_status st = dev->apply_preset(id);
        m.say(gen, st == YR_OK ? tr("Preset applied.") : tr("The preset was not applied: ") + m.why(*dev, st), st != YR_OK);
        m.read_settings(dev, gen);
    });
    return Result{true, tr("Applying the preset...")};
}

Result RadioWindows::reset_defaults() {
    Impl& m = *impl_;
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    m.confirmation = Confirmation{Confirmation::Kind::Reset, {}, {},
                                  tr("Set every setting of ") + m.title +
                                      tr(" back to its default? The values are stored for this device.")};
    return Result{true, tr("Confirm the reset.")};
}

// -- debug -------------------------------------------------------------------------------------------

DebugView RadioWindows::debug_view() const {
    const Impl& m = *impl_;
    DebugView v;
    v.header = m.header();
    if (!m.device) return v;
    v.state = radio::state_text(m.device->state());
    {
        const std::lock_guard lock(m.sm);
        v.stats_valid = m.snap.stats_valid;
        v.stats = m.snap.stats;
    }
    v.sensors = m.sensor_rows();
    const std::int64_t now = platform::monotonic_ns();
    {
        const std::lock_guard lock(m.tap->m);
        for (const auto& [k, c] : m.tap->carriers) {
            RecordRow r;
            const measure::MappedRecord& rec = c.latest;
            r.carrier = mhz(static_cast<double>(rec.frequency_hz));
            r.role = measure::role_text(rec.role);
            r.schema = rec.record.schema_id;
            r.flags = measure::sample_flags_text(rec.flags);
            r.age_s = std::max<std::int64_t>(0, now - rec.arrival_ns) / 1e9;
            if (c.arrivals.size() >= 2) {
                const double span = (c.arrivals.back() - c.arrivals.front()) / 1e9;
                r.rate_hz = span > 0.0 ? static_cast<double>(c.arrivals.size() - 1) / span : 0.0;
            }
            if (rec.schema)
                for (std::size_t i = 0; i < rec.schema->fields.size() && i < rec.record.values.size(); ++i) {
                    const plugin::Field& f = rec.schema->fields[i];
                    if (std::holds_alternative<std::monostate>(rec.record.values[i])) continue;
                    std::string t = plugin::value_text(rec.record.values[i], f);
                    if (!f.unit.empty() && f.enum_items.empty()) t += " " + f.unit;
                    r.fields.emplace_back(f.label.empty() ? f.name : f.label, std::move(t));
                }
            v.records.push_back(std::move(r));
        }
        v.events.assign(m.tap->events.begin(), m.tap->events.end());
    }
    if (radio::RadioHost* host = m.live.host(); host != nullptr) {
        for (radio::LogLine& l : host->log_lines(m.plugin_id))
            if (l.device_key.empty() || l.device_key == m.device_key) v.log.push_back(std::move(l));
        for (const auto& [k, n] : host->context_calls(m.plugin_id)) v.context_calls.emplace_back(k, n);
    }
    return v;
}

// -- calibration -----------------------------------------------------------------------------------

CalibrationView RadioWindows::calibration_view() const {
    const Impl& m = *impl_;
    CalibrationView v;
    v.header = m.header();
    v.table_file = m.live.paths().calibration_table.string();
    v.noise_reference_dbm = m.live.monitor()->interference_reference();
    const measure::SelfInterferenceOptions o;
    v.thermal_dbm = measure::thermal_noise_dbm(o.bandwidth_hz, o.noise_figure_db);
    {
        const std::lock_guard lock(m.tap->m);
        v.last_noise_dbm = m.tap->last_noise;
    }
    v.measuring = m.live.radio_running();
    v.noise_reason = m.noise_reason();
    if (m.collecting != Collect::None) v.collecting = m.tap->progress();
    v.verify_error_db = m.verify_error;
    v.ppm = m.ppm;
    v.result = m.result;
    v.ppm_reason = m.measure_reason();
    v.verify_reason = m.wizard ? std::string(tr("Finish or cancel the calibration first.")) : v.ppm_reason;
    if (!m.device) {
        v.start_reason = tr("Connect the radio first.");
        v.measure_reason = v.start_reason;
        return v;
    }
    v.self_calibrated = m.self_calibrated();
    v.simulated = m.simulated();
    const auto key = m.key();
    const std::int64_t now = platform::utc_now_ns() / 1'000'000'000;
    const auto table = m.live.calibration_table();
    if (key) v.key_text = key->id();
    if (v.self_calibrated) {
        v.key_status = tr("Self-calibrated: the radio delivers dBm itself.");
    } else if (!key) {
        v.key_status = tr("No calibration key yet.");
    } else if (const calibration::Entry* e = table ? table->find(*key) : nullptr) {
        v.key_status = std::string(e->expired(now) ? tr("Calibrated, EXPIRED ") : tr("Calibrated on ")) + date_text(e->created_unix) +
                       (e->expired(now) ? "" : tr(", valid until ") + date_text(e->expires_unix()));
    } else {
        v.key_status = tr("Uncalibrated: levels are dBFS.");
    }
    if (table && key)
        for (const calibration::Entry* e : table->of_device(key->driver, key->serial)) {
            CalibrationEntryRow r;
            r.key_id = e->key.id();
            r.date = date_text(e->created_unix);
            r.expires = date_text(e->expires_unix());
            r.serial = e->key.serial;
            r.settings = e->key.settings_text();
            r.method = e->method;
            r.status = e->expired(now) ? "expired" : "valid";
            r.points = e->points.size();
            r.simulated = e->simulated;
            r.current = e->key == *key;
            v.entries.push_back(std::move(r));
        }
    v.wizard = m.wizard.has_value();
    if (v.self_calibrated) v.start_reason = tr("This radio measures dBm itself: no level calibration is needed.");
    else if (!key) v.start_reason = tr("The radio has no calibration key yet: wait until it is open.");
    else if (m.wizard) v.start_reason = tr("A calibration is running.");
    v.measure_reason = m.wizard ? m.measure_reason() : tr("Start the calibration first.");
    if (m.wizard) {
        v.readings = m.wizard->readings();
        if (!m.wizard->done()) {
            const calibration::Target t = m.wizard->target();
            char b[200];
            std::snprintf(b, sizeof b, tr("Point %zu of %zu: set the %s to %.4f MHz, %.1f dBm, then Measure."), t.index + 1,
                          t.count, m.wizard->plan().method == calibration::kMethodReference ? tr("reference radio") : "generator",
                          t.frequency_hz / 1e6, t.level_dbm);
            v.target = b;
        } else {
            v.target = tr("All points measured: save the result into the calibration table.");
        }
    }
    return v;
}

Result RadioWindows::start_calibration() {
    Impl& m = *impl_;
    const CalibrationView v = calibration_view();
    if (!v.start_reason.empty()) return m.refuse(v.start_reason);
    calibration::Plan plan;
    if (!parse_list(form_.frequencies_mhz, 1e6, plan.frequencies_hz))
        return m.refuse(tr("Frequencies: enter MHz values separated by commas."));
    if (!parse_list(form_.levels_dbm, 1.0, plan.levels_dbm))
        return m.refuse(tr("Levels: enter dBm values separated by commas."));
    plan.valid_days = form_.valid_days;
    plan.method = form_.method;
    plan.simulated = m.simulated();
    if (plan.simulated) plan.method = calibration::kMethodPractice;
    if (const std::string e = plan.validate(); !e.empty()) return m.refuse(e);
    if (form_.readings < 1 || form_.readings > 100) return m.refuse(tr("Readings per point: 1 to 100."));
    m.wizard.emplace(*m.key(), plan, m.plugin_id);
    m.result.reset();
    return m.note(tr("Calibration started for ") + m.key()->id() + ".");
}

Result RadioWindows::measure_point() {
    Impl& m = *impl_;
    if (!m.wizard) return m.refuse(tr("Start the calibration first."));
    if (m.wizard->done()) return m.refuse(tr("All points are measured."));
    if (const std::string why = m.measure_reason(); !why.empty()) return m.refuse(why);
    m.tap->start(Collect::Level, m.wizard->target().frequency_hz, form_.readings);
    m.collecting = Collect::Level;
    return m.note(tr("Measuring..."));
}

Result RadioWindows::skip_point() {
    Impl& m = *impl_;
    if (!m.wizard || m.wizard->done()) return m.refuse(tr("No point to skip."));
    m.tap->stop();
    m.collecting = Collect::None;
    m.wizard->skip();
    if (m.wizard->done()) m.result = m.wizard->result(platform::utc_now_ns() / 1'000'000'000);
    return m.note(tr("Point skipped."));
}

Result RadioWindows::cancel_calibration() {
    Impl& m = *impl_;
    m.tap->stop();
    m.collecting = Collect::None;
    m.wizard.reset();
    m.result.reset();
    return m.note(tr("Calibration cancelled."));
}

Result RadioWindows::save_calibration() {
    Impl& m = *impl_;
    if (!m.result || m.result->points.empty()) return m.refuse(tr("No accepted points to save."));
    calibration::Table t;
    if (const auto cur = m.live.calibration_table()) t = *cur;
    t.put(*m.result);
    const Result r = m.live.set_calibration_table(t);
    if (!r.ok) return m.refuse(r.message);
    m.wizard.reset();
    m.result.reset();
    return m.note(r.message);
}

Result RadioWindows::remove_entry(const std::string& key_id) {
    Impl& m = *impl_;
    const auto cur = m.live.calibration_table();
    if (!cur) return m.refuse(tr("There is no calibration table."));
    calibration::Table t = *cur;
    for (const calibration::Entry& e : cur->entries())
        if (e.key.id() == key_id) {
            t.remove(e.key);
            const Result r = m.live.set_calibration_table(t);
            return r.ok ? m.note(tr("Entry removed. ") + r.message) : m.refuse(r.message);
        }
    return m.refuse(tr("No such entry."));
}

Result RadioWindows::export_table(const fs::path& file) {
    Impl& m = *impl_;
    if (file.empty()) return m.refuse(tr("Enter a file name."));
    const auto cur = m.live.calibration_table();
    std::string error;
    if (!(cur ? *cur : calibration::Table{}).save(file, error)) return m.refuse(tr("Export failed: ") + error);
    return m.note(tr("Exported to ") + file.string() + ".");
}

Result RadioWindows::import_table(const fs::path& file) {
    Impl& m = *impl_;
    if (file.empty()) return m.refuse(tr("Enter a file name."));
    calibration::Table in;
    std::string error;
    if (!calibration::Table::load(file, in, error)) return m.refuse(tr("Import failed: ") + error);
    calibration::Table t;
    if (const auto cur = m.live.calibration_table()) t = *cur;
    const calibration::MergeResult mr = t.merge(in);
    const Result r = m.live.set_calibration_table(t);
    if (!r.ok) return m.refuse(r.message);
    return m.note(tr("Imported: ") + std::to_string(mr.added) + " added, " + std::to_string(mr.replaced) + " replaced, " +
                  std::to_string(mr.kept) + tr(" kept (the newer entry wins)."));
}

Result RadioWindows::verify() {
    Impl& m = *impl_;
    if (m.wizard) return m.refuse(tr("Finish or cancel the calibration first."));
    if (const std::string why = m.measure_reason(); !why.empty()) return m.refuse(why);
    const auto t = m.live.calibration_table();
    const auto k = m.key();
    if (!t || !k || !t->lookup(*k, form_.verify_mhz * 1e6, platform::utc_now_ns() / 1'000'000'000).usable())
        return m.refuse(tr("The calibration table has no entry for this key near ") + mhz(form_.verify_mhz * 1e6) + ".");
    m.verify_error.reset();
    m.tap->start(Collect::Level, form_.verify_mhz * 1e6, form_.readings);
    m.collecting = Collect::Level;
    return m.note(tr("Verifying..."));
}

Result RadioWindows::measure_ppm() {
    Impl& m = *impl_;
    if (const std::string why = m.measure_reason(); !why.empty()) return m.refuse(why);
    if (!(form_.carrier_mhz > 0.0)) return m.refuse(tr("Enter the known carrier's frequency."));
    m.ppm.reset();
    m.tap->start(Collect::FreqError, form_.carrier_mhz * 1e6, form_.readings);
    m.collecting = Collect::FreqError;
    return m.note(tr("Measuring the frequency error..."));
}

Result RadioWindows::take_noise_reference() {
    Impl& m = *impl_;
    if (const std::string why = m.noise_reason(); !why.empty()) return m.refuse(why);
    m.tap->start(Collect::Noise, 0.0, 5);
    m.collecting = Collect::Noise;
    return m.note(tr("Taking the reference noise over 5 cycles..."));
}

Result RadioWindows::clear_noise_reference() {
    impl_->live.monitor()->set_interference_reference(std::nullopt);
    return impl_->note(tr("Reference noise cleared: the thermal floor is the reference again."));
}

// -- device info --------------------------------------------------------------------------------------

InfoView RadioWindows::info_view() const {
    const Impl& m = *impl_;
    InfoView v;
    v.header = m.header();
    if (!m.device) return v;
    if (radio::RadioHost* host = m.live.host(); host != nullptr)
        if (auto e = host->entry(m.plugin_id)) {
            v.plugin_known = true;
            v.plugin = *e;
        }
    v.device = m.device->info();
    if (v.device) v.device_flags = flags_text(v.device->flags);
    if (const auto k = m.key()) v.calibration_key = k->id();
    v.sensors = m.sensor_rows();
    {
        const std::lock_guard lock(m.sm);
        v.self_test_ran = m.snap.self_test_ran;
        v.self_test = m.snap.self_test;
    }
    if (const auto tr = m.translator())
        for (radio::SelfTestItem& i : v.self_test)
            if (std::string t = tr("selftest." + i.name + ".label"); !t.empty()) i.label = t;
    if (m.live.radio_running()) v.self_test_reason = tr("Stop the radio first: the self-test needs the device idle.");
    return v;
}

Result RadioWindows::run_self_test() {
    Impl& m = *impl_;
    if (!m.device) return m.refuse(tr("Connect the radio first."));
    if (m.live.radio_running()) return m.refuse(tr("Stop the radio first: the self-test needs the device idle."));
    m.ui_message.clear();
    m.post([&m, dev = m.device, gen = m.current_gen()] {
        std::vector<radio::SelfTestItem> items;
        const yr_status st = dev->self_test(items);
        std::size_t failed = 0;
        for (const radio::SelfTestItem& i : items) failed += i.result == YR_TEST_FAIL ? 1 : 0;
        m.publish(gen, [&](Impl::Snap& s) {
            s.self_test = items;
            s.self_test_ran = true;
        });
        if (st != YR_OK) m.say(gen, tr("The self-test did not run: ") + m.why(*dev, st), true);
        else
            m.say(gen,
                  tr("Self-test: ") + std::to_string(items.size()) + " items, " + std::to_string(failed) +
                      (failed == 1 ? " failed." : " failed."),
                  failed != 0);
    });
    return Result{true, tr("Running the self-test...")};
}

bool RadioWindows::wait_idle(int timeout_ms) const {
    const Impl& m = *impl_;
    std::unique_lock lock(m.qm);
    return m.idle_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [&] { return !m.job_running && m.jobs.empty(); });
}

}  // namespace yantra::app
