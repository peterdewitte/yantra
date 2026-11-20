// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio test host library (RAD-91): plugin loading and refusal
// (RAD-11), host table (RAD-14), record sink (RAD-20, RAD-24, RAD-33),
// emulated context callbacks and notifications (RAD-18), device control
// wrappers (RAD-30..46).
#include "host.hpp"

#include <yantra/radio.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The opaque handles of radio.h, defined by the host side.
struct yr_host {
    yantra::radio_host::Host* host;
};
struct yr_stream {
    yantra::radio_host::StreamSink* sink;
};

namespace yantra::radio_host {

namespace {

using radios::Event;
using radios::Record;

int64_t monotonic_now()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t utc_now()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Copies `text` into a fixed buffer, cut at a UTF-8 character boundary.
template <std::size_t N>
void copy_fixed(char (&dst)[N], const std::string& text)
{
    std::size_t n = std::min(text.size(), N - 1);
    while (n > 0 && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xC0u) == 0x80u)
        --n;
    std::memcpy(dst, text.data(), n);
    dst[n] = '\0';
}

std::string text(const char* s)
{
    return s != nullptr ? std::string(s) : std::string();
}

Value value_from_c(const yr_value& v)
{
    Value out;
    out.type = v.type;
    switch (v.type) {
    case YR_TYPE_BOOL:
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
        out.i64 = v.u.i64;
        break;
    case YR_TYPE_UINT:
        out.u64 = v.u.u64;
        break;
    case YR_TYPE_DOUBLE:
        out.f64 = v.u.f64;
        break;
    case YR_TYPE_STRING:
        out.str = text(v.u.str);
        break;
    default:
        out.type = YR_TYPE_NONE;
        break;
    }
    return out;
}

yr_value value_to_c(const Value& v)
{
    yr_value out = yr_value_none();
    out.type = v.type;
    switch (v.type) {
    case YR_TYPE_BOOL:
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
        out.u.i64 = v.i64;
        break;
    case YR_TYPE_UINT:
        out.u.u64 = v.u64;
        break;
    case YR_TYPE_DOUBLE:
        out.u.f64 = v.f64;
        break;
    case YR_TYPE_STRING:
        out.u.str = v.str.c_str();
        break;
    default:
        out.type = YR_TYPE_NONE;
        break;
    }
    return out;
}

// --- the host table ---------------------------------------------------------

Host* host_of(yr_host* h)
{
    return h != nullptr ? h->host : nullptr;
}

void api_log(yr_host* h, yr_device* device, uint32_t level, const char* message)
{
    if (Host* host = host_of(h))
        host->on_log(device, level, message);
}

int64_t api_monotonic(yr_host*)
{
    return monotonic_now();
}

int64_t api_utc(yr_host*)
{
    return utc_now();
}

yr_status api_push(yr_stream* stream, const yr_record* record);
yr_status api_push_spectrum(yr_stream* stream, const yr_spectrum* spectrum);
void api_event(yr_stream* stream, uint32_t event, int64_t value);

void api_devices_changed(yr_host* h)
{
    if (Host* host = host_of(h))
        host->on_devices_changed();
}

void api_settings_changed(yr_host* h, yr_device*)
{
    if (Host* host = host_of(h))
        host->on_settings_changed();
}

yr_status api_floor_plan(yr_host* h, yr_floor_plan_info* out)
{
    Host* host = host_of(h);
    return host != nullptr ? host->context().get_floor_plan(out) : YR_ERR_INVALID_ARGUMENT;
}

yr_status api_cursor(yr_host* h, yr_plan_position* out)
{
    Host* host = host_of(h);
    return host != nullptr ? host->context().get_cursor(out) : YR_ERR_INVALID_ARGUMENT;
}

yr_status api_walk(yr_host* h, yr_plan_position* out)
{
    Host* host = host_of(h);
    return host != nullptr ? host->context().get_walk_position(out) : YR_ERR_INVALID_ARGUMENT;
}

yr_status api_scene(yr_host* h, const char* floor_id, char* buffer, uint32_t size,
                    uint32_t* out_length)
{
    Host* host = host_of(h);
    return host != nullptr ? host->context().get_scene(floor_id, buffer, size, out_length)
                           : YR_ERR_INVALID_ARGUMENT;
}

} // namespace

const char* host_version()
{
    return YANTRA_VERSION; // the product version (PLG-29)
}

// --- emulated context -------------------------------------------------------

bool EmulatedContext::configure(const ContextOptions& options, std::string& error)
{
    std::vector<Floor> floors;
    for (const std::string& text_json : options.scene_json) {
        radios::Scene scene;
        if (!radios::parse_scene(text_json, scene, error)) {
            error = "scene: " + error;
            return false;
        }
        for (const Floor& f : floors)
            if (f.id == scene.floor.id) {
                error = "scene: floor id \"" + f.id + "\" given twice";
                return false;
            }
        floors.push_back({scene.floor.id, scene.floor.name, scene.floor.scale_set,
                          scene.floor.width_m, scene.floor.height_m,
                          radios::scene_to_json(scene)});
    }
    if (floors.empty()) {
        radios::Scene scene;
        scene.floor.id = "floor-1";
        scene.floor.name = "Floor 1";
        floors.push_back({scene.floor.id, scene.floor.name, false, 0.0, 0.0,
                          radios::scene_to_json(scene)});
    }
    if (options.no_scene)
        for (Floor& f : floors)
            f.json.clear();
    std::size_t shown = 0;
    if (!options.shown_floor.empty()) {
        auto it = std::find_if(floors.begin(), floors.end(),
                               [&](const Floor& f) { return f.id == options.shown_floor; });
        if (it == floors.end()) {
            error = "no scene for floor \"" + options.shown_floor + "\"";
            return false;
        }
        shown = static_cast<std::size_t>(it - floors.begin());
    }
    std::lock_guard lock(mutex_);
    options_ = options;
    floors_ = std::move(floors);
    shown_ = shown;
    plan_shown_ = options.plan;
    const int64_t now = monotonic_now();
    cursor_.reset();
    walk_.reset();
    if (!options.cursor.empty())
        cursor_ = options.cursor.front();
    if (!options.walk.empty())
        walk_ = options.walk.front();
    cursor_moved_ns_ = walk_moved_ns_ = now;
    path_index_ = 0;
    records_seen_ = 0;
    return true;
}

const EmulatedContext::Floor* EmulatedContext::shown_floor_locked() const
{
    if (!plan_shown_ || floors_.empty())
        return nullptr;
    return &floors_[shown_];
}

void EmulatedContext::count_locked(const char* fn, yr_status st)
{
    ++calls_[std::string(fn) + ":" + yr_status_name(st)];
}

yr_status EmulatedContext::get_floor_plan(yr_floor_plan_info* out)
{
    std::lock_guard lock(mutex_);
    yr_status st = YR_OK;
    const Floor* f = shown_floor_locked();
    if (out == nullptr) {
        st = YR_ERR_INVALID_ARGUMENT;
    } else if (out->struct_size < sizeof(yr_floor_plan_info)) {
        st = YR_ERR_VERSION;
    } else if (f == nullptr) {
        st = YR_ERR_NO_PLAN;
    } else {
        out->scale_set = f->scale_set ? 1u : 0u;
        out->width_m = f->scale_set ? f->width_m : 0.0;
        out->height_m = f->scale_set ? f->height_m : 0.0;
        copy_fixed(out->plan_name, options_.plan_name);
        copy_fixed(out->example_id, options_.example_id);
        copy_fixed(out->floor_id, f->id);
        copy_fixed(out->floor_name, f->name);
    }
    count_locked("get_floor_plan", st);
    return st;
}

yr_status EmulatedContext::position_locked(const std::optional<PlanPoint>& p, int64_t moved_ns,
                                           yr_plan_position* out) const
{
    if (out == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    if (out->struct_size < sizeof(yr_plan_position))
        return YR_ERR_VERSION;
    const Floor* f = shown_floor_locked();
    if (f == nullptr || !f->scale_set || !p)
        return YR_ERR_NO_POSITION;
    out->reserved = 0;
    out->x_m = p->x_m;
    out->y_m = p->y_m;
    out->moved_monotonic_ns = moved_ns;
    copy_fixed(out->floor_id, f->id);
    copy_fixed(out->floor_name, f->name);
    return YR_OK;
}

yr_status EmulatedContext::get_cursor(yr_plan_position* out)
{
    std::lock_guard lock(mutex_);
    const yr_status st = position_locked(cursor_, cursor_moved_ns_, out);
    count_locked("get_cursor", st);
    return st;
}

yr_status EmulatedContext::get_walk_position(yr_plan_position* out)
{
    std::lock_guard lock(mutex_);
    const yr_status st = position_locked(walk_, walk_moved_ns_, out);
    count_locked("get_walk_position", st);
    return st;
}

yr_status EmulatedContext::get_scene(const char* floor_id, char* buffer, uint32_t buffer_size,
                                     uint32_t* out_length)
{
    std::lock_guard lock(mutex_);
    yr_status st = YR_OK;
    const Floor* f = nullptr;
    if (shown_floor_locked() == nullptr) {
        st = YR_ERR_NO_PLAN;
    } else if (floor_id == nullptr || floor_id[0] == '\0') {
        f = shown_floor_locked();
    } else {
        for (const Floor& candidate : floors_)
            if (candidate.id == floor_id)
                f = &candidate;
        if (f == nullptr)
            st = YR_ERR_NOT_FOUND;
    }
    if (f != nullptr && f->json.empty()) {
        st = YR_ERR_NOT_FOUND; // a floor without a scene
    } else if (f != nullptr) {
        const auto len = static_cast<uint32_t>(f->json.size());
        if (out_length != nullptr)
            *out_length = len;
        if (buffer == nullptr || buffer_size < len + 1u) {
            st = YR_ERR_BUFFER_TOO_SMALL;
        } else {
            std::memcpy(buffer, f->json.data(), len);
            buffer[len] = '\0';
        }
    }
    count_locked("get_scene", st);
    return st;
}

std::vector<uint32_t> EmulatedContext::set_plan_shown(bool shown)
{
    std::lock_guard lock(mutex_);
    if (plan_shown_ == shown)
        return {};
    plan_shown_ = shown;
    return {YR_CONTEXT_PLAN_CHANGED};
}

std::vector<uint32_t> EmulatedContext::set_cursor(std::optional<PlanPoint> p)
{
    std::lock_guard lock(mutex_);
    cursor_ = p;
    cursor_moved_ns_ = monotonic_now();
    return {YR_CONTEXT_CURSOR_MOVED};
}

std::vector<uint32_t> EmulatedContext::set_walk(std::optional<PlanPoint> p)
{
    std::lock_guard lock(mutex_);
    walk_ = p;
    walk_moved_ns_ = monotonic_now();
    return {YR_CONTEXT_WALK_POSITION};
}

std::vector<uint32_t> EmulatedContext::on_record()
{
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> events;
    if (options_.path_step_records == 0)
        return events;
    ++records_seen_;
    if (records_seen_ % options_.path_step_records != 0)
        return events;
    ++path_index_;
    const int64_t now = monotonic_now();
    if (path_index_ < options_.cursor.size()) {
        cursor_ = options_.cursor[path_index_];
        cursor_moved_ns_ = now;
        events.push_back(YR_CONTEXT_CURSOR_MOVED);
    }
    if (path_index_ < options_.walk.size()) {
        walk_ = options_.walk[path_index_];
        walk_moved_ns_ = now;
        events.push_back(YR_CONTEXT_WALK_POSITION);
    }
    return events;
}

std::map<std::string, uint64_t> EmulatedContext::call_counts() const
{
    std::lock_guard lock(mutex_);
    return calls_;
}

std::vector<std::string> EmulatedContext::context_texts() const
{
    std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    auto add = [&](const std::string& s) {
        if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(s);
    };
    add(options_.plan_name);
    add(options_.example_id);
    for (const Floor& f : floors_) {
        add(f.id);
        add(f.name);
    }
    return out;
}

// --- the record sink ----------------------------------------------------------

StreamSink::StreamSink(Host& host, const SinkOptions& options)
    : host_(host), options_(options), handle_(new yr_stream{this})
{
}

StreamSink::~StreamSink()
{
    delete handle_;
}

yr_status StreamSink::push(const yr_record* record)
{
    {
        // Under the mutex, so a waiter in wait_pushes cannot miss it.
        std::lock_guard lock(mutex_);
        ++pushes_;
        ++in_push_;
    }
    push_cv_.notify_all();
    struct Leave {
        std::atomic<uint32_t>& n;
        ~Leave() { --n; }
    } leave{in_push_};
    if (options_.push_hold.count() > 0)
        std::this_thread::sleep_for(options_.push_hold);
    if (record == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            if (stopped_)
                ++late_;
            return YR_ERR_BUSY;
        }
    }
    std::vector<std::string> problems = radio::validate_record(host_.descriptor(), *record);
    if (!problems.empty()) {
        std::lock_guard lock(mutex_);
        ++invalid_;
        for (std::string& p : problems)
            if (problems_.size() < 10)
                problems_.push_back(std::move(p));
        return YR_ERR_INVALID_ARGUMENT;
    }
    Item item;
    item.record = radios::record_from_c(*record, host_.header().schemas[record->schema_index]);
    std::vector<uint32_t> context_events;
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            if (stopped_)
                ++late_;
            return YR_ERR_BUSY;
        }
        if (queue_.size() >= options_.capacity) {
            ++dropped_;
            return YR_OK;
        }
        queue_.push_back(std::move(item));
        context_events = host_.context().on_record();
    }
    cv_.notify_one();
    if (!context_events.empty())
        host_.notify(context_events);
    return YR_OK;
}

yr_status StreamSink::push_spectrum(const yr_spectrum* spectrum)
{
    {
        std::lock_guard lock(mutex_);
        ++pushes_;
        ++in_push_;
    }
    push_cv_.notify_all();
    struct Leave {
        std::atomic<uint32_t>& n;
        ~Leave() { --n; }
    } leave{in_push_};
    if (options_.push_hold.count() > 0)
        std::this_thread::sleep_for(options_.push_hold);
    if (spectrum == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            if (stopped_)
                ++late_;
            return YR_ERR_BUSY;
        }
    }
    std::vector<std::string> problems = radio::validate_spectrum(host_.descriptor(), *spectrum);
    if (!problems.empty()) {
        std::lock_guard lock(mutex_);
        ++invalid_;
        for (std::string& p : problems)
            if (problems_.size() < 10)
                problems_.push_back(std::move(p));
        return YR_ERR_INVALID_ARGUMENT;
    }
    Item item;
    item.is_record = false;
    item.is_spectrum = true;
    item.spectrum = spectrum_from_c(*spectrum, monotonic_now());
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            if (stopped_)
                ++late_;
            return YR_ERR_BUSY;
        }
        if (queue_.size() >= options_.capacity) {
            ++dropped_;
            return YR_OK;
        }
        queue_.push_back(std::move(item));
    }
    cv_.notify_one();
    return YR_OK;
}

void StreamSink::event(uint32_t event, int64_t value)
{
    Item item;
    item.is_record = false;
    item.event = {event, value, monotonic_now()};
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            if (stopped_)
                ++late_;
            return;
        }
        queue_.push_back(std::move(item));
    }
    cv_.notify_one();
}

bool StreamSink::active() const
{
    std::lock_guard lock(mutex_);
    return active_;
}

void StreamSink::set_active(bool active)
{
    std::lock_guard lock(mutex_);
    active_ = active;
    if (!active)
        stopped_ = true;
}

std::deque<StreamSink::Item> StreamSink::take(std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock lock(mutex_);
    cv_.wait_until(lock, deadline, [&] { return !queue_.empty(); });
    std::deque<Item> out;
    out.swap(queue_);
    return out;
}

void StreamSink::counters(StreamResult& r) const
{
    std::lock_guard lock(mutex_);
    r.invalid = invalid_;
    r.dropped = dropped_;
    r.late = late_;
    r.problems = problems_;
}

bool StreamSink::wait_pushes(uint64_t count, std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock lock(mutex_);
    return push_cv_.wait_until(lock, deadline, [&] { return pushes_.load() >= count; });
}

bool StreamSink::wait_push_in_progress(std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock lock(mutex_);
    return push_cv_.wait_until(lock, deadline, [&] { return in_push_.load() > 0; });
}

namespace {

yr_status api_push(yr_stream* stream, const yr_record* record)
{
    if (stream == nullptr || stream->sink == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    try {
        return stream->sink->push(record);
    } catch (...) {
        return YR_ERR_NO_MEMORY;
    }
}

yr_status api_push_spectrum(yr_stream* stream, const yr_spectrum* spectrum)
{
    if (stream == nullptr || stream->sink == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    try {
        return stream->sink->push_spectrum(spectrum);
    } catch (...) {
        return YR_ERR_NO_MEMORY;
    }
}

void api_event(yr_stream* stream, uint32_t event, int64_t value)
{
    if (stream == nullptr || stream->sink == nullptr)
        return;
    try {
        stream->sink->event(event, value);
    } catch (...) {
    }
}

} // namespace

// --- Host ---------------------------------------------------------------------

std::unique_ptr<Host> Host::load(const std::filesystem::path& path, const HostConfig& config,
                                 std::string& error)
{
    std::unique_ptr<Host> host(new Host());
    host->config_ = config;
    std::string why;
    host->library_ = DynamicLibrary::open(path, why);
    if (!host->library_) {
        error = "cannot load " + path.string() + ": " + why;
        return nullptr;
    }
    void* sym = host->library_->symbol(YANTRA_RADIO_ENTRY_SYMBOL, why);
    if (sym == nullptr) {
        error = path.string() + " is not a radio plugin (no " YANTRA_RADIO_ENTRY_SYMBOL "): " + why;
        return nullptr;
    }
    auto entry = reinterpret_cast<yr_entry_fn>(sym);
    const yr_descriptor* d = entry();
    const std::string refusal = radio::refusal_reason(yr_api_version_compiled(), d);
    if (!refusal.empty()) {
        error = "refused: " + refusal + " (RAD-11)";
        return nullptr;
    }
    host->descriptor_ = d;
    host->problems_ = radio::validate_descriptor(d);
    if (d->init == nullptr || d->shutdown == nullptr || d->stream == nullptr ||
        d->device == nullptr || !YR_HAS_FIELD(d->stream, yr_stream_control, last_error) ||
        !YR_HAS_FIELD(d->device, yr_device_control, self_test)) {
        error = "the plugin descriptor lacks required functions";
        for (const std::string& p : host->problems_)
            error += "; " + p;
        return nullptr;
    }
    host->header_ = radios::header_from_descriptor(*d);

    const bool scene = (d->capabilities & YR_CAP_SCENE) != 0;
    if (scene && !host->context_.configure(config.context, error))
        return nullptr;

    host->handle_ = new yr_host{host.get()};
    yr_host_api& api = host->api_;
    api.struct_size = sizeof(yr_host_api);
    api.api_version = yr_api_version_compiled();
    api.reserved = 0;
    api.host = host->handle_;
    api.log = api_log;
    api.monotonic_ns = api_monotonic;
    api.utc_now_ns = api_utc;
    api.push_record = api_push;
    api.stream_event = api_event;
    api.devices_changed = api_devices_changed;
    api.settings_changed = api_settings_changed;
    api.push_spectrum = config.spectrum_sink ? api_push_spectrum : nullptr;
    if (scene) {
        api.get_floor_plan = api_floor_plan;
        api.get_cursor = api_cursor;
        api.get_walk_position = api_walk;
        api.get_scene = api_scene;
    }
    if (scene && d->on_context_event != nullptr)
        host->notifier_ = std::thread([h = host.get()] { h->notifier_main(); });
    if (config.initialise) {
        const yr_status st = d->init(&host->api_, &host->plugin_);
        if (st != YR_OK || host->plugin_ == nullptr) {
            error = std::string("init failed: ") + yr_status_name(st);
            host->plugin_ = nullptr;
            return nullptr;
        }
    }
    return host;
}

Host::~Host()
{
    {
        std::lock_guard lock(notify_mutex_);
        notify_stop_ = true;
    }
    notify_cv_.notify_all();
    if (notifier_.joinable())
        notifier_.join();
    if (plugin_ != nullptr && descriptor_ != nullptr)
        descriptor_->shutdown(plugin_);
    plugin_ = nullptr;
    delete handle_;
}

std::string Host::last_error(yr_device* device) const
{
    char buffer[1024] = {};
    if (descriptor_->stream->last_error(plugin_, device, buffer, sizeof buffer) != YR_OK)
        return {};
    return buffer;
}

void Host::on_log(yr_device* device, uint32_t level, const char* message)
{
    LogLine line{level, device != nullptr, text(message)};
    std::lock_guard lock(log_mutex_);
    if (config_.log != nullptr && level >= config_.print_level) {
        static const char* const names[] = {"debug", "info", "warning", "error"};
        *config_.log << "plugin " << (level <= YR_LOG_ERROR ? names[level] : "?") << ": "
                     << line.message << '\n';
    }
    logs_.push_back(std::move(line));
}

std::vector<LogLine> Host::logs() const
{
    std::lock_guard lock(log_mutex_);
    return logs_;
}

void Host::notify(const std::vector<uint32_t>& events)
{
    if (!notifier_.joinable())
        return;
    {
        std::lock_guard lock(notify_mutex_);
        for (uint32_t e : events)
            if (std::find(notify_queue_.begin(), notify_queue_.end(), e) == notify_queue_.end())
                notify_queue_.push_back(e);
    }
    notify_cv_.notify_all();
}

void Host::wait_notifications_idle()
{
    std::unique_lock lock(notify_mutex_);
    notify_cv_.wait(lock, [&] { return notify_stop_ || (notify_queue_.empty() && !notify_busy_); });
}

std::map<uint32_t, uint64_t> Host::notifications_sent() const
{
    std::lock_guard lock(notify_mutex_);
    return notified_;
}

void Host::notifier_main()
{
    using clock = std::chrono::steady_clock;
    // RAD-18: CURSOR_MOVED at most 30 per second.
    constexpr auto cursor_gap = std::chrono::microseconds(1'000'000 / 30);
    clock::time_point last_cursor{};
    std::unique_lock lock(notify_mutex_);
    for (;;) {
        notify_cv_.wait(lock, [&] { return notify_stop_ || !notify_queue_.empty(); });
        if (notify_stop_)
            break;
        const uint32_t event = notify_queue_.front();
        if (event == YR_CONTEXT_CURSOR_MOVED && last_cursor != clock::time_point{}) {
            const auto due = last_cursor + cursor_gap;
            if (notify_cv_.wait_until(lock, due, [&] { return notify_stop_; }))
                break;
        }
        notify_queue_.pop_front();
        notify_busy_ = true;
        if (event == YR_CONTEXT_CURSOR_MOVED)
            last_cursor = clock::now();
        lock.unlock();
        descriptor_->on_context_event(plugin_, event);
        lock.lock();
        notify_busy_ = false;
        ++notified_[event];
        notify_cv_.notify_all();
    }
    notify_cv_.notify_all();
}

// --- device control helpers -----------------------------------------------------

yr_status enumerate_devices(Host& host, std::vector<radios::DeviceInfo>& out,
                            std::string& diagnostic)
{
    out.clear();
    diagnostic.clear();
    const yr_status st = host.descriptor().stream->enumerate(
        host.plugin(),
        [](void* user, const yr_device_info* info) {
            if (info != nullptr)
                static_cast<std::vector<radios::DeviceInfo>*>(user)->push_back(
                    radios::device_from_c(*info));
        },
        &out);
    if (st != YR_OK)
        diagnostic = host.last_error(nullptr);
    return st;
}

yr_status list_settings(Host& host, yr_device* device, bool sensors, std::vector<SettingDef>& out)
{
    out.clear();
    const yr_device_control& dc = *host.descriptor().device;
    auto fn = sensors ? dc.list_sensors : dc.list_settings;
    if (fn == nullptr)
        return YR_ERR_UNSUPPORTED;
    struct Ctx {
        std::vector<SettingDef>* out;
        bool sensors;
    } ctx{&out, sensors};
    return fn(
        device,
        [](void* user, const yr_setting_def* s) {
            auto* c = static_cast<Ctx*>(user);
            if (s == nullptr)
                return;
            SettingDef d;
            d.problems = radio::validate_setting_def(*s, c->sensors);
            if (YR_HAS_FIELD(s, yr_setting_def, reserved)) {
                d.key = text(s->key);
                d.label = text(s->label);
                d.unit = text(s->unit);
                d.help = text(s->help);
                d.group = text(s->group);
                d.unavailable_reason = text(s->unavailable_reason);
                d.warning = text(s->warning);
                d.type = s->type;
                d.flags = s->flags;
                d.minimum = s->minimum;
                d.maximum = s->maximum;
                d.step = s->step;
                for (uint32_t k = 0; k < s->item_count && s->items != nullptr; ++k)
                    d.items.push_back({s->items[k].value, text(s->items[k].name),
                                       text(s->items[k].label)});
                d.default_value = value_from_c(s->default_value);
                d.sort_order = s->sort_order;
            }
            c->out->push_back(std::move(d));
        },
        &ctx);
}

yr_status list_presets(Host& host, yr_device* device, std::vector<PresetDef>& out)
{
    out.clear();
    const yr_device_control& dc = *host.descriptor().device;
    if (dc.list_presets == nullptr)
        return YR_ERR_UNSUPPORTED;
    return dc.list_presets(
        device,
        [](void* user, const yr_preset* p) {
            if (p != nullptr)
                static_cast<std::vector<PresetDef>*>(user)->push_back(
                    {text(p->id), text(p->label), text(p->help)});
        },
        &out);
}

yr_status run_self_test(Host& host, yr_device* device, std::vector<SelfTestItem>& out)
{
    out.clear();
    const yr_device_control& dc = *host.descriptor().device;
    if (dc.self_test == nullptr)
        return YR_ERR_UNSUPPORTED;
    return dc.self_test(
        device,
        [](void* user, const yr_selftest_item* it) {
            if (it != nullptr)
                static_cast<std::vector<SelfTestItem>*>(user)->push_back(
                    {it->result, text(it->name), text(it->label), text(it->detail),
                     text(it->remedy)});
        },
        &out);
}

yr_status read_value(Host& host, yr_device* device, const std::string& key, bool sensor,
                     Value& out)
{
    const yr_device_control& dc = *host.descriptor().device;
    auto fn = sensor ? dc.read_sensor : dc.get_setting;
    if (fn == nullptr)
        return YR_ERR_UNSUPPORTED;
    char buffer[YR_MAX_STRING_VALUE_BYTES + 1] = {};
    yr_value v = yr_value_none();
    const yr_status st = fn(device, key.c_str(), &v, buffer, sizeof buffer);
    out = st == YR_OK ? value_from_c(v) : Value{};
    return st;
}

yr_status write_value(Host& host, yr_device* device, const std::string& key, const Value& value)
{
    const yr_value v = value_to_c(value);
    return host.descriptor().device->set_setting(device, key.c_str(), &v);
}

yr_status run_action(Host& host, yr_device* device, const std::string& key, std::string& result)
{
    const yr_device_control& dc = *host.descriptor().device;
    if (dc.run_action == nullptr)
        return YR_ERR_UNSUPPORTED;
    char buffer[1024] = {};
    const yr_status st = dc.run_action(device, key.c_str(), buffer, sizeof buffer);
    result = buffer;
    return st;
}

std::string type_text(uint32_t type)
{
    if (type == YR_TYPE_ACTION)
        return "action";
    const std::string_view n = radios::type_name(type);
    return n.empty() ? "none" : std::string(n);
}

std::string test_result_text(uint32_t result)
{
    switch (result) {
    case YR_TEST_PASS: return "pass";
    case YR_TEST_WARN: return "warn";
    case YR_TEST_FAIL: return "fail";
    case YR_TEST_SKIPPED: return "skipped";
    default: return "?";
    }
}

bool parse_value(const SettingDef& def, const std::string& t, Value& out, std::string& error)
{
    out = Value{};
    out.type = def.type;
    char* end = nullptr;
    errno = 0;
    switch (def.type) {
    case YR_TYPE_BOOL:
        if (t == "true" || t == "1" || t == "on") {
            out.i64 = 1;
        } else if (t == "false" || t == "0" || t == "off") {
            out.i64 = 0;
        } else {
            error = def.key + " needs true or false";
            return false;
        }
        return true;
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
        if (def.type == YR_TYPE_ENUM)
            for (const radios::EnumItem& it : def.items)
                if (it.name == t) {
                    out.i64 = it.value;
                    return true;
                }
        out.i64 = std::strtoll(t.c_str(), &end, 10);
        if (t.empty() || *end != '\0' || errno != 0) {
            error = def.key + (def.type == YR_TYPE_ENUM ? " needs an item name or value"
                                                        : " needs an integer");
            return false;
        }
        return true;
    case YR_TYPE_UINT:
        out.u64 = std::strtoull(t.c_str(), &end, 10);
        if (t.empty() || t[0] == '-' || *end != '\0' || errno != 0) {
            error = def.key + " needs an unsigned integer";
            return false;
        }
        return true;
    case YR_TYPE_DOUBLE:
        out.f64 = std::strtod(t.c_str(), &end);
        if (t.empty() || *end != '\0' || !std::isfinite(out.f64)) {
            error = def.key + " needs a number";
            return false;
        }
        return true;
    case YR_TYPE_STRING:
        out.str = t;
        return true;
    case YR_TYPE_ACTION:
        error = def.key + " is an action; run it with --action";
        return false;
    default:
        error = def.key + " has an unknown type";
        return false;
    }
}

std::string format_value(const Value& v, const std::vector<radios::EnumItem>& items)
{
    switch (v.type) {
    case YR_TYPE_BOOL:
        return v.i64 != 0 ? "true" : "false";
    case YR_TYPE_INT:
        return std::to_string(v.i64);
    case YR_TYPE_ENUM:
        for (const radios::EnumItem& it : items)
            if (it.value == v.i64)
                return it.name + " (" + std::to_string(v.i64) + ")";
        return std::to_string(v.i64);
    case YR_TYPE_UINT:
        return std::to_string(v.u64);
    case YR_TYPE_DOUBLE: {
        char b[64];
        std::snprintf(b, sizeof b, "%.10g", v.f64);
        return b;
    }
    case YR_TYPE_STRING:
        return "\"" + v.str + "\"";
    default:
        return "-";
    }
}

// --- spectra (API 1.2, RAD-140) ------------------------------------------------

Spectrum spectrum_from_c(const yr_spectrum& s, int64_t received_ns)
{
    Spectrum out;
    out.flags = s.flags;
    out.monotonic_ns = s.time.monotonic_ns;
    out.utc_ns = s.time.utc_ns;
    out.utc_source = s.time.utc_source;
    out.received_ns = received_ns;
    out.start_hz = s.start_hz;
    out.stop_hz = s.stop_hz;
    out.bin_width_hz = s.bin_width_hz;
    out.power_db.assign(s.power_db, s.power_db + s.bin_count);
    return out;
}

std::string spectrum_summary(const Spectrum& s, bool dbm)
{
    const char* unit = dbm ? " dBm" : " dBFS";
    char buf[256];
    if (s.power_db.empty())
        return "spectrum: no bins";
    const auto peak = std::max_element(s.power_db.begin(), s.power_db.end());
    std::vector<double> sorted = s.power_db;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2),
                     sorted.end());
    const double peak_hz =
        s.start_hz + (static_cast<double>(peak - s.power_db.begin()) + 0.5) * s.bin_width_hz;
    std::snprintf(buf, sizeof buf,
                  "spectrum %.4f-%.4f MHz, %zu bins of %.3f kHz, peak %.1f%s at %.4f MHz, "
                  "median %.1f%s%s",
                  s.start_hz / 1e6, s.stop_hz / 1e6, s.power_db.size(), s.bin_width_hz / 1e3,
                  *peak, unit, peak_hz / 1e6, sorted[sorted.size() / 2], unit,
                  (s.flags & YR_RECORD_SIMULATED) != 0 ? ", simulated" : "");
    return buf;
}

std::string spectrum_csv(const Spectrum& s)
{
    std::string line = std::to_string(s.utc_ns);
    char buf[64];
    std::snprintf(buf, sizeof buf, ",%.3f,%.6f,%zu,%u", s.start_hz, s.bin_width_hz,
                  s.power_db.size(), s.flags);
    line += buf;
    for (double p : s.power_db) {
        std::snprintf(buf, sizeof buf, ",%.2f", p);
        line += buf;
    }
    return line;
}

// --- streaming ------------------------------------------------------------------

StreamResult run_stream(Host& host, yr_device* device, const yr_plan& plan,
                        const StreamLimits& limits,
                        const std::function<void(const radios::Record&)>& on_record,
                        const std::function<void(const radios::Event&)>& on_event,
                        const SinkOptions& sink_options,
                        const std::function<void(const Spectrum&)>& on_spectrum)
{
    using clock = std::chrono::steady_clock;
    StreamResult result;
    const yr_stream_control& sc = *host.descriptor().stream;
    StreamSink sink(host, sink_options);

    sink.set_active(true);
    result.start_status = sc.start_stream(device, &plan, sink.handle());
    if (result.start_status != YR_OK) {
        sink.set_active(false);
        result.start_error = host.last_error(device);
        sink.counters(result);
        return result;
    }
    const auto start = clock::now();
    const auto deadline =
        limits.max_seconds > 0.0
            ? start + std::chrono::duration_cast<clock::duration>(
                          std::chrono::duration<double>(limits.max_seconds))
            : clock::time_point::max();
    bool final_event = false;

    auto handle_items = [&](std::deque<StreamSink::Item>& items) {
        for (StreamSink::Item& item : items) {
            if (item.is_spectrum) {
                ++result.spectra;
                if (on_spectrum)
                    on_spectrum(item.spectrum);
            } else if (item.is_record) {
                if (limits.max_records != 0 && result.records >= limits.max_records) {
                    ++result.records_beyond_limit;
                    continue;
                }
                ++result.records;
                if (on_record)
                    on_record(item.record);
            } else {
                ++result.events;
                if (on_event)
                    on_event(item.event);
                const uint32_t e = item.event.event;
                if (e == YR_EVENT_END_OF_STREAM || e == YR_EVENT_DEVICE_LOST ||
                    e == YR_EVENT_ERROR) {
                    final_event = true;
                    if (result.stop_reason.empty())
                        result.stop_reason = std::string(radios::event_name(e));
                }
            }
        }
    };

    for (;;) {
        // Wake at least every 100 ms to notice an interrupt.
        const auto wake = std::min(deadline, clock::now() + std::chrono::milliseconds(100));
        std::deque<StreamSink::Item> items = sink.take(wake);
        handle_items(items);
        if (limits.max_records != 0 && result.records >= limits.max_records) {
            if (result.stop_reason.empty())
                result.stop_reason = "records";
            break;
        }
        if (final_event)
            break;
        if (clock::now() >= deadline) {
            result.stop_reason = "duration";
            break;
        }
        if (limits.interrupt != nullptr && limits.interrupt->load()) {
            result.stop_reason = "interrupted";
            break;
        }
    }
    result.stop_status = sc.stop_stream(device);
    sink.set_active(false);
    std::deque<StreamSink::Item> rest = sink.take(clock::now());
    handle_items(rest);
    result.stats.struct_size = sizeof(yr_stream_stats);
    result.stats_ok = sc.stream_stats(device, &result.stats) == YR_OK;
    sink.counters(result);
    return result;
}

} // namespace yantra::radio_host
