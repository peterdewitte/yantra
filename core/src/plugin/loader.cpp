// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin loader (PLG-07, PLG-09, PLG-10), see loader.h.
// Copied from yantra-v2 68ea20a: core/src/plugin/manager.cpp (load_file,
// enable/disable, failure reports, host table), adapted for plugin API 1.0
// (no radio kind, no IQ channels, timers, emit_record, window requests).

#include "yantra/core/plugin/loader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <yantra/plugin.hpp>

#include "yantra/core/platform/clock.h"
#include "yantra/core/platform/dynlib.h"
#include "yantra/core/plugin/abi.h"
#include "yantra/core/plugin/folders.h"

namespace yantra::plugin {
struct Runtime;
}

// The host handle a plugin gets in yantra_host_api::host (opaque in plugin.h).
struct yantra_host {
    yantra::plugin::Runtime* rt;
};

namespace yantra::plugin {

const char* status_text(PluginStatus status) noexcept {
    switch (status) {
    case PluginStatus::Enabled: return "enabled";
    case PluginStatus::Disabled: return "disabled";
    case PluginStatus::Failed: return "failed";
    case PluginStatus::Refused: return "refused";
    }
    return "unknown";
}

std::string kinds_text(std::uint32_t kinds) {
    std::string out;
    auto add = [&](std::uint32_t bit, const char* name) {
        if ((kinds & bit) == 0) return;
        if (!out.empty()) out += ", ";
        out += name;
    };
    add(YANTRA_KIND_TECHNOLOGY, "technology");
    add(YANTRA_KIND_FEATURE, "feature");
    add(YANTRA_KIND_EXPORTER, "exporter");
    return out;
}

// One loaded plugin. Written on the main thread; the host functions read
// the immutable parts (info, descriptor) from any thread.
struct Runtime {
    PluginLoader::Impl* m = nullptr;
    std::filesystem::path path;
    std::unique_ptr<platform::DynamicLibrary> library;
    const yantra_plugin_descriptor* descriptor = nullptr;
    PluginInfo info;
    yantra_host handle{this};
    yantra_host_api api{};
    yantra_plugin_instance* instance = nullptr;  // guarded by Impl::timer_mutex for the timer thread
    std::atomic<bool> timers_allowed{false};
    PluginStatus status = PluginStatus::Disabled;
    std::string message;
};

struct PluginLoader::Impl {
    PluginLoaderConfig config;
    std::shared_ptr<PluginStateStore> state;
    std::thread::id main_thread = std::this_thread::get_id();
    std::vector<std::unique_ptr<Runtime>> runtimes;
    std::vector<PluginEntry> refused;

    mutable std::mutex snapshot_mutex;
    std::vector<PluginEntry> snapshot;

    std::mutex failure_mutex;
    std::vector<std::pair<std::string, std::string>> failures;

    std::mutex recording_mutex;
    std::shared_ptr<RecordingSink> recording;

    std::mutex ui_mutex;
    std::vector<UiRequest> ui_requests;

    // Timers (yantra_host_api::timer_start): one worker thread for all.
    struct Timer {
        Runtime* rt = nullptr;
        std::uint32_t id = 0;
        std::chrono::milliseconds period{0};
        std::chrono::steady_clock::time_point due{};
    };
    std::mutex timer_mutex;
    std::condition_variable timer_cv;
    std::vector<Timer> timers;
    const Runtime* timer_running = nullptr;
    bool timer_stop = false;
    std::thread timer_thread;

    bool on_main_thread() const { return std::this_thread::get_id() == main_thread; }

    void log(const std::string& id, std::uint32_t level, const std::string& message) const {
        if (config.log) config.log(id, level, message);
    }

    Runtime* find(const std::string& id) const {
        for (const auto& rt : runtimes)
            if (rt->info.id == id) return rt.get();
        return nullptr;
    }

    static PluginEntry entry_of(const Runtime& rt) {
        PluginEntry e;
        e.path = rt.path;
        e.id = rt.info.id;
        e.name = rt.info.name;
        e.version = rt.info.version;
        e.author = rt.info.author;
        e.licence = rt.info.licence;
        e.description = rt.info.description;
        e.api_version = rt.info.api_version;
        e.kinds = rt.info.kinds;
        e.status = rt.status;
        e.message = rt.message;
        return e;
    }

    void publish() {
        std::vector<PluginEntry> list;
        for (const auto& rt : runtimes) list.push_back(entry_of(*rt));
        list.insert(list.end(), refused.begin(), refused.end());
        const std::lock_guard lock(snapshot_mutex);
        snapshot = std::move(list);
    }

    PluginEntry refuse(const std::filesystem::path& path, const std::string& message,
                       const yantra_plugin_descriptor* d = nullptr) {
        PluginEntry e;
        e.path = path;
        e.status = PluginStatus::Refused;
        e.message = message;
        if (d != nullptr && d->struct_size >= plugin_api::descriptor_prefix_size) {
            e.api_version = d->api_version;
            e.kinds = d->kinds;
            if (YANTRA_HAS_FIELD(d, yantra_plugin_descriptor, ui) &&
                d->api_version.major == YANTRA_PLUGIN_API_VERSION_MAJOR) {
                e.id = text_of(d->id);
                e.name = text_of(d->name);
                e.version = text_of(d->version);
                e.author = text_of(d->author);
                e.licence = text_of(d->licence);
                e.description = text_of(d->description);
            }
        }
        log("", YANTRA_LOG_WARNING, path.filename().string() + " refused: " + message);
        refused.push_back(e);
        publish();
        return e;
    }

    // --- timers ---------------------------------------------------------------

    void timer_main() {
        std::unique_lock lock(timer_mutex);
        while (!timer_stop) {
            if (timers.empty()) {
                timer_cv.wait(lock, [&] { return timer_stop || !timers.empty(); });
                continue;
            }
            auto it = std::min_element(timers.begin(), timers.end(),
                                       [](const Timer& a, const Timer& b) { return a.due < b.due; });
            const auto now = std::chrono::steady_clock::now();
            if (now < it->due) {
                timer_cv.wait_until(lock, it->due);
                continue;
            }
            Runtime* rt = it->rt;
            const std::uint32_t id = it->id;
            it->due += it->period;
            if (it->due < now) it->due = now + it->period;
            yantra_plugin_instance* instance = rt->instance;
            if (instance == nullptr || rt->descriptor->on_timer == nullptr) continue;
            timer_running = rt;
            lock.unlock();
            rt->descriptor->on_timer(instance, id);
            lock.lock();
            timer_running = nullptr;
            timer_cv.notify_all();
        }
    }

    yantra_status timer_start(Runtime& rt, std::uint32_t id, std::uint32_t period_ms) {
        if (rt.descriptor->on_timer == nullptr) return YANTRA_ERR_UNSUPPORTED;
        if (period_ms < 10) return YANTRA_ERR_INVALID_ARGUMENT;
        if (!rt.timers_allowed.load()) return YANTRA_ERR_BUSY;
        const std::lock_guard lock(timer_mutex);
        const auto period = std::chrono::milliseconds(period_ms);
        const auto due = std::chrono::steady_clock::now() + period;
        auto it = std::find_if(timers.begin(), timers.end(),
                               [&](const Timer& t) { return t.rt == &rt && t.id == id; });
        if (it != timers.end()) {
            it->period = period;
            it->due = due;
        } else {
            timers.push_back({&rt, id, period, due});
        }
        if (!timer_thread.joinable()) timer_thread = std::thread([this] { timer_main(); });
        timer_cv.notify_all();
        return YANTRA_OK;
    }

    yantra_status timer_stop_one(Runtime& rt, std::uint32_t id) {
        const std::lock_guard lock(timer_mutex);
        auto it = std::find_if(timers.begin(), timers.end(),
                               [&](const Timer& t) { return t.rt == &rt && t.id == id; });
        if (it == timers.end()) return YANTRA_ERR_NOT_FOUND;
        timers.erase(it);
        timer_cv.notify_all();
        return YANTRA_OK;
    }

    // Removes every timer of `rt` and waits for a running on_timer of it.
    void stop_timers(Runtime& rt) {
        rt.timers_allowed = false;
        std::unique_lock lock(timer_mutex);
        std::erase_if(timers, [&](const Timer& t) { return t.rt == &rt; });
        timer_cv.notify_all();
        timer_cv.wait(lock, [&] { return timer_running != &rt; });
    }

    // --- enable / disable ------------------------------------------------------

    yantra_status enable_runtime(Runtime& rt) {
        if (rt.status == PluginStatus::Enabled) return YANTRA_OK;
        rt.timers_allowed = true;
        yantra_plugin_instance* instance = nullptr;
        const yantra_status st = rt.descriptor->init(&rt.api, &instance);
        if (st != YANTRA_OK || instance == nullptr) {
            stop_timers(rt);
            rt.status = PluginStatus::Failed;
            rt.message = std::string("init failed: ") +
                         (st == YANTRA_OK ? "no instance returned" : plugin_api::status_name(st));
            log(rt.info.id, YANTRA_LOG_ERROR, rt.info.name + " disabled: " + rt.message);
            return st == YANTRA_OK ? YANTRA_ERR_FAILED : st;
        }
        {
            const std::lock_guard lock(timer_mutex);
            rt.instance = instance;
        }
        rt.status = PluginStatus::Enabled;
        rt.message.clear();
        log(rt.info.id, YANTRA_LOG_INFO, rt.info.name + " enabled");
        return YANTRA_OK;
    }

    void disable_runtime(Runtime& rt, PluginStatus status, const std::string& message) {
        if (rt.status == PluginStatus::Enabled) {
            stop_timers(rt);
            yantra_plugin_instance* instance = nullptr;
            {
                const std::lock_guard lock(timer_mutex);
                instance = rt.instance;
                rt.instance = nullptr;
            }
            rt.descriptor->shutdown(instance);
        }
        rt.status = status;
        rt.message = message;
        if (status == PluginStatus::Failed)
            log(rt.info.id, YANTRA_LOG_ERROR, rt.info.name + " disabled: " + message);
    }
};

// --- host table -------------------------------------------------------------------

namespace {

Runtime* rt_of(yantra_host* h) { return h != nullptr ? h->rt : nullptr; }

bool valid_setting_key(const char* key) {
    if (key == nullptr || *key == '\0') return false;
    for (const char* c = key; *c != '\0'; ++c) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' ||
                        *c == '.';
        if (!ok) return false;
    }
    return true;
}

void host_log(yantra_host* h, std::uint32_t level, const char* message) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr) return;
    try {
        rt->m->log(rt->info.id.empty() ? rt->path.filename().string() : rt->info.id,
                   std::min<std::uint32_t>(level, YANTRA_LOG_ERROR), text_of(message));
    } catch (...) {
    }
}

void host_notify(yantra_host* h, std::uint32_t level, const char* text) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr) return;
    try {
        const std::uint32_t lv = std::clamp<std::uint32_t>(level, YANTRA_LOG_INFO, YANTRA_LOG_ERROR);
        rt->m->log(rt->info.id, lv, text_of(text));
        if (rt->m->config.notify) rt->m->config.notify(rt->info.id, lv, text_of(text));
    } catch (...) {
    }
}

yantra_status host_settings_get(yantra_host* h, const char* key, char* buffer,
                                std::uint32_t buffer_size, std::uint32_t* out_length) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr || !valid_setting_key(key)) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&] {
        const std::optional<std::string> v = rt->m->state->setting(rt->info.id, key);
        if (!v) return YANTRA_ERR_NOT_FOUND;
        return plugin_api::copy_text(*v, buffer, buffer_size, out_length);
    });
}

yantra_status host_settings_set(yantra_host* h, const char* key, const char* value) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr || !valid_setting_key(key)) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&] {
        rt->m->state->set_setting(rt->info.id, key,
                                  value != nullptr ? std::optional<std::string>{value}
                                                   : std::nullopt);
        return YANTRA_OK;
    });
}

std::int64_t host_monotonic(yantra_host*) { return platform::monotonic_ns(); }

yantra_status host_now(yantra_host*, yantra_time* out) {
    if (out == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    out->monotonic_ns = platform::monotonic_ns();
    out->utc_ns = platform::utc_now_ns();
    out->utc_source = YANTRA_TIME_SOURCE_SYSTEM;
    out->reserved = 0;
    return YANTRA_OK;
}

yantra_status host_timer_start(yantra_host* h, std::uint32_t id, std::uint32_t period_ms) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&] { return rt->m->timer_start(*rt, id, period_ms); });
}

yantra_status host_timer_stop(yantra_host* h, std::uint32_t id) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&] { return rt->m->timer_stop_one(*rt, id); });
}

yantra_status host_emit_record(yantra_host* h, const yantra_record* record) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr || record == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&]() -> yantra_status {
        if (!rt->info.feature) return YANTRA_ERR_UNSUPPORTED;
        const auto& schemas = rt->info.feature->schemas;
        if (record->struct_size < sizeof(yantra_record) || record->schema_index >= schemas.size())
            return YANTRA_ERR_INVALID_ARGUMENT;
        RecordCheck check = parse_record(record, schemas[record->schema_index]);
        if (!check.parsed.ok()) {
            rt->m->log(rt->info.id, YANTRA_LOG_DEBUG,
                       "record refused: " + (check.parsed.errors.empty()
                                                 ? std::string("invalid")
                                                 : check.parsed.errors.front()));
            return check.scope_violation ? YANTRA_ERR_SCOPE : YANTRA_ERR_INVALID_ARGUMENT;
        }
        std::shared_ptr<RecordingSink> sink;
        {
            const std::lock_guard lock(rt->m->recording_mutex);
            sink = rt->m->recording;
        }
        if (!sink) return YANTRA_ERR_BUSY;
        sink->on_feature_record(rt->info.id, *check.parsed.value);
        return YANTRA_OK;
    });
}

yantra_status window_request(yantra_host* h, const char* window_id, bool open) {
    Runtime* rt = rt_of(h);
    if (rt == nullptr || window_id == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    return plugin_api::guarded([&] {
        const auto& w = rt->info.ui.windows;
        if (std::none_of(w.begin(), w.end(), [&](const UiWindow& x) { return x.id == window_id; }))
            return YANTRA_ERR_NOT_FOUND;
        const std::lock_guard lock(rt->m->ui_mutex);
        rt->m->ui_requests.push_back({rt->info.id, window_id, open});
        return YANTRA_OK;
    });
}

yantra_status host_ui_open(yantra_host* h, const char* id) { return window_request(h, id, true); }
yantra_status host_ui_close(yantra_host* h, const char* id) { return window_request(h, id, false); }

yantra_host_api make_host_api(Runtime& rt) {
    yantra_host_api api{};
    api.struct_size = sizeof(yantra_host_api);
    api.api_version = host_api_version();
    api.host = &rt.handle;
    api.log = host_log;
    api.notify = host_notify;
    api.settings_get = host_settings_get;
    api.settings_set = host_settings_set;
    api.monotonic_ns = host_monotonic;
    api.now = host_now;
    api.timer_start = host_timer_start;
    api.timer_stop = host_timer_stop;
    api.emit_record = host_emit_record;
    api.ui_open_window = host_ui_open;
    api.ui_close_window = host_ui_close;
    return api;
}

std::string joined(const std::vector<std::string>& errors) {
    std::string out;
    for (const std::string& e : errors) out += (out.empty() ? "" : "; ") + e;
    return out;
}

}  // namespace

// --- loader ------------------------------------------------------------------------

PluginLoader::PluginLoader(PluginLoaderConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->state = impl_->config.state ? impl_->config.state : std::make_shared<MemoryStateStore>();
}

PluginLoader::~PluginLoader() {
    unload_all();
    {
        const std::lock_guard lock(impl_->timer_mutex);
        impl_->timer_stop = true;
    }
    impl_->timer_cv.notify_all();
    if (impl_->timer_thread.joinable()) impl_->timer_thread.join();
}

std::size_t PluginLoader::load_all() {
    if (!impl_->on_main_thread()) return 0;
    std::size_t n = 0;
    for (const auto& path : discover_plugin_files(impl_->config.folders)) {
        load_file(path);
        ++n;
    }
    return n;
}

PluginEntry PluginLoader::load_file(const std::filesystem::path& path) {
    Impl& m = *impl_;
    if (!m.on_main_thread()) {
        PluginEntry e;
        e.path = path;
        e.message = "load_file called outside the main thread";
        return e;
    }
    for (const auto& rt : m.runtimes) {
        std::error_code ec;
        if (std::filesystem::equivalent(rt->path, path, ec)) return Impl::entry_of(*rt);
    }
    const std::string file = path.filename().string();
    auto rt = std::make_unique<Runtime>();
    rt->m = &m;
    rt->path = path;
    rt->api = make_host_api(*rt);

    std::string error;
    rt->library = platform::DynamicLibrary::open(path, error);
    if (!rt->library) return m.refuse(path, "cannot be opened as a shared library: " + error);
    auto* entry = reinterpret_cast<yantra_plugin_entry_fn>(
        rt->library->symbol(YANTRA_PLUGIN_ENTRY_SYMBOL, error));
    if (entry == nullptr)
        return m.refuse(path, std::string("not a Yantra plugin: it does not export '") +
                                  YANTRA_PLUGIN_ENTRY_SYMBOL + "'");
    const yantra_plugin_descriptor* d = entry(&rt->api);
    if (d == nullptr)
        return m.refuse(path, "the plugin declined to run with this Yantra (plugin API " +
                                  plugin_api::version_text(host_api_version()) +
                                  "); it may need a newer Yantra");
    // Version first (frozen prefix, PLG-04), then the whole descriptor with
    // the licence check (PLG-10).
    Parsed<PluginInfo> parsed = parse_descriptor(d, host_api_version(), file);
    if (!parsed.ok()) return m.refuse(path, joined(parsed.errors), d);
    if (const Runtime* other = m.find(parsed.value->id))
        return m.refuse(path,
                        "duplicate plugin id '" + parsed.value->id + "': already loaded from " +
                            other->path.string(),
                        d);
    rt->descriptor = d;
    rt->info = std::move(*parsed.value);
    rt->status = PluginStatus::Disabled;
    Runtime& r = *rt;
    m.runtimes.push_back(std::move(rt));
    m.log(r.info.id, YANTRA_LOG_INFO,
          "loaded " + r.info.name + " " + r.info.version + " (" + kinds_text(r.info.kinds) + ", " +
              r.info.licence + ") from " + path.string());
    if (m.state->enabled(r.info.id).value_or(true)) m.enable_runtime(r);
    m.publish();
    return Impl::entry_of(r);
}

std::vector<PluginEntry> PluginLoader::list() const {
    const std::lock_guard lock(impl_->snapshot_mutex);
    return impl_->snapshot;
}

std::optional<PluginEntry> PluginLoader::entry(const std::string& id) const {
    const std::lock_guard lock(impl_->snapshot_mutex);
    for (const PluginEntry& e : impl_->snapshot)
        if (e.id == id && e.status != PluginStatus::Refused) return e;
    return std::nullopt;
}

const PluginInfo* PluginLoader::info(const std::string& id) const {
    if (!impl_->on_main_thread()) return nullptr;
    const Runtime* rt = impl_->find(id);
    return rt != nullptr ? &rt->info : nullptr;
}

const yantra_plugin_descriptor* PluginLoader::descriptor(const std::string& id) const {
    if (!impl_->on_main_thread()) return nullptr;
    const Runtime* rt = impl_->find(id);
    return rt != nullptr && rt->status == PluginStatus::Enabled ? rt->descriptor : nullptr;
}

yantra_plugin_instance* PluginLoader::instance(const std::string& id) const {
    if (!impl_->on_main_thread()) return nullptr;
    const Runtime* rt = impl_->find(id);
    return rt != nullptr && rt->status == PluginStatus::Enabled ? rt->instance : nullptr;
}

yantra_status PluginLoader::enable(const std::string& id) {
    Impl& m = *impl_;
    if (!m.on_main_thread()) return YANTRA_ERR_BUSY;
    Runtime* rt = m.find(id);
    if (rt == nullptr) return YANTRA_ERR_NOT_FOUND;
    m.state->set_enabled(id, true);
    const yantra_status st = m.enable_runtime(*rt);
    m.publish();
    return st;
}

yantra_status PluginLoader::disable(const std::string& id) {
    Impl& m = *impl_;
    if (!m.on_main_thread()) return YANTRA_ERR_BUSY;
    Runtime* rt = m.find(id);
    if (rt == nullptr) return YANTRA_ERR_NOT_FOUND;
    m.state->set_enabled(id, false);
    m.disable_runtime(*rt, PluginStatus::Disabled, "");
    m.log(id, YANTRA_LOG_INFO, rt->info.name + " disabled by the user");
    m.publish();
    return YANTRA_OK;
}

void PluginLoader::unload_all() {
    Impl& m = *impl_;
    if (!m.on_main_thread()) return;
    // Reverse load order: a feature plugin may depend on an earlier one.
    for (auto it = m.runtimes.rbegin(); it != m.runtimes.rend(); ++it)
        m.disable_runtime(**it, PluginStatus::Disabled, "");
    m.runtimes.clear();
    m.refused.clear();
    m.publish();
}

void PluginLoader::report_failure(const std::string& id, const std::string& message) {
    const std::lock_guard lock(impl_->failure_mutex);
    impl_->failures.emplace_back(id, message);
}

std::size_t PluginLoader::poll() {
    Impl& m = *impl_;
    if (!m.on_main_thread()) return 0;
    std::vector<std::pair<std::string, std::string>> failures;
    {
        const std::lock_guard lock(m.failure_mutex);
        failures.swap(m.failures);
    }
    std::size_t n = 0;
    for (const auto& [id, message] : failures) {
        Runtime* rt = m.find(id);
        if (rt == nullptr || rt->status != PluginStatus::Enabled) continue;
        m.disable_runtime(*rt, PluginStatus::Failed, message);
        ++n;
    }
    if (n > 0) m.publish();
    return n;
}

void PluginLoader::set_recording_sink(std::shared_ptr<RecordingSink> sink) {
    const std::lock_guard lock(impl_->recording_mutex);
    impl_->recording = std::move(sink);
}

std::vector<UiRequest> PluginLoader::take_ui_requests() {
    std::vector<UiRequest> out;
    const std::lock_guard lock(impl_->ui_mutex);
    out.swap(impl_->ui_requests);
    return out;
}

PluginStateStore& PluginLoader::state() noexcept { return *impl_->state; }

}  // namespace yantra::plugin
