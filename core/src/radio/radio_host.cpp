// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radio host (PLG-25, PLG-27, OA-10, NFR-04), see radio_host.h.
// Design from yantra-radios fb194f5: tools/yantra-radio-host/host.cpp
// (load and version check, non-blocking sink with drop counting, context
// notifier with the 30/s cursor limit), adapted: stored settings,
// delivery thread per stream, reopen on DEVICE_BACK, control thread with
// call limits, snapshots from a ContextProvider.

#include "yantra/core/radio/radio_host.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <yantra/radio.hpp>

#include "control_thread.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/platform/dynlib.h"
#include "yantra/core/plugin/folders.h"

namespace yantra::radio {
struct PluginRt;
struct Sink;
}  // namespace yantra::radio

// Opaque handles of radio.h, defined by the host.
struct yr_host {
    yantra::radio::PluginRt* rt;
};
struct yr_stream {
    yantra::radio::Sink* sink;
};

namespace yantra::radio {

using Clock = std::chrono::steady_clock;
using ms = std::chrono::milliseconds;

const char* status_text(RadioPluginStatus status) noexcept {
    switch (status) {
    case RadioPluginStatus::Enabled: return "enabled";
    case RadioPluginStatus::Disabled: return "disabled";
    case RadioPluginStatus::Failed: return "failed";
    case RadioPluginStatus::Refused: return "refused";
    }
    return "unknown";
}

const char* state_text(DeviceState state) noexcept {
    switch (state) {
    case DeviceState::Closed: return "closed";
    case DeviceState::Open: return "open";
    case DeviceState::Streaming: return "streaming";
    case DeviceState::Lost: return "lost";
    case DeviceState::Failed: return "failed";
    }
    return "unknown";
}

const char* test_result_text(std::uint32_t result) noexcept {
    switch (result) {
    case YR_TEST_PASS: return "pass";
    case YR_TEST_WARN: return "warn";
    case YR_TEST_FAIL: return "fail";
    default: return "skipped";
    }
}

std::string capabilities_text(std::uint32_t caps) {
    static const std::pair<std::uint32_t, const char*> names[] = {
        {YR_CAP_SCAN, "scan"},           {YR_CAP_HOTPLUG, "hotplug"},
        {YR_CAP_RETUNE_LIVE, "retune live"}, {YR_CAP_SIMULATED, "simulated"},
        {YR_CAP_SCENE, "scene"},         {YR_CAP_SELF_CALIBRATED, "self calibrated"}};
    std::string out;
    for (const auto& [bit, name] : names) {
        if ((caps & bit) == 0) continue;
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out;
}

namespace {

std::string text(const char* s) { return s == nullptr ? std::string{} : std::string{s}; }

bool valid_status(yr_status st) { return st == YR_OK || (st <= -1 && st >= -13); }

plugin::Value value_from_c(const yr_value& v) {
    switch (v.type) {
    case YR_TYPE_BOOL: return v.u.i64 != 0;
    case YR_TYPE_INT: return std::int64_t{v.u.i64};
    case YR_TYPE_UINT: return std::uint64_t{v.u.u64};
    case YR_TYPE_DOUBLE: return v.u.f64;
    case YR_TYPE_STRING: return text(v.u.str);
    case YR_TYPE_ENUM: return plugin::EnumValue{v.u.i64};
    default: return std::monostate{};
    }
}

// A setting or sensor definition, copied (RAD-40, RAD-45).
SettingDef setting_from_c(const yr_setting_def& sd) {
    SettingDef d;
    d.key = text(sd.key);
    d.type = sd.type;
    d.flags = sd.flags;
    d.label = text(sd.label);
    d.unit = text(sd.unit);
    d.help = text(sd.help);
    d.group = text(sd.group);
    d.unavailable_reason = text(sd.unavailable_reason);
    d.warning = text(sd.warning);
    if (sd.type == YR_TYPE_ENUM && sd.items != nullptr)
        for (std::uint32_t i = 0; i < sd.item_count; ++i)
            d.items.push_back(EnumItem{sd.items[i].value, text(sd.items[i].name), text(sd.items[i].label)});
    d.minimum = sd.minimum;
    d.maximum = sd.maximum;
    d.step = sd.step;
    d.default_value = value_from_c(sd.default_value);
    d.sort_order = sd.sort_order;
    return d;
}

// A yr_value with its string storage.
struct CValue {
    yr_value v = yr_value_none();
    std::string str;
};

CValue value_to_c(const plugin::Value& value) {
    CValue c;
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>) {
                c.v = yr_value_bool(x ? 1 : 0);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                c.v = yr_value_int(x);
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                c.v = yr_value_uint(x);
            } else if constexpr (std::is_same_v<T, double>) {
                c.v = yr_value_double(x);
            } else if constexpr (std::is_same_v<T, std::string>) {
                c.str = x;
                c.v.type = YR_TYPE_STRING;
            } else if constexpr (std::is_same_v<T, plugin::EnumValue>) {
                c.v = yr_value_enum(x.value);
            }
        },
        value);
    if (c.v.type == YR_TYPE_STRING) c.v.u.str = c.str.c_str();
    return c;
}

// Copies `s` into a fixed context buffer, cut at a UTF-8 character border.
template <std::size_t N>
void copy_fixed(char (&dst)[N], const std::string& s) {
    std::size_t n = std::min(s.size(), N - 1);
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

// Writes `full` into the plugin's versioned struct `out`, only as far as
// both sides know it (out->struct_size, set by the plugin).
template <class T>
bool write_versioned(T* out, T full) {
    if (out == nullptr || out->struct_size < 8) return false;
    const std::uint32_t size = out->struct_size;
    full.struct_size = size;
    std::memcpy(static_cast<void*>(out), &full, std::min<std::size_t>(size, sizeof(T)));
    return true;
}

}  // namespace

// --- per-plugin runtime ------------------------------------------------------------

struct PluginRt {
    RadioHost::Impl* host = nullptr;
    std::filesystem::path path;
    std::unique_ptr<platform::DynamicLibrary> library;
    const yr_descriptor* d = nullptr;
    yr_plugin* plugin = nullptr;
    yr_host handle{this};
    yr_host_api api{};
    std::vector<std::shared_ptr<const std::vector<std::string>>> field_names;
    // RAD-48 translations: "<lang>\n<text key>" -> text (immutable after load).
    std::map<std::string, std::string> translations;
    std::unique_ptr<detail::ControlThread> control;
    std::atomic<bool> failed{false};
    std::atomic<std::uint64_t> devices_changed{0};
    std::atomic<std::uint64_t> settings_changed{0};

    // Guarded by RadioHost::Impl::mutex.
    RadioPluginEntry entry;

    // Devices opened from this plugin.
    std::mutex dev_mutex;
    std::vector<std::weak_ptr<RadioDevice::Impl>> devices;
    std::map<const yr_device*, std::string> device_keys;

    // Context callbacks (PLG-27).
    mutable std::mutex ctx_mutex;
    std::map<std::string, std::uint64_t> ctx_calls;
    mutable std::mutex n_mutex;
    mutable std::condition_variable n_cv;
    std::deque<std::uint32_t> n_queue;
    bool n_busy = false;
    bool n_stop = false;
    std::map<std::uint32_t, std::uint64_t> n_sent;
    std::thread notifier;
    std::uint64_t subscription = 0;

    const std::string& id() const { return entry.id; }

    void count_ctx(const char* fn, yr_status st) {
        const std::lock_guard lock(ctx_mutex);
        ++ctx_calls[std::string(fn) + ":" + yr_status_name(st)];
    }
};

// --- calibration key of a device (HW-01b) ----------------------------------------

// The device's calibration keys over time: a record gets the key valid at
// its measurement time, so records measured before a gain change keep the
// old key even when they arrive after it.
struct KeyHistory {
    static constexpr std::size_t kKeep = 8;
    mutable std::mutex m;
    std::vector<std::pair<std::int64_t, std::shared_ptr<const calibration::Key>>> h;

    // Records from `since_ns` on are measured with `key`; an unchanged key
    // is not added again.
    void set(std::int64_t since_ns, calibration::Key key) {
        const std::lock_guard lock(m);
        if (!h.empty() && *h.back().second == key) return;
        h.emplace_back(since_ns, std::make_shared<const calibration::Key>(std::move(key)));
        if (h.size() > kKeep) h.erase(h.begin());
    }
    std::shared_ptr<const calibration::Key> at(std::int64_t t) const {
        const std::lock_guard lock(m);
        for (auto it = h.rbegin(); it != h.rend(); ++it)
            if (it->first <= t) return it->second;
        return h.empty() ? nullptr : h.front().second;
    }
    std::shared_ptr<const calibration::Key> latest() const {
        const std::lock_guard lock(m);
        return h.empty() ? nullptr : h.back().second;
    }
};

// --- record sink of one stream ------------------------------------------------------

struct Sink {
    struct Item {
        bool is_record = true;
        measure::RadioRecord record;
        std::uint32_t event = 0;
        std::int64_t value = 0;
        std::int64_t t = 0;
    };

    yr_stream handle{this};
    PluginRt* rt = nullptr;
    std::string device_key;
    std::uint64_t stream_id = 0;
    std::shared_ptr<measure::MeasurementInput> pipeline;
    std::shared_ptr<const KeyHistory> keys;  // the device's calibration keys (HW-01b)
    std::size_t capacity = 4096;
    // Called on the delivery thread for the events the host acts on.
    std::function<void(std::uint32_t event, std::int64_t value)> on_control_event;

    std::mutex m;
    std::condition_variable cv;
    std::deque<Item> q;
    bool active = false;       // records accepted
    bool events_open = false;  // events accepted
    bool stopped = false;      // stop_stream returned: pushes are late
    bool finishing = false;
    SinkCounters c;
    std::thread delivery;

    void start() {
        {
            const std::lock_guard lock(m);
            active = true;
            events_open = true;
        }
        delivery = std::thread([this] { deliver_main(); });
    }

    // After stop_stream/close returned: no more records or events; the
    // queue is delivered, then the delivery thread ends. Not from it.
    void finish() {
        {
            const std::lock_guard lock(m);
            active = false;
            events_open = false;
            stopped = true;
            finishing = true;
        }
        cv.notify_all();
        if (delivery.joinable() && delivery.get_id() != std::this_thread::get_id()) delivery.join();
    }

    void deactivate_records() {
        const std::lock_guard lock(m);
        active = false;
    }

    SinkCounters counters() {
        const std::lock_guard lock(m);
        return c;
    }

    yr_status push(const yr_record* record) {
        if (record == nullptr) return YR_ERR_INVALID_ARGUMENT;
        {
            const std::lock_guard lock(m);
            if (!active) {
                if (stopped) ++c.late;
                return YR_ERR_BUSY;
            }
        }
        std::vector<std::string> problems = radio::validate_record(*rt->d, *record);
        if (!problems.empty()) {
            const std::lock_guard lock(m);
            ++c.invalid;
            for (std::string& p : problems)
                if (c.problems.size() < 10) c.problems.push_back(std::move(p));
            return YR_ERR_INVALID_ARGUMENT;
        }
        // Copy (the record is valid during the call only, RAD-20).
        Item item;
        measure::RadioRecord& r = item.record;
        const yr_schema& s = *rt->d->schemas[record->schema_index];
        r.plugin_id = rt->entry.id;
        r.plugin_version = rt->entry.version;
        r.technology_id = rt->entry.technology_id;
        r.device_key = device_key;
        r.stream_id = stream_id;
        r.schema_index = record->schema_index;
        r.schema_kind = s.kind;
        r.present = record->present;
        r.capabilities = rt->d->capabilities;
        r.field_names = rt->field_names[record->schema_index];
        r.radio_schema = &s;  // the plugin's descriptor: valid while it is loaded
        if (keys) r.calibration_key = keys->at(record->time.monotonic_ns);
        r.record.schema_id = text(s.id);
        r.record.schema_version = s.version;
        r.record.time.monotonic_ns = record->time.monotonic_ns;
        r.record.time.utc_ns = record->time.utc_ns;
        r.record.time.utc_source = record->time.utc_source;
        r.record.flags = record->flags;
        r.record.values.resize(s.field_count);
        for (std::uint32_t i = 0; i < record->value_count && i < s.field_count; ++i)
            r.record.values[i] = value_from_c(record->values[i]);
        {
            const std::lock_guard lock(m);
            if (!active) {
                if (stopped) ++c.late;
                return YR_ERR_BUSY;
            }
            if (q.size() >= capacity) {
                ++c.dropped;  // RAD-24: never block, drop and count
                return YR_OK;
            }
            q.push_back(std::move(item));
            ++c.accepted;
        }
        cv.notify_one();
        return YR_OK;
    }

    void event(std::uint32_t e, std::int64_t value) {
        Item item;
        item.is_record = false;
        item.event = e;
        item.value = value;
        item.t = platform::monotonic_ns();
        {
            const std::lock_guard lock(m);
            if (!events_open) {
                if (stopped) ++c.late;
                return;
            }
            // Streaming has stopped with these (RAD-25): later records are BUSY.
            if (e == YR_EVENT_DEVICE_LOST || e == YR_EVENT_END_OF_STREAM || e == YR_EVENT_ERROR)
                active = false;
            q.push_back(std::move(item));
        }
        cv.notify_one();
    }

    void deliver_main() {
        std::unique_lock lock(m);
        for (;;) {
            cv.wait(lock, [&] { return finishing || !q.empty(); });
            if (q.empty()) return;
            std::deque<Item> items;
            items.swap(q);
            lock.unlock();
            std::uint64_t delivered = 0;
            for (Item& it : items) {
                if (it.is_record) {
                    if (pipeline) pipeline->on_radio_record(it.record);
                    ++delivered;
                    continue;
                }
                if (pipeline) {
                    measure::RadioEvent ev;
                    ev.kind = measure::RadioEvent::Kind::Stream;
                    ev.plugin_id = rt->entry.id;
                    ev.device_key = device_key;
                    ev.stream_id = stream_id;
                    ev.event = it.event;
                    ev.value = it.value;
                    ev.monotonic_ns = it.t;
                    pipeline->on_radio_event(ev);
                }
                if (on_control_event &&
                    (it.event == YR_EVENT_DEVICE_LOST || it.event == YR_EVENT_DEVICE_BACK ||
                     it.event == YR_EVENT_END_OF_STREAM || it.event == YR_EVENT_ERROR))
                    on_control_event(it.event, it.value);
            }
            lock.lock();
            c.delivered += delivered;
        }
    }
};

namespace {

yr_status api_push(yr_stream* stream, const yr_record* record) {
    if (stream == nullptr || stream->sink == nullptr) return YR_ERR_INVALID_ARGUMENT;
    try {
        return stream->sink->push(record);
    } catch (...) {
        return YR_ERR_NO_MEMORY;
    }
}

void api_event(yr_stream* stream, std::uint32_t event, std::int64_t value) {
    if (stream == nullptr || stream->sink == nullptr) return;
    try {
        stream->sink->event(event, value);
    } catch (...) {
    }
}

}  // namespace

// --- device ---------------------------------------------------------------------------

struct RadioDevice::Impl {
    PluginRt* rt = nullptr;  // nullptr once the host is gone
    std::weak_ptr<RadioHost::Impl> host;
    std::string plugin_id;
    std::string key;

    std::recursive_mutex op;  // one operation sequence at a time
    mutable std::mutex m;     // the fields below
    yr_device* dev = nullptr;
    DeviceState state = DeviceState::Closed;
    std::optional<Plan> plan;
    bool want_stream = false;
    std::shared_ptr<Sink> sink;
    SinkCounters totals;
    std::uint64_t streams = 0;
    std::uint64_t reopens = 0;
    std::optional<DeviceInfo> info;  // device_info after open (driver, serial: HW-01b)
    // Calibration keys (HW-01b); shared with the sinks of its streams.
    std::shared_ptr<KeyHistory> keys = std::make_shared<KeyHistory>();
};

// --- host -------------------------------------------------------------------------------

struct RadioHost::Impl : std::enable_shared_from_this<RadioHost::Impl> {
    RadioHostConfig config;
    std::shared_ptr<plugin::PluginStateStore> state;
    std::thread::id main_thread = std::this_thread::get_id();

    mutable std::mutex mutex;  // plugins vector, entries, refused
    std::vector<std::unique_ptr<PluginRt>> plugins;
    std::vector<RadioPluginEntry> refused;

    // Host tasks: reactions to stream events and devices_changed.
    std::mutex task_mutex;
    std::condition_variable task_cv;
    std::deque<std::function<void()>> tasks;
    bool task_stop = false;
    std::thread task_thread;

    std::atomic<std::uint64_t> next_stream_id{1};

    bool on_main_thread() const { return std::this_thread::get_id() == main_thread; }

    // The newest log lines per plugin id (debug window, PLG-16).
    mutable std::mutex log_mutex;
    mutable std::map<std::string, std::deque<LogLine>> log_rings;

    void log(const std::string& id, const std::string& key, std::uint32_t level,
             const std::string& message) const {
        if (!id.empty()) {
            const std::lock_guard lock(log_mutex);
            std::deque<LogLine>& ring = log_rings[id];
            ring.push_back(LogLine{platform::utc_now_ns(), level, key, message});
            while (ring.size() > RadioHost::kLogLines) ring.pop_front();
        }
        if (config.log) config.log(id, key, level, message);
    }

    PluginRt* find(const std::string& id) const {
        const std::lock_guard lock(mutex);
        for (const auto& rt : plugins)
            if (rt->entry.id == id) return rt.get();
        return nullptr;
    }

    ms limit(bool long_call) const {
        const double base = long_call ? 10000.0 : 1000.0;
        return ms(static_cast<std::int64_t>(std::ceil(base * std::max(1.0, config.call_slack))));
    }

    void emit(measure::RadioEvent::Kind kind, const std::string& id, const std::string& key,
              std::uint64_t stream, const std::string& message) const {
        if (!config.pipeline) return;
        measure::RadioEvent ev;
        ev.kind = kind;
        ev.plugin_id = id;
        ev.device_key = key;
        ev.stream_id = stream;
        ev.monotonic_ns = platform::monotonic_ns();
        ev.message = message;
        config.pipeline->on_radio_event(ev);
    }

    void post(std::function<void()> task) {
        {
            const std::lock_guard lock(task_mutex);
            if (task_stop) return;
            tasks.push_back(std::move(task));
            if (!task_thread.joinable()) task_thread = std::thread([this] { task_main(); });
        }
        task_cv.notify_all();
    }

    void task_main() {
        std::unique_lock lock(task_mutex);
        for (;;) {
            task_cv.wait(lock, [&] { return task_stop || !tasks.empty(); });
            if (task_stop) return;
            std::function<void()> t = std::move(tasks.front());
            tasks.pop_front();
            lock.unlock();
            try {
                t();
            } catch (...) {
            }
            lock.lock();
        }
    }

    void stop_tasks() {
        {
            const std::lock_guard lock(task_mutex);
            task_stop = true;
            tasks.clear();
        }
        task_cv.notify_all();
        if (task_thread.joinable()) task_thread.join();
    }

    // Disables a plugin whose call crashed (PLG-25); the app keeps running.
    void fail(PluginRt& rt, const std::string& message) {
        {
            const std::lock_guard lock(mutex);
            if (rt.failed.exchange(true)) return;
            rt.entry.status = RadioPluginStatus::Failed;
            rt.entry.message = message;
        }
        log(rt.id(), "", YR_LOG_ERROR, rt.entry.name + " disabled: " + message);
        std::vector<std::shared_ptr<RadioDevice::Impl>> devs;
        {
            const std::lock_guard lock(rt.dev_mutex);
            for (auto& w : rt.devices)
                if (auto d = w.lock()) devs.push_back(d);
        }
        for (auto& d : devs) {
            std::shared_ptr<Sink> sink;
            {
                const std::lock_guard lock(d->m);
                d->state = DeviceState::Failed;
                d->want_stream = false;
                sink = d->sink;
            }
            if (sink) sink->deactivate_records();
            emit(measure::RadioEvent::Kind::Failed, rt.id(), d->key, 0, message);
        }
    }

    // A control call with the RAD-15 limit; a timeout or an unknown result
    // code disables the plugin. `f` captures by value.
    template <class F>
    yr_status call(PluginRt& rt, const char* name, bool long_call, F f, bool even_if_failed = false) {
        if (rt.failed.load() && !even_if_failed) return YR_ERR_FAILED;
        yr_status st = YR_ERR_FAILED;
        const ms lim = limit(long_call);
        switch (rt.control->call<yr_status>(f, lim, st)) {
        case detail::ControlThread::Result::Done: break;
        case detail::ControlThread::Result::Timeout:
            fail(rt, std::string(name) + " did not return within " +
                         std::to_string(lim.count() / 1000.0).substr(0, 4) + " s (RAD-15)");
            return YR_ERR_FAILED;
        case detail::ControlThread::Result::Refused: return YR_ERR_FAILED;
        }
        if (!valid_status(st)) {
            fail(rt, std::string(name) + " returned the unknown result code " + std::to_string(st) +
                         " (RAD-16)");
            return YR_ERR_FAILED;
        }
        return st;
    }

    template <class F>
    bool call_void(PluginRt& rt, const char* name, F f, bool even_if_failed = false) {
        return call(
                   rt, name, false,
                   [f]() mutable {
                       f();
                       return YR_OK;
                   },
                   even_if_failed) == YR_OK;
    }

    std::string last_error(PluginRt& rt, yr_device* dev) {
        auto buffer = std::make_shared<std::string>();
        const yr_descriptor* d = rt.d;
        yr_plugin* p = rt.plugin;
        const yr_status st = call(
            rt, "last_error", false,
            [d, p, dev, buffer] {
                char b[1024] = {};
                const yr_status s = d->stream->last_error(p, dev, b, sizeof b);
                *buffer = b;
                return s;
            },
            true);
        return st == YR_OK ? *buffer : std::string{};
    }

    // --- device operations (caller holds d.op) -----------------------------

    void apply_settings(PluginRt& rt, RadioDevice::Impl& d) {
        for (const auto& [k, v] : state->device_settings(rt.id(), d.key)) {
            const CValue c = value_to_c(v);
            const yr_descriptor* desc = rt.d;
            yr_device* dev = d.dev;
            const std::string key = k;
            const yr_status st = call(rt, "set_setting", false, [desc, dev, key, c] {
                return desc->device->set_setting(dev, key.c_str(), &c.v);
            });
            if (st != YR_OK)
                log(rt.id(), d.key, YR_LOG_WARNING,
                    "stored setting " + k + " not applied: " + yr_status_name(st) + " " +
                        last_error(rt, dev));
        }
    }

    // The open device's entry (RAD-30): driver and serial of the key.
    std::optional<DeviceInfo> device_info(PluginRt& rt, yr_device* dev) {
        auto info = std::make_shared<std::optional<DeviceInfo>>();
        const yr_descriptor* desc = rt.d;
        const yr_status st = call(rt, "device_info", false, [desc, dev, info] {
            return desc->stream->device_info(
                dev,
                [](void* user, const yr_device_info* i) {
                    if (i == nullptr) return;
                    DeviceInfo x;
                    x.key = text(i->key);
                    x.label = text(i->label);
                    x.driver = text(i->driver);
                    x.serial = text(i->serial);
                    x.module_version = text(i->module_version);
                    x.hardware_info = text(i->hardware_info);
                    x.flags_reason = text(i->flags_reason);
                    x.flags = i->flags;
                    *static_cast<std::optional<DeviceInfo>*>(user) = std::move(x);
                },
                info.get());
        });
        return st == YR_OK ? *info : std::nullopt;
    }

    // HW-01b: reads the device's driver and serial (once per open) and the
    // current value of every CALIBRATION setting (RAD-40), and records the
    // key from `since_ns` on. Called after open/reopen (the stored settings
    // applied), after each set_setting and on settings_changed. Caller
    // holds d.op.
    void refresh_key(PluginRt& rt, RadioDevice::Impl& d, std::int64_t since_ns, bool reread_info) {
        yr_device* dev = nullptr;
        {
            const std::lock_guard lock(d.m);
            dev = d.dev;
            if (dev == nullptr) return;
            if (!d.info) reread_info = true;
        }
        if (reread_info) {
            std::optional<DeviceInfo> info = device_info(rt, dev);
            const std::lock_guard lock(d.m);
            d.info = std::move(info);
        }
        auto keys = std::make_shared<std::vector<std::string>>();
        const yr_descriptor* desc = rt.d;
        const yr_status ls = call(rt, "list_settings", false, [desc, dev, keys] {
            return desc->device->list_settings(
                dev,
                [](void* user, const yr_setting_def* sd) {
                    if (sd == nullptr || sd->key == nullptr) return;
                    if ((sd->flags & YR_SETTING_CALIBRATION) == 0 || sd->type == YR_TYPE_ACTION) return;
                    static_cast<std::vector<std::string>*>(user)->emplace_back(sd->key);
                },
                keys.get());
        });
        calibration::Key key;
        {
            const std::lock_guard lock(d.m);
            if (d.info) {
                key.driver = d.info->driver;
                key.serial = d.info->serial;
            }
        }
        if (ls == YR_OK) {
            for (const std::string& k : *keys) {
                auto result = std::make_shared<plugin::Value>();
                const yr_status st = call(rt, "get_setting", false, [desc, dev, k, result] {
                    yr_value v = yr_value_none();
                    char textbuf[1024] = {};
                    const yr_status r = desc->device->get_setting(dev, k.c_str(), &v, textbuf, sizeof textbuf);
                    if (r == YR_OK) *result = value_from_c(v);
                    return r;
                });
                if (st == YR_OK) key.settings[k] = calibration::setting_text(*result);
            }
        }
        d.keys->set(since_ns, std::move(key));
    }

    yr_status start_locked(PluginRt& rt, RadioDevice::Impl& d, const Plan& plan) {
        auto sink = std::make_shared<Sink>();
        sink->rt = &rt;
        sink->device_key = d.key;
        sink->stream_id = next_stream_id++;
        sink->pipeline = config.pipeline;
        sink->keys = d.keys;
        sink->capacity = std::max<std::size_t>(1, config.sink_capacity);
        std::weak_ptr<Impl> wh = weak_from_this();
        std::weak_ptr<RadioDevice::Impl> wd;
        {
            const std::lock_guard lock(rt.dev_mutex);
            for (auto& w : rt.devices)
                if (auto x = w.lock(); x.get() == &d) wd = x;
        }
        const std::uint64_t sid = sink->stream_id;
        sink->on_control_event = [wh, wd, sid](std::uint32_t e, std::int64_t value) {
            auto h = wh.lock();
            if (!h) return;
            h->post([wh, wd, sid, e, value] {
                auto host = wh.lock();
                auto dev = wd.lock();
                if (host && dev) host->on_stream_event(*dev, sid, e, value);
            });
        };
        sink->start();
        auto storage = std::make_shared<Plan>(plan);
        const yr_descriptor* desc = rt.d;
        yr_device* dev = d.dev;
        yr_stream* handle = &sink->handle;
        const yr_status st = call(rt, "start_stream", false, [desc, dev, storage, handle] {
            yr_plan p{};
            p.struct_size = sizeof p;
            p.type = storage->type;
            p.serving_hz = storage->serving_hz;
            p.carriers_hz = storage->carriers_hz.empty() ? nullptr : storage->carriers_hz.data();
            p.carrier_count = static_cast<std::uint32_t>(storage->carriers_hz.size());
            p.flags = storage->flags;
            p.scan_first_hz = storage->scan_first_hz;
            p.scan_last_hz = storage->scan_last_hz;
            p.scan_step_hz = storage->scan_step_hz;
            return desc->stream->start_stream(dev, &p, handle);
        });
        if (st != YR_OK) {
            if (!rt.control->stuck()) sink->finish();
            keep_or_finish(rt, sink);
            return st;
        }
        {
            const std::lock_guard lock(d.m);
            d.sink = sink;
            d.state = DeviceState::Streaming;
            d.plan = plan;
            ++d.streams;
        }
        emit(measure::RadioEvent::Kind::Started, rt.id(), d.key, sid, "");
        return YR_OK;
    }

    // A sink a stuck plugin may still push into is never freed.
    static void keep_or_finish(PluginRt& rt, const std::shared_ptr<Sink>& sink) {
        if (!rt.control->stuck()) return;
        static std::mutex keep_mutex;
        static std::vector<std::shared_ptr<Sink>>* kept = new std::vector<std::shared_ptr<Sink>>;
        const std::lock_guard lock(keep_mutex);
        kept->push_back(sink);
    }

    // Ends the device's current stream on the host side (after stop_stream).
    void retire_sink(PluginRt& rt, RadioDevice::Impl& d) {
        std::shared_ptr<Sink> sink;
        {
            const std::lock_guard lock(d.m);
            sink.swap(d.sink);
        }
        if (!sink) return;
        if (rt.control->stuck()) {
            sink->deactivate_records();
            keep_or_finish(rt, sink);
            return;
        }
        sink->finish();
        const SinkCounters c = sink->counters();
        const std::lock_guard lock(d.m);
        d.totals.accepted += c.accepted;
        d.totals.delivered += c.delivered;
        d.totals.dropped += c.dropped;
        d.totals.invalid += c.invalid;
        d.totals.late += c.late;
        for (const auto& p : c.problems)
            if (d.totals.problems.size() < 10) d.totals.problems.push_back(p);
    }

    yr_status stop_locked(PluginRt& rt, RadioDevice::Impl& d) {
        std::uint64_t sid = 0;
        {
            const std::lock_guard lock(d.m);
            if (!d.sink) return YR_OK;
            sid = d.sink->stream_id;
        }
        const yr_descriptor* desc = rt.d;
        yr_device* dev = d.dev;
        const yr_status st =
            call(rt, "stop_stream", false, [desc, dev] { return desc->stream->stop_stream(dev); }, true);
        retire_sink(rt, d);
        {
            const std::lock_guard lock(d.m);
            if (d.state == DeviceState::Streaming) d.state = DeviceState::Open;
        }
        emit(measure::RadioEvent::Kind::Stopped, rt.id(), d.key, sid, "");
        return st == YR_ERR_DEVICE_LOST ? YR_OK : st;
    }

    void close_locked(PluginRt& rt, RadioDevice::Impl& d) {
        yr_device* dev = nullptr;
        {
            const std::lock_guard lock(d.m);
            dev = d.dev;
        }
        if (dev != nullptr) {
            const yr_descriptor* desc = rt.d;
            // close stops a running stream first (RAD-31).
            call_void(rt, "close", [desc, dev] { desc->stream->close(dev); }, true);
            const std::lock_guard lock(rt.dev_mutex);
            rt.device_keys.erase(dev);
        }
        retire_sink(rt, d);
        const std::lock_guard lock(d.m);
        d.dev = nullptr;
        if (d.state != DeviceState::Failed) d.state = DeviceState::Closed;
        d.want_stream = false;
    }

    yr_status open_device(PluginRt& rt, const std::string& key, yr_device*& out) {
        auto result = std::make_shared<yr_device*>(nullptr);
        const yr_descriptor* desc = rt.d;
        yr_plugin* p = rt.plugin;
        const yr_status st = call(rt, "open", true, [desc, p, key, result] {
            return desc->stream->open(p, key.c_str(), result.get());
        });
        out = st == YR_OK ? *result : nullptr;
        if (st == YR_OK && out == nullptr) return YR_ERR_FAILED;
        if (out != nullptr) {
            const std::lock_guard lock(rt.dev_mutex);
            rt.device_keys[out] = key;
        }
        return st;
    }

    // RAD-35, NFR-04: close, reopen, re-apply the settings, restart.
    void reopen_locked(PluginRt& rt, RadioDevice::Impl& d) {
        {
            const std::lock_guard lock(d.m);
            if (d.state != DeviceState::Lost) return;
        }
        yr_device* old = nullptr;
        {
            const std::lock_guard lock(d.m);
            old = d.dev;
            d.dev = nullptr;
        }
        if (old != nullptr) {
            const yr_descriptor* desc = rt.d;
            call_void(rt, "close", [desc, old] { desc->stream->close(old); });
            const std::lock_guard lock(rt.dev_mutex);
            rt.device_keys.erase(old);
        }
        retire_sink(rt, d);
        yr_device* dev = nullptr;
        const yr_status st = open_device(rt, d.key, dev);
        if (st != YR_OK) {
            log(rt.id(), d.key, YR_LOG_INFO,
                "device not back yet: " + std::string(yr_status_name(st)) + " " +
                    last_error(rt, nullptr));
            return;  // stays Lost; the next devices_changed or DEVICE_BACK retries
        }
        bool want = false;
        std::optional<Plan> plan;
        {
            const std::lock_guard lock(d.m);
            d.dev = dev;
            d.state = DeviceState::Open;
            ++d.reopens;
            want = d.want_stream;
            plan = d.plan;
        }
        apply_settings(rt, d);
        refresh_key(rt, d, platform::monotonic_ns(), true);  // HW-01b (a new handle)
        yr_status started = YR_OK;
        if (want && plan) started = start_locked(rt, d, *plan);
        log(rt.id(), d.key, YR_LOG_INFO,
            want ? "device back: reopened and restarted" : "device back: reopened");
        emit(measure::RadioEvent::Kind::Reopened, rt.id(), d.key, 0,
             started == YR_OK ? "" : "restart failed: " + std::string(yr_status_name(started)));
    }

    void on_stream_event(RadioDevice::Impl& d, std::uint64_t sid, std::uint32_t e, std::int64_t) {
        PluginRt* rt = d.rt;
        if (rt == nullptr || rt->failed.load()) return;
        const std::lock_guard op(d.op);
        {
            const std::lock_guard lock(d.m);
            if (!d.sink || d.sink->stream_id != sid) return;  // an old stream
        }
        switch (e) {
        case YR_EVENT_DEVICE_LOST: {
            {
                const std::lock_guard lock(d.m);
                d.state = DeviceState::Lost;
            }
            log(rt->id(), d.key, YR_LOG_WARNING, "device lost; waiting for it to come back");
            emit(measure::RadioEvent::Kind::Lost, rt->id(), d.key, sid, "");
            break;
        }
        case YR_EVENT_DEVICE_BACK: reopen_locked(*rt, d); break;
        case YR_EVENT_END_OF_STREAM:
        case YR_EVENT_ERROR: {
            const std::string why =
                e == YR_EVENT_ERROR ? last_error(*rt, d.dev) : std::string("end of stream");
            // The plugin stopped streaming; stop_stream brings it back to
            // OPEN on the plugin side too (a no-op for a stopped stream).
            const yr_descriptor* desc = rt->d;
            yr_device* dev = d.dev;
            if (dev != nullptr)
                call(*rt, "stop_stream", false, [desc, dev] { return desc->stream->stop_stream(dev); });
            retire_sink(*rt, d);
            {
                const std::lock_guard lock(d.m);
                if (d.state == DeviceState::Streaming) d.state = DeviceState::Open;
                d.want_stream = false;
            }
            if (e == YR_EVENT_ERROR) log(rt->id(), d.key, YR_LOG_ERROR, "stream stopped: " + why);
            emit(measure::RadioEvent::Kind::Stopped, rt->id(), d.key, sid, why);
            break;
        }
        default: break;
        }
    }

    void on_devices_changed(PluginRt& rt) {
        std::vector<std::shared_ptr<RadioDevice::Impl>> lost;
        {
            const std::lock_guard lock(rt.dev_mutex);
            for (auto& w : rt.devices)
                if (auto d = w.lock()) {
                    const std::lock_guard l(d->m);
                    if (d->state == DeviceState::Lost) lost.push_back(d);
                }
        }
        if (lost.empty()) return;
        const EnumerateResult now = enumerate(rt);
        for (auto& d : lost) {
            const bool present = std::any_of(now.devices.begin(), now.devices.end(),
                                             [&](const DeviceInfo& i) { return i.key == d->key; });
            if (!present) continue;
            const std::lock_guard op(d->op);
            reopen_locked(rt, *d);
        }
    }

    // settings_changed (any device of the plugin when `device` is NULL).
    void on_settings_changed(PluginRt& rt, yr_device* device) {
        std::vector<std::shared_ptr<RadioDevice::Impl>> devs;
        {
            const std::lock_guard lock(rt.dev_mutex);
            for (auto& w : rt.devices)
                if (auto d = w.lock()) devs.push_back(d);
        }
        const std::int64_t now = platform::monotonic_ns();
        for (auto& d : devs) {
            const std::lock_guard op(d->op);
            {
                const std::lock_guard lock(d->m);
                if (d->dev == nullptr || (device != nullptr && d->dev != device)) continue;
            }
            refresh_key(rt, *d, now, false);
        }
    }

    EnumerateResult enumerate(PluginRt& rt) {
        EnumerateResult out;
        auto list = std::make_shared<std::vector<DeviceInfo>>();
        const yr_descriptor* desc = rt.d;
        yr_plugin* p = rt.plugin;
        out.status = call(rt, "enumerate", false, [desc, p, list] {
            return desc->stream->enumerate(
                p,
                [](void* user, const yr_device_info* i) {
                    if (i == nullptr) return;
                    auto* l = static_cast<std::vector<DeviceInfo>*>(user);
                    DeviceInfo x;
                    x.key = text(i->key);
                    x.label = text(i->label);
                    x.driver = text(i->driver);
                    x.serial = text(i->serial);
                    x.module_version = text(i->module_version);
                    x.hardware_info = text(i->hardware_info);
                    x.flags_reason = text(i->flags_reason);
                    x.flags = i->flags;
                    l->push_back(std::move(x));
                },
                list.get());
        });
        if (out.status == YR_OK) out.devices = *list;
        if (out.status == YR_ERR_NO_DEVICE) out.diagnostic = last_error(rt, nullptr);
        return out;
    }

    // --- context (PLG-27) ---------------------------------------------------------

    std::shared_ptr<const context::Snapshot> snapshot() const {
        return config.context ? config.context->snapshot() : nullptr;
    }

    void notifier_main(PluginRt& rt) {
        // RAD-18: CURSOR_MOVED at most 30 per second.
        constexpr auto cursor_gap = std::chrono::microseconds(1'000'000 / 30);
        Clock::time_point last_cursor{};
        std::unique_lock lock(rt.n_mutex);
        for (;;) {
            rt.n_cv.wait(lock, [&] { return rt.n_stop || !rt.n_queue.empty(); });
            if (rt.n_stop) break;
            const std::uint32_t event = rt.n_queue.front();
            if (event == YR_CONTEXT_CURSOR_MOVED && last_cursor != Clock::time_point{}) {
                if (rt.n_cv.wait_until(lock, last_cursor + cursor_gap, [&] { return rt.n_stop; }))
                    break;
            }
            rt.n_queue.pop_front();
            rt.n_busy = true;
            if (event == YR_CONTEXT_CURSOR_MOVED) last_cursor = Clock::now();
            lock.unlock();
            rt.d->on_context_event(rt.plugin, event);
            lock.lock();
            rt.n_busy = false;
            ++rt.n_sent[event];
            rt.n_cv.notify_all();
        }
        rt.n_cv.notify_all();
    }

    void start_context(PluginRt& rt) {
        if ((rt.d->capabilities & YR_CAP_SCENE) == 0 || rt.d->on_context_event == nullptr ||
            !config.context)
            return;
        {
            const std::lock_guard lock(rt.n_mutex);
            rt.n_stop = false;
            rt.n_queue.clear();
        }
        PluginRt* p = &rt;
        rt.notifier = std::thread([this, p] { notifier_main(*p); });
        rt.subscription = config.context->subscribe([p](std::uint32_t bits) {
            static const std::pair<std::uint32_t, std::uint32_t> map[] = {
                {context::kPlanChanged, YR_CONTEXT_PLAN_CHANGED},
                {context::kSceneChanged, YR_CONTEXT_SCENE_CHANGED},
                {context::kCursorMoved, YR_CONTEXT_CURSOR_MOVED},
                {context::kWalkPosition, YR_CONTEXT_WALK_POSITION}};
            {
                const std::lock_guard lock(p->n_mutex);
                for (const auto& [bit, ev] : map)
                    if ((bits & bit) != 0 &&
                        std::find(p->n_queue.begin(), p->n_queue.end(), ev) == p->n_queue.end())
                        p->n_queue.push_back(ev);  // pending repeats are merged
            }
            p->n_cv.notify_all();
        });
    }

    void stop_context(PluginRt& rt) {
        if (rt.subscription != 0 && config.context) config.context->unsubscribe(rt.subscription);
        rt.subscription = 0;
        {
            const std::lock_guard lock(rt.n_mutex);
            rt.n_stop = true;
        }
        rt.n_cv.notify_all();
        if (rt.notifier.joinable()) rt.notifier.join();
    }

    // --- load / enable --------------------------------------------------------------

    RadioPluginEntry refuse(const std::filesystem::path& path, const std::string& message,
                            const yr_descriptor* d = nullptr) {
        RadioPluginEntry e;
        e.path = path;
        e.status = RadioPluginStatus::Refused;
        e.message = message;
        if (d != nullptr && d->struct_size >= radio::descriptor_prefix_size) {
            e.api_version = d->api_version;
            e.capabilities = d->capabilities;
            if (d->api_version.major == YR_API_VERSION_MAJOR &&
                YR_HAS_FIELD(d, yr_descriptor, on_context_event)) {
                e.id = text(d->id);
                e.name = text(d->name);
                e.version = text(d->version);
                e.licence = text(d->licence);
            }
        }
        log("", "", YR_LOG_WARNING, path.filename().string() + " refused: " + message);
        const std::lock_guard lock(mutex);
        refused.push_back(e);
        return e;
    }

    yr_status enable_rt(PluginRt& rt) {
        {
            const std::lock_guard lock(mutex);
            if (rt.entry.status == RadioPluginStatus::Enabled) return YR_OK;
        }
        rt.failed = false;
        rt.control = std::make_unique<detail::ControlThread>();
        yr_plugin* p = nullptr;
        const yr_status st = rt.d->init(&rt.api, &p);  // main thread (RAD-15)
        if (st != YR_OK || p == nullptr) {
            const std::string why = std::string("init failed: ") +
                                    (st == YR_OK ? "no plugin state returned" : yr_status_name(st));
            {
                const std::lock_guard lock(mutex);
                rt.entry.status = RadioPluginStatus::Failed;
                rt.entry.message = why;
            }
            rt.failed = true;
            log(rt.id(), "", YR_LOG_ERROR, rt.entry.name + " disabled: " + why);
            return st == YR_OK ? YR_ERR_FAILED : st;
        }
        rt.plugin = p;
        {
            const std::lock_guard lock(mutex);
            rt.entry.status = RadioPluginStatus::Enabled;
            rt.entry.message.clear();
        }
        start_context(rt);
        log(rt.id(), "", YR_LOG_INFO, rt.entry.name + " enabled");
        return YR_OK;
    }

    void close_devices(PluginRt& rt) {
        std::vector<std::shared_ptr<RadioDevice::Impl>> devs;
        {
            const std::lock_guard lock(rt.dev_mutex);
            for (auto& w : rt.devices)
                if (auto d = w.lock()) devs.push_back(d);
        }
        for (auto& d : devs) {
            const std::lock_guard op(d->op);
            if (!rt.control->stuck()) close_locked(rt, *d);
        }
    }

    void disable_rt(PluginRt& rt, RadioPluginStatus status, const std::string& message) {
        if (rt.plugin != nullptr) {
            stop_context(rt);
            if (rt.control && !rt.control->stuck()) {
                close_devices(rt);
                rt.d->shutdown(rt.plugin);  // main thread, every device closed (RAD-15)
                rt.plugin = nullptr;
                rt.control.reset();
            }
        }
        const std::lock_guard lock(mutex);
        if (rt.entry.status != RadioPluginStatus::Failed || status != RadioPluginStatus::Disabled ||
            !message.empty()) {
            rt.entry.status = status;
            rt.entry.message = message;
        } else {
            rt.entry.status = status;
        }
    }
};

// --- host table functions --------------------------------------------------------------

namespace {

PluginRt* rt_of(yr_host* h) { return h != nullptr ? h->rt : nullptr; }

void api_log(yr_host* h, yr_device* device, std::uint32_t level, const char* message) {
    PluginRt* rt = rt_of(h);
    if (rt == nullptr) return;
    try {
        std::string key;
        if (device != nullptr) {
            const std::lock_guard lock(rt->dev_mutex);
            if (auto it = rt->device_keys.find(device); it != rt->device_keys.end()) key = it->second;
        }
        rt->host->log(rt->id(), key, std::min<std::uint32_t>(level, YR_LOG_ERROR), text(message));
    } catch (...) {
    }
}

std::int64_t api_monotonic(yr_host*) { return platform::monotonic_ns(); }
std::int64_t api_utc(yr_host*) { return platform::utc_now_ns(); }

void api_devices_changed(yr_host* h) {
    PluginRt* rt = rt_of(h);
    if (rt == nullptr) return;
    ++rt->devices_changed;
    try {
        RadioHost::Impl* host = rt->host;
        host->post([host, rt] {
            if (!rt->failed.load()) host->on_devices_changed(*rt);
        });
    } catch (...) {
    }
}

void api_settings_changed(yr_host* h, yr_device* device) {
    PluginRt* rt = rt_of(h);
    if (rt == nullptr) return;
    ++rt->settings_changed;
    // The plugin changed setting values itself (RAD-44): the calibration
    // key may have changed (HW-01b). Re-read it on the host's task thread.
    try {
        RadioHost::Impl* host = rt->host;
        host->post([host, rt, device] {
            if (!rt->failed.load()) host->on_settings_changed(*rt, device);
        });
    } catch (...) {
    }
}

yr_status api_floor_plan(yr_host* h, yr_floor_plan_info* out) {
    PluginRt* rt = rt_of(h);
    if (rt == nullptr) return YR_ERR_INVALID_ARGUMENT;
    yr_status st = YR_OK;
    try {
        const auto s = rt->host->snapshot();
        if (out == nullptr || out->struct_size < 8) {
            st = YR_ERR_INVALID_ARGUMENT;
        } else if (!s || !s->plan) {
            st = YR_ERR_NO_PLAN;
        } else {
            yr_floor_plan_info f{};
            f.scale_set = s->plan->scale_set ? 1u : 0u;
            f.width_m = s->plan->scale_set ? s->plan->width_m : 0.0;
            f.height_m = s->plan->scale_set ? s->plan->height_m : 0.0;
            copy_fixed(f.plan_name, s->plan->plan_name);
            copy_fixed(f.example_id, s->plan->example_id);
            copy_fixed(f.floor_id, s->plan->floor_id);
            copy_fixed(f.floor_name, s->plan->floor_name);
            write_versioned(out, f);
        }
    } catch (...) {
        st = YR_ERR_NO_MEMORY;
    }
    rt->count_ctx("get_floor_plan", st);
    return st;
}

yr_status position(PluginRt* rt, const char* fn, bool walk, yr_plan_position* out) {
    if (rt == nullptr) return YR_ERR_INVALID_ARGUMENT;
    yr_status st = YR_OK;
    try {
        const auto s = rt->host->snapshot();
        const std::optional<context::PlanPosition>* p = nullptr;
        if (s) p = walk ? &s->walk : &s->cursor;
        if (out == nullptr || out->struct_size < 8) {
            st = YR_ERR_INVALID_ARGUMENT;
        } else if (p == nullptr || !p->has_value()) {
            st = YR_ERR_NO_POSITION;
        } else if (!walk && (!s->plan || !s->plan->scale_set)) {
            st = YR_ERR_NO_POSITION;  // mouse over a plan without scale: no metres
        } else {
            const context::PlanPosition& v = **p;
            yr_plan_position pos{};
            pos.x_m = v.x_m;
            pos.y_m = v.y_m;
            pos.moved_monotonic_ns = v.moved_monotonic_ns;
            copy_fixed(pos.floor_id, v.floor_id);
            copy_fixed(pos.floor_name, v.floor_name);
            write_versioned(out, pos);
        }
    } catch (...) {
        st = YR_ERR_NO_MEMORY;
    }
    rt->count_ctx(fn, st);
    return st;
}

yr_status api_cursor(yr_host* h, yr_plan_position* out) {
    return position(rt_of(h), "get_cursor", false, out);
}

yr_status api_walk(yr_host* h, yr_plan_position* out) {
    return position(rt_of(h), "get_walk_position", true, out);
}

yr_status api_scene(yr_host* h, const char* floor_id, char* buffer, std::uint32_t size,
                    std::uint32_t* out_length) {
    PluginRt* rt = rt_of(h);
    if (rt == nullptr) return YR_ERR_INVALID_ARGUMENT;
    yr_status st = YR_OK;
    try {
        const auto s = rt->host->snapshot();
        if (!s || !s->plan) {
            st = YR_ERR_NO_PLAN;
        } else {
            const std::string floor =
                floor_id == nullptr || *floor_id == '\0' ? s->plan->floor_id : std::string(floor_id);
            const auto it = s->scenes.find(floor);
            if (it == s->scenes.end())
                st = YR_ERR_NOT_FOUND;
            else
                st = radio::copy_text(it->second, buffer, size, out_length);
        }
    } catch (...) {
        st = YR_ERR_NO_MEMORY;
    }
    rt->count_ctx("get_scene", st);
    return st;
}

}  // namespace

// --- RadioHost -------------------------------------------------------------------------

RadioHost::RadioHost(RadioHostConfig config) : impl_(std::make_shared<Impl>()) {
    impl_->config = std::move(config);
    impl_->state =
        impl_->config.state ? impl_->config.state : std::make_shared<plugin::MemoryStateStore>();
}

RadioHost::~RadioHost() {
    Impl& m = *impl_;
    std::vector<PluginRt*> rts;
    {
        const std::lock_guard lock(m.mutex);
        for (auto& rt : m.plugins) rts.push_back(rt.get());
    }
    for (PluginRt* rt : rts) m.stop_context(*rt);
    m.stop_tasks();
    for (auto it = rts.rbegin(); it != rts.rend(); ++it) {
        PluginRt& rt = **it;
        if (rt.plugin != nullptr && rt.control && !rt.control->stuck()) {
            m.close_devices(rt);
            rt.d->shutdown(rt.plugin);
            rt.plugin = nullptr;
        }
        // Handles the app still holds no longer reach the plugin.
        const std::lock_guard lock(rt.dev_mutex);
        for (auto& w : rt.devices)
            if (auto d = w.lock()) {
                const std::lock_guard op(d->op);
                d->rt = nullptr;
            }
    }
    const std::lock_guard lock(m.mutex);
    for (auto& rt : m.plugins) {
        if (rt->control && rt->control->stuck()) {
            // A call never returned: keep its plugin and state mapped.
            static_cast<void>(rt.release());
        }
    }
    m.plugins.clear();
}

std::size_t RadioHost::load_all() {
    if (!impl_->on_main_thread()) return 0;
    std::size_t n = 0;
    for (const auto& path : plugin::discover_plugin_files(impl_->config.folders)) {
        load_file(path);
        ++n;
    }
    return n;
}

RadioPluginEntry RadioHost::load_file(const std::filesystem::path& path) {
    Impl& m = *impl_;
    if (!m.on_main_thread()) {
        RadioPluginEntry e;
        e.path = path;
        e.message = "load_file called outside the main thread";
        return e;
    }
    {
        const std::lock_guard lock(m.mutex);
        for (const auto& rt : m.plugins) {
            std::error_code ec;
            if (std::filesystem::equivalent(rt->path, path, ec)) return rt->entry;
        }
    }
    auto rt = std::make_unique<PluginRt>();
    rt->host = &m;
    rt->path = path;
    std::string error;
    rt->library = platform::DynamicLibrary::open(path, error);
    if (!rt->library) return m.refuse(path, "cannot be opened as a shared library: " + error);
    auto entry = reinterpret_cast<yr_entry_fn>(rt->library->symbol(YANTRA_RADIO_ENTRY_SYMBOL, error));
    if (entry == nullptr)
        return m.refuse(path, std::string("not a radio plugin: it does not export '") +
                                  YANTRA_RADIO_ENTRY_SYMBOL + "'");
    const yr_descriptor* d = entry();
    // RAD-11 first, from the frozen prefix only.
    const std::string refusal = radio::refusal_reason(yr_api_version_compiled(), d);
    if (!refusal.empty()) return m.refuse(path, refusal + " (RAD-11)", d);
    std::vector<std::string> problems = radio::validate_descriptor(d);
    if (d->init == nullptr || d->shutdown == nullptr || d->stream == nullptr || d->device == nullptr ||
        !YR_HAS_FIELD(d->stream, yr_stream_control, last_error) ||
        !YR_HAS_FIELD(d->device, yr_device_control, self_test))
        problems.insert(problems.begin(), "the descriptor lacks required functions");
    if (!problems.empty()) {
        std::string message;
        for (const auto& p : problems) message += (message.empty() ? "" : "; ") + p;
        return m.refuse(path, message, d);
    }
    if (m.find(text(d->id)) != nullptr)
        return m.refuse(path, "duplicate radio plugin id '" + text(d->id) + "'", d);

    rt->d = d;
    RadioPluginEntry& e = rt->entry;
    e.path = path;
    e.id = text(d->id);
    e.name = text(d->name);
    e.version = text(d->version);
    e.author = text(d->author);
    e.licence = text(d->licence);
    e.description = text(d->description);
    e.source_url = text(d->source_url);
    e.technology_id = text(d->technology_id);
    e.api_version = d->api_version;
    e.capabilities = d->capabilities;
    e.uses_context = (d->capabilities & YR_CAP_SCENE) != 0;
    e.status = RadioPluginStatus::Disabled;
    if (d->translations != nullptr)
        for (std::uint32_t i = 0; i < d->translation_count; ++i) {
            const yr_translation& t = d->translations[i];
            if (t.key != nullptr && t.lang != nullptr && t.text != nullptr)
                rt->translations[std::string(t.lang) + "\n" + t.key] = t.text;
        }
    for (std::uint32_t i = 0; i < d->schema_count; ++i) {
        auto names = std::make_shared<std::vector<std::string>>();
        for (std::uint32_t f = 0; f < d->schemas[i]->field_count; ++f)
            names->push_back(text(d->schemas[i]->fields[f]->name));
        rt->field_names.push_back(std::move(names));
    }

    yr_host_api& api = rt->api;
    api.struct_size = sizeof(yr_host_api);
    api.api_version = yr_api_version_compiled();
    api.host = &rt->handle;
    api.log = api_log;
    api.monotonic_ns = api_monotonic;
    api.utc_now_ns = api_utc;
    api.push_record = api_push;
    api.stream_event = api_event;
    api.devices_changed = api_devices_changed;
    api.settings_changed = api_settings_changed;
    if (e.uses_context) {  // RAD-14: only for SCENE plugins
        api.get_floor_plan = api_floor_plan;
        api.get_cursor = api_cursor;
        api.get_walk_position = api_walk;
        api.get_scene = api_scene;
    }
    PluginRt& r = *rt;
    {
        const std::lock_guard lock(m.mutex);
        m.plugins.push_back(std::move(rt));
    }
    m.log(r.id(), "", YR_LOG_INFO,
          "loaded " + r.entry.name + " " + r.entry.version + " (radio API " +
              std::to_string(d->api_version.major) + "." + std::to_string(d->api_version.minor) +
              ", " + r.entry.licence + ") from " + path.string());
    if (m.state->enabled(r.id()).value_or(true)) m.enable_rt(r);
    const std::lock_guard lock(m.mutex);
    return r.entry;
}

std::vector<RadioPluginEntry> RadioHost::list() const {
    const std::lock_guard lock(impl_->mutex);
    std::vector<RadioPluginEntry> out;
    for (const auto& rt : impl_->plugins) out.push_back(rt->entry);
    out.insert(out.end(), impl_->refused.begin(), impl_->refused.end());
    return out;
}

std::optional<RadioPluginEntry> RadioHost::entry(const std::string& id) const {
    const std::lock_guard lock(impl_->mutex);
    for (const auto& rt : impl_->plugins)
        if (rt->entry.id == id) return rt->entry;
    return std::nullopt;
}

const yr_descriptor* RadioHost::descriptor(const std::string& id) const {
    if (!impl_->on_main_thread()) return nullptr;
    const PluginRt* rt = impl_->find(id);
    return rt != nullptr ? rt->d : nullptr;
}

yr_status RadioHost::enable(const std::string& id) {
    if (!impl_->on_main_thread()) return YR_ERR_BUSY;
    PluginRt* rt = impl_->find(id);
    if (rt == nullptr) return YR_ERR_NOT_FOUND;
    if (rt->control && rt->control->stuck()) return YR_ERR_BUSY;
    impl_->state->set_enabled(id, true);
    if (rt->plugin != nullptr) impl_->disable_rt(*rt, RadioPluginStatus::Disabled, "");
    return impl_->enable_rt(*rt);
}

yr_status RadioHost::disable(const std::string& id) {
    if (!impl_->on_main_thread()) return YR_ERR_BUSY;
    PluginRt* rt = impl_->find(id);
    if (rt == nullptr) return YR_ERR_NOT_FOUND;
    impl_->state->set_enabled(id, false);
    impl_->disable_rt(*rt, RadioPluginStatus::Disabled, "");
    impl_->log(id, "", YR_LOG_INFO, rt->entry.name + " disabled by the user");
    return YR_OK;
}

EnumerateResult RadioHost::enumerate(const std::string& plugin_id) {
    PluginRt* rt = impl_->find(plugin_id);
    EnumerateResult out;
    if (rt == nullptr || rt->plugin == nullptr) {
        out.status = rt == nullptr ? YR_ERR_NOT_FOUND : YR_ERR_BUSY;
        out.diagnostic = rt == nullptr ? "no radio plugin " + plugin_id : "the plugin is not enabled";
        return out;
    }
    return impl_->enumerate(*rt);
}

std::shared_ptr<RadioDevice> RadioHost::open(const std::string& plugin_id, const std::string& key,
                                             std::string& error) {
    Impl& m = *impl_;
    PluginRt* rt = m.find(plugin_id);
    if (rt == nullptr || rt->plugin == nullptr || rt->failed.load()) {
        error = rt == nullptr ? "no radio plugin " + plugin_id
                              : rt->entry.name + " is not enabled" +
                                    (rt->entry.message.empty() ? "" : ": " + rt->entry.message);
        return nullptr;
    }
    yr_device* dev = nullptr;
    const yr_status st = m.open_device(*rt, key, dev);
    if (st != YR_OK) {
        error = std::string(yr_status_name(st)) + ": " + m.last_error(*rt, nullptr);
        return nullptr;
    }
    auto d = std::make_shared<RadioDevice::Impl>();
    d->rt = rt;
    d->host = m.weak_from_this();
    d->plugin_id = plugin_id;
    d->key = key;
    d->dev = dev;
    d->state = DeviceState::Open;
    {
        const std::lock_guard lock(rt->dev_mutex);
        std::erase_if(rt->devices, [](const auto& w) { return w.expired(); });
        rt->devices.push_back(d);
    }
    {
        const std::lock_guard op(d->op);
        m.apply_settings(*rt, *d);
        // HW-01b: the key from the first record on.
        m.refresh_key(*rt, *d, std::numeric_limits<std::int64_t>::min(), true);
    }
    return std::make_shared<RadioDevice>(d);
}

std::map<std::string, std::uint64_t> RadioHost::context_calls(const std::string& plugin_id) const {
    const PluginRt* rt = impl_->find(plugin_id);
    if (rt == nullptr) return {};
    const std::lock_guard lock(rt->ctx_mutex);
    return rt->ctx_calls;
}

std::map<std::uint32_t, std::uint64_t> RadioHost::context_notifications(
    const std::string& plugin_id) const {
    const PluginRt* rt = impl_->find(plugin_id);
    if (rt == nullptr) return {};
    const std::lock_guard lock(rt->n_mutex);
    return rt->n_sent;
}

bool RadioHost::wait_context_idle(const std::string& plugin_id, std::chrono::milliseconds timeout) const {
    const PluginRt* rt = impl_->find(plugin_id);
    if (rt == nullptr) return false;
    std::unique_lock lock(rt->n_mutex);
    return rt->n_cv.wait_for(lock, timeout,
                             [&] { return rt->n_stop || (rt->n_queue.empty() && !rt->n_busy); });
}

bool RadioHost::wait_calls_returned(const std::string& plugin_id,
                                    std::chrono::milliseconds timeout) const {
    const PluginRt* rt = impl_->find(plugin_id);
    if (rt == nullptr || !rt->control) return true;
    return rt->control->wait_unstuck(timeout);
}

std::uint64_t RadioHost::devices_changed_count(const std::string& plugin_id) const {
    const PluginRt* rt = impl_->find(plugin_id);
    return rt != nullptr ? rt->devices_changed.load() : 0;
}

std::string RadioHost::translation(const std::string& plugin_id, const std::string& text_key,
                                   const std::string& lang) const {
    const PluginRt* rt = impl_->find(plugin_id);
    if (rt == nullptr) return {};
    const auto it = rt->translations.find(lang + "\n" + text_key);
    return it != rt->translations.end() ? it->second : std::string{};
}

std::vector<LogLine> RadioHost::log_lines(const std::string& plugin_id) const {
    const std::lock_guard lock(impl_->log_mutex);
    const auto it = impl_->log_rings.find(plugin_id);
    if (it == impl_->log_rings.end()) return {};
    return {it->second.begin(), it->second.end()};
}

std::uint64_t RadioHost::settings_changed_count(const std::string& plugin_id) const {
    const PluginRt* rt = impl_->find(plugin_id);
    return rt != nullptr ? rt->settings_changed.load() : 0;
}

// --- RadioDevice ------------------------------------------------------------------------

RadioDevice::RadioDevice(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

RadioDevice::~RadioDevice() { close(); }

const std::string& RadioDevice::plugin_id() const noexcept { return impl_->plugin_id; }
const std::string& RadioDevice::key() const noexcept { return impl_->key; }

DeviceState RadioDevice::state() const {
    const std::lock_guard lock(impl_->m);
    return impl_->state;
}

std::uint64_t RadioDevice::stream_id() const {
    const std::lock_guard lock(impl_->m);
    return impl_->sink ? impl_->sink->stream_id : 0;
}

yr_status RadioDevice::start(const Plan& plan) {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return YR_ERR_NOT_FOUND;
    if (d.rt->failed.load()) return YR_ERR_FAILED;
    {
        const std::lock_guard lock(d.m);
        if (d.state == DeviceState::Lost) return YR_ERR_DEVICE_LOST;
        if (d.state != DeviceState::Open) return YR_ERR_BUSY;
    }
    const yr_status st = host->start_locked(*d.rt, d, plan);
    if (st == YR_OK) {
        const std::lock_guard lock(d.m);
        d.want_stream = true;
    }
    return st;
}

yr_status RadioDevice::set_plan(const Plan& plan) {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return YR_ERR_NOT_FOUND;
    auto storage = std::make_shared<Plan>(plan);
    const yr_descriptor* desc = d.rt->d;
    yr_device* dev = d.dev;
    const yr_status st = host->call(*d.rt, "set_plan", false, [desc, dev, storage] {
        yr_plan p{};
        p.struct_size = sizeof p;
        p.type = storage->type;
        p.serving_hz = storage->serving_hz;
        p.carriers_hz = storage->carriers_hz.empty() ? nullptr : storage->carriers_hz.data();
        p.carrier_count = static_cast<std::uint32_t>(storage->carriers_hz.size());
        p.flags = storage->flags;
        p.scan_first_hz = storage->scan_first_hz;
        p.scan_last_hz = storage->scan_last_hz;
        p.scan_step_hz = storage->scan_step_hz;
        return desc->stream->set_plan(dev, &p);
    });
    if (st == YR_OK) {
        const std::lock_guard lock(d.m);
        d.plan = plan;
    }
    return st;
}

yr_status RadioDevice::stop() {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return YR_ERR_NOT_FOUND;
    {
        const std::lock_guard lock(d.m);
        d.want_stream = false;
    }
    return host->stop_locked(*d.rt, d);
}

void RadioDevice::close() {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return;
    if (d.rt->control && !d.rt->control->stuck()) host->close_locked(*d.rt, d);
    PluginRt* rt = d.rt;
    {
        const std::lock_guard lock(rt->dev_mutex);
        std::erase_if(rt->devices, [&](const auto& w) {
            auto x = w.lock();
            return !x || x.get() == &d;
        });
    }
    d.rt = nullptr;
}

yr_status RadioDevice::set_setting(const std::string& key, const plugin::Value& value) {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return YR_ERR_NOT_FOUND;
    PluginRt& rt = *d.rt;
    auto apply = [&] {
        const CValue c = value_to_c(value);
        const yr_descriptor* desc = rt.d;
        yr_device* dev = d.dev;
        return host->call(rt, "set_setting", false, [desc, dev, key, c] {
            return desc->device->set_setting(dev, key.c_str(), &c.v);
        });
    };
    yr_status st = apply();
    bool streaming = false;
    std::optional<Plan> plan;
    {
        const std::lock_guard lock(d.m);
        streaming = d.state == DeviceState::Streaming;
        plan = d.plan;
    }
    if (st == YR_ERR_BUSY && streaming && plan) {
        // A RESTART setting (RAD-41): stop, apply, restart.
        host->stop_locked(rt, d);
        st = apply();
        const yr_status again = host->start_locked(rt, d, *plan);
        if (st == YR_OK) st = again;
    }
    // HW-01b: a CALIBRATION setting (or one that changes one) makes a new
    // key for the records measured from now on.
    if (st == YR_OK) {
        host->state->set_device_setting(d.plugin_id, d.key, key, value);
        host->refresh_key(rt, d, platform::monotonic_ns(), false);
    }
    return st;
}

yr_status RadioDevice::get_setting(const std::string& key, plugin::Value& out) {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return YR_ERR_NOT_FOUND;
    auto result = std::make_shared<plugin::Value>();
    const yr_descriptor* desc = d.rt->d;
    yr_device* dev = d.dev;
    const yr_status st = host->call(*d.rt, "get_setting", false, [desc, dev, key, result] {
        yr_value v = yr_value_none();
        char textbuf[1024] = {};
        const yr_status s = desc->device->get_setting(dev, key.c_str(), &v, textbuf, sizeof textbuf);
        if (s == YR_OK) *result = value_from_c(v);
        return s;
    });
    if (st == YR_OK) out = *result;
    return st;
}

std::shared_ptr<const calibration::Key> RadioDevice::calibration_key() const { return impl_->keys->latest(); }

std::optional<DeviceInfo> RadioDevice::info() const {
    const std::lock_guard lock(impl_->m);
    return impl_->info;
}

DeviceStats RadioDevice::stats() const {
    Impl& d = *impl_;
    DeviceStats out;
    std::shared_ptr<Sink> sink;
    {
        const std::lock_guard lock(d.m);
        out.sink = d.totals;
        out.streams = d.streams;
        out.reopens = d.reopens;
        sink = d.sink;
    }
    if (sink) {
        const SinkCounters c = sink->counters();
        out.sink.accepted += c.accepted;
        out.sink.delivered += c.delivered;
        out.sink.dropped += c.dropped;
        out.sink.invalid += c.invalid;
        out.sink.late += c.late;
        for (const auto& p : c.problems)
            if (out.sink.problems.size() < 10) out.sink.problems.push_back(p);
    }
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr || d.dev == nullptr) return out;
    auto stats = std::make_shared<yr_stream_stats>();
    const yr_descriptor* desc = d.rt->d;
    yr_device* dev = d.dev;
    const yr_status st = host->call(*d.rt, "stream_stats", false, [desc, dev, stats] {
        stats->struct_size = sizeof(yr_stream_stats);
        return desc->stream->stream_stats(dev, stats.get());
    });
    if (st == YR_OK) {
        out.plugin_ok = true;
        out.plugin = *stats;
    }
    return out;
}

namespace {

// Collects definitions through a yr_setting_cb.
void collect_def(void* user, const yr_setting_def* sd) {
    if (sd == nullptr || sd->key == nullptr) return;
    try {
        static_cast<std::vector<SettingDef>*>(user)->push_back(setting_from_c(*sd));
    } catch (...) {
    }
}

}  // namespace

// The device control calls of the radio windows (RAD-40..46): each one a
// control call with its RAD-15 limit, like the stream control.
template <class F>
static yr_status device_call(RadioDevice::Impl& d, const char* name, bool long_call, F f) {
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr || d.dev == nullptr) return YR_ERR_NOT_FOUND;
    return host->call(*d.rt, name, long_call, [f, desc = d.rt->d, dev = d.dev] { return f(desc, dev); });
}

yr_status RadioDevice::list_settings(std::vector<SettingDef>& out) {
    auto defs = std::make_shared<std::vector<SettingDef>>();
    const yr_status st = device_call(*impl_, "list_settings", false, [defs](const yr_descriptor* desc, yr_device* dev) {
        return desc->device->list_settings(dev, collect_def, defs.get());
    });
    if (st == YR_OK) out = *defs;
    return st;
}

yr_status RadioDevice::run_action(const std::string& key, std::string& result) {
    auto text_out = std::make_shared<std::string>();
    const yr_status st = device_call(*impl_, "run_action", true, [key, text_out](const yr_descriptor* desc, yr_device* dev) {
        if (desc->device->run_action == nullptr) return YR_ERR_UNSUPPORTED;
        char b[1024] = {};
        const yr_status s = desc->device->run_action(dev, key.c_str(), b, sizeof b);
        *text_out = b;
        return s;
    });
    result = *text_out;
    // An action may change settings (and with them the calibration key).
    if (st == YR_OK) {
        Impl& d = *impl_;
        const std::lock_guard op(d.op);
        if (auto host = d.host.lock(); host && d.rt != nullptr) host->refresh_key(*d.rt, d, platform::monotonic_ns(), false);
    }
    return st;
}

yr_status RadioDevice::list_presets(std::vector<Preset>& out) {
    auto presets = std::make_shared<std::vector<Preset>>();
    const yr_status st = device_call(*impl_, "list_presets", false, [presets](const yr_descriptor* desc, yr_device* dev) {
        if (desc->device->list_presets == nullptr) return YR_ERR_UNSUPPORTED;
        return desc->device->list_presets(
            dev,
            [](void* user, const yr_preset* p) {
                if (p == nullptr || p->id == nullptr) return;
                try {
                    static_cast<std::vector<Preset>*>(user)->push_back(Preset{text(p->id), text(p->label), text(p->help)});
                } catch (...) {
                }
            },
            presets.get());
    });
    if (st == YR_OK) out = *presets;
    return st;
}

yr_status RadioDevice::apply_preset(const std::string& id) {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    const yr_status st = device_call(d, "apply_preset", false, [id](const yr_descriptor* desc, yr_device* dev) {
        if (desc->device->apply_preset == nullptr) return YR_ERR_UNSUPPORTED;
        return desc->device->apply_preset(dev, id.c_str());
    });
    if (st != YR_OK) return st;
    // RAD-47: the values the preset set are stored like the user's own,
    // so the next open applies them again.
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return st;
    std::vector<SettingDef> defs;
    if (list_settings(defs) == YR_OK) {
        const auto stored = host->state->device_settings(d.plugin_id, d.key);
        for (const SettingDef& s : defs) {
            if (s.type == YR_TYPE_ACTION || s.has(YR_SETTING_READ_ONLY) || s.has(YR_SETTING_UNAVAILABLE)) continue;
            plugin::Value v;
            if (get_setting(s.key, v) != YR_OK) continue;
            const bool known = std::any_of(stored.begin(), stored.end(), [&](const auto& kv) { return kv.first == s.key; });
            if (known || !(v == s.default_value)) host->state->set_device_setting(d.plugin_id, d.key, s.key, v);
        }
    }
    host->refresh_key(*d.rt, d, platform::monotonic_ns(), false);
    return st;
}

yr_status RadioDevice::list_sensors(std::vector<SettingDef>& out) {
    auto defs = std::make_shared<std::vector<SettingDef>>();
    const yr_status st = device_call(*impl_, "list_sensors", false, [defs](const yr_descriptor* desc, yr_device* dev) {
        if (desc->device->list_sensors == nullptr) return YR_ERR_UNSUPPORTED;
        return desc->device->list_sensors(dev, collect_def, defs.get());
    });
    if (st == YR_OK) out = *defs;
    return st;
}

yr_status RadioDevice::read_sensor(const std::string& key, plugin::Value& out) {
    auto result = std::make_shared<plugin::Value>();
    const yr_status st = device_call(*impl_, "read_sensor", false, [key, result](const yr_descriptor* desc, yr_device* dev) {
        if (desc->device->read_sensor == nullptr) return YR_ERR_UNSUPPORTED;
        yr_value v = yr_value_none();
        char b[1024] = {};
        const yr_status s = desc->device->read_sensor(dev, key.c_str(), &v, b, sizeof b);
        if (s == YR_OK) *result = value_from_c(v);
        return s;
    });
    if (st == YR_OK) out = *result;
    return st;
}

yr_status RadioDevice::self_test(std::vector<SelfTestItem>& out) {
    auto items = std::make_shared<std::vector<SelfTestItem>>();
    const yr_status st = device_call(*impl_, "self_test", true, [items](const yr_descriptor* desc, yr_device* dev) {
        return desc->device->self_test(
            dev,
            [](void* user, const yr_selftest_item* i) {
                if (i == nullptr) return;
                try {
                    static_cast<std::vector<SelfTestItem>*>(user)->push_back(
                        SelfTestItem{i->result, text(i->name), text(i->label), text(i->detail), text(i->remedy)});
                } catch (...) {
                }
            },
            items.get());
    });
    out = *items;
    return st;
}

std::string RadioDevice::last_error() const {
    Impl& d = *impl_;
    const std::lock_guard op(d.op);
    auto host = d.host.lock();
    if (!host || d.rt == nullptr) return {};
    return host->last_error(*d.rt, d.dev);
}

}  // namespace yantra::radio
