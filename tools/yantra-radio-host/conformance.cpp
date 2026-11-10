// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio plugin conformance kit (RAD-91), see conformance.hpp. Each
// check is a member function of Kit that records problems; a check that
// cannot run (no device, capability not offered) is skipped with the
// reason. Waits are on observable conditions (records, events, pushes)
// with generous deadlines; the only fixed windows are the short grace
// periods after stop_stream in which a late push would be seen (they can
// only miss a bug, never report a false one).
#include "conformance.hpp"

#include <yantra/radio.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace yantra::radio_host {

namespace {

using clock = std::chrono::steady_clock;
using radios::Record;
using radios::Event;
namespace radio = yantra::radio;

constexpr auto kGrace = std::chrono::milliseconds(150);

double seconds_since(clock::time_point t)
{
    return std::chrono::duration<double>(clock::now() - t).count();
}

std::string status_text(yr_status st)
{
    return yr_status_name(st);
}

bool is_number_type(uint32_t t)
{
    return t == YR_TYPE_INT || t == YR_TYPE_UINT || t == YR_TYPE_DOUBLE;
}

double number_of(const Value& v)
{
    switch (v.type) {
    case YR_TYPE_INT:
    case YR_TYPE_BOOL:
    case YR_TYPE_ENUM: return static_cast<double>(v.i64);
    case YR_TYPE_UINT: return static_cast<double>(v.u64);
    case YR_TYPE_DOUBLE: return v.f64;
    default: return NAN;
    }
}

Value number_value(uint32_t type, double x)
{
    Value v;
    v.type = type;
    if (type == YR_TYPE_DOUBLE)
        v.f64 = x;
    else if (type == YR_TYPE_UINT)
        v.u64 = x <= 0.0 ? 0u : static_cast<uint64_t>(std::llround(x));
    else
        v.i64 = static_cast<int64_t>(std::llround(x));
    return v;
}

bool same_value(const Value& a, const Value& b)
{
    if (a.type != b.type)
        return false;
    if (a.type == YR_TYPE_DOUBLE)
        return std::fabs(a.f64 - b.f64) <= 1e-9 * std::max(1.0, std::fabs(a.f64));
    return a == b;
}

bool single_line(const std::string& s)
{
    return s.find('\n') == std::string::npos && s.find('\r') == std::string::npos;
}

// Threads of this process (Linux, Windows), -1 where unknown.
int thread_count()
{
#if defined(_WIN32)
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return -1;
    const DWORD self = GetCurrentProcessId();
    int n = 0;
    THREADENTRY32 t{};
    t.dwSize = sizeof t;
    for (BOOL ok = Thread32First(snap, &t); ok; ok = Thread32Next(snap, &t))
        if (t.th32OwnerProcessID == self)
            ++n;
    CloseHandle(snap);
    return n;
#elif defined(__linux__)
    std::error_code ec;
    int n = 0;
    for (auto it = std::filesystem::directory_iterator("/proc/self/task", ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        ++n;
    return ec ? -1 : n;
#else
    return -1;
#endif
}

const char* const kBuiltinScene = R"({
  "format": "yantra-radio-scene",
  "version": 1,
  "floor": { "id": "kit-floor-q7", "name": "Kit floor name Q7", "scale_set": true,
             "width_m": 20.0, "height_m": 10.0 },
  "wall_classes": [ { "id": "brick", "label": "Brick", "attenuation_db": 8.0 } ],
  "walls": [ { "x1_m": 10.0, "y1_m": 0.0, "x2_m": 10.0, "y2_m": 10.0,
               "class": "brick", "attenuation_db": 8.0 } ],
  "cells": [ { "id": "kc1", "label": "Kit cell 1", "mcc": 901, "mnc": 1, "la": 7,
               "colour_code": 1, "carrier_hz": 392012500 } ],
  "antennas": [ { "id": "ka1", "cell": "kc1", "x_m": 3.0, "y_m": 5.0, "height_m": 2.5,
                  "eirp_dbm": 10.0, "pattern": { "type": "omni" } } ]
}
)";
const char* const kKitPlanName = "Kit plan name Z9";
const char* const kKitExampleId = "kit-example-z9";

// Records and events taken from a sink.
struct Collected {
    std::vector<Record> records;
    std::vector<Event> events;
    bool has_event(uint32_t e) const
    {
        return std::any_of(events.begin(), events.end(),
                           [e](const Event& x) { return x.event == e; });
    }
};

// One check in progress.
struct Check {
    CheckResult& r;
    void fail(const std::string& problem)
    {
        if (r.problems.size() < 40)
            r.problems.push_back(problem);
    }
    void expect(bool ok, const std::string& problem)
    {
        if (!ok)
            fail(problem);
    }
    void skip(const std::string& why)
    {
        r.result = "skip";
        r.detail = why;
    }
    void note(const std::string& text)
    {
        if (!r.detail.empty())
            r.detail += "; ";
        r.detail += text;
    }
};

class Kit {
public:
    Kit(std::filesystem::path path, const ConformanceOptions& options)
        : path_(std::move(path)), options_(options)
    {
        report_.plugin_path = path_.string();
    }

    ConformanceReport run();

private:
    // --- infrastructure ---
    void check(const std::string& id, const std::string& title,
               std::vector<std::string> requirements, const std::function<void(Check&)>& fn);
    void skip_all(const std::vector<std::pair<std::string, std::string>>& ids,
                  const std::string& why);
    const yr_stream_control& sc() const { return *host_->descriptor().stream; }
    const yr_device_control& dc() const { return *host_->descriptor().device; }
    uint32_t caps() const { return host_->descriptor().capabilities; }
    int64_t mono() const { return host_->host_api().monotonic_ns(host_->host_api().host); }
    int64_t utc() const { return host_->host_api().utc_now_ns(host_->host_api().host); }
    std::string error_text(yr_device* d) const { return host_->last_error(d); }
    bool open_device(Check& c);
    void close_device();
    yr_plan default_plan() const;
    std::vector<SettingDef> settings(Check& c, const char* when);
    std::map<std::string, std::pair<SettingDef, Value>> snapshot();
    void restore(Check& c);
    std::unique_ptr<StreamSink> start(Check& c, const yr_plan& plan, const SinkOptions& o,
                                      const char* what);
    bool drain(StreamSink& sink, Collected& out, double seconds,
               const std::function<bool(const Collected&)>& until);
    void stop(Check& c, StreamSink& sink, const char* what);
    void collect_names(const std::string& where, const std::string& name);

    // --- checks ---
    void check_load(Check& c);
    void check_version(Check& c);
    void check_enumerate(Check& c);
    void check_open_close(Check& c);
    void check_settings(Check& c);
    void check_persistence(Check& c);
    void check_actions(Check& c);
    void check_presets(Check& c);
    void check_sensors(Check& c);
    void check_self_test(Check& c);
    void check_plans(Check& c);
    void check_records(Check& c);
    void check_plan_change(Check& c);
    void check_state_machine(Check& c);
    void check_stop_during_push(Check& c);
    void check_sink_full(Check& c);
    void check_hotplug(Check& c);
    void check_context(Check& c);
    void check_completeness(Check& c);
    void check_scope(Check& c);
    void check_cleanup(Check& c);

    std::filesystem::path path_;
    ConformanceOptions options_;
    ConformanceReport report_;
    std::unique_ptr<Host> host_;
    std::string load_error_;
    int threads_before_ = -1;
    std::string device_key_;
    std::string no_device_;
    yr_device* device_ = nullptr;
    std::vector<SettingDef> original_settings_;
    std::map<std::string, Value> original_values_;
    std::vector<uint64_t> seen_carriers_;
    std::vector<std::pair<std::string, std::string>> names_; // where, name (RAD-17)
    std::set<std::string> selftest_names_;
    std::set<std::string> preset_ids_;
    std::vector<SettingDef> sensors_;
};

// --- infrastructure ------------------------------------------------------------

void Kit::check(const std::string& id, const std::string& title,
                std::vector<std::string> requirements, const std::function<void(Check&)>& fn)
{
    CheckResult r;
    r.id = id;
    r.title = title;
    r.requirements = std::move(requirements);
    const auto t0 = clock::now();
    Check c{r};
    try {
        fn(c);
    } catch (const std::exception& e) {
        c.fail(std::string("the kit stopped this check: ") + e.what());
    } catch (...) {
        c.fail("the kit stopped this check on an unknown exception");
    }
    if (r.result != "skip")
        r.result = r.problems.empty() ? "pass" : "fail";
    r.seconds = seconds_since(t0);
    if (options_.progress != nullptr) {
        *options_.progress << r.result << " " << r.id << " - " << r.title << "\n";
        for (const std::string& p : r.problems)
            *options_.progress << "    " << p << "\n";
        if (r.result == "skip")
            *options_.progress << "    " << r.detail << "\n";
    }
    report_.checks.push_back(std::move(r));
}

void Kit::skip_all(const std::vector<std::pair<std::string, std::string>>& ids,
                   const std::string& why)
{
    for (const auto& [id, title] : ids)
        check(id, title, {}, [&](Check& c) { c.skip(why); });
}

bool Kit::open_device(Check& c)
{
    if (device_ != nullptr)
        return true;
    const auto t0 = clock::now();
    const yr_status st = sc().open(host_->plugin(), device_key_.c_str(), &device_);
    if (st != YR_OK || device_ == nullptr) {
        c.fail("open(" + device_key_ + ") returned " + status_text(st) + ": " +
               error_text(nullptr));
        device_ = nullptr;
        return false;
    }
    c.expect(seconds_since(t0) <= 10.0, "open took longer than 10 s (RAD-15)");
    return true;
}

void Kit::close_device()
{
    if (device_ != nullptr)
        sc().close(device_);
    device_ = nullptr;
}

yr_plan Kit::default_plan() const
{
    yr_plan p{};
    p.struct_size = sizeof p;
    p.type = YR_PLAN_SERVING;
    p.flags = YR_PLAN_MEASURE_NEIGHBOURS;
    return p;
}

std::vector<SettingDef> Kit::settings(Check& c, const char* when)
{
    std::vector<SettingDef> out;
    const yr_status st = list_settings(*host_, device_, false, out);
    if (st != YR_OK)
        c.fail(std::string("list_settings ") + when + " returned " + status_text(st));
    return out;
}

std::map<std::string, std::pair<SettingDef, Value>> Kit::snapshot()
{
    std::map<std::string, std::pair<SettingDef, Value>> out;
    std::vector<SettingDef> defs;
    if (list_settings(*host_, device_, false, defs) != YR_OK)
        return out;
    for (SettingDef& d : defs) {
        Value v;
        if (d.type != YR_TYPE_ACTION)
            read_value(*host_, device_, d.key, false, v);
        const std::string key = d.key;
        out[key] = {std::move(d), v};
    }
    return out;
}

// Writes the values the device had when the kit started, twice so that a
// value that depends on another one (gain after AGC) lands too.
void Kit::restore(Check& c)
{
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::string> failed;
        for (const SettingDef& d : original_settings_) {
            const auto it = original_values_.find(d.key);
            if (it == original_values_.end() || d.type == YR_TYPE_ACTION ||
                (d.flags & (YR_SETTING_READ_ONLY | YR_SETTING_UNAVAILABLE)) != 0)
                continue;
            if (write_value(*host_, device_, d.key, it->second) != YR_OK)
                failed.push_back(d.key);
        }
        if (failed.empty())
            return;
        if (pass == 1)
            for (const std::string& k : failed)
                c.fail("could not write the original value of " + k + " back");
    }
}

std::unique_ptr<StreamSink> Kit::start(Check& c, const yr_plan& plan, const SinkOptions& o,
                                       const char* what)
{
    auto sink = std::make_unique<StreamSink>(*host_, o);
    sink->set_active(true);
    const auto t0 = clock::now();
    const yr_status st = sc().start_stream(device_, &plan, sink->handle());
    if (st != YR_OK) {
        c.fail(std::string(what) + ": start_stream returned " + status_text(st) + ": " +
               error_text(device_));
        sink->set_active(false);
        return nullptr;
    }
    c.expect(seconds_since(t0) <= 1.0,
             std::string(what) + ": start_stream took longer than 1 s (RAD-15)");
    const uint32_t state = sc().state(device_);
    c.expect(state == YR_DEVICE_STREAMING || state == YR_DEVICE_OPEN /* already ended */,
             std::string(what) + ": state after start_stream is " + std::to_string(state) +
                 ", expected STREAMING (RAD-34)");
    return sink;
}

bool Kit::drain(StreamSink& sink, Collected& out, double seconds,
                const std::function<bool(const Collected&)>& until)
{
    const auto deadline =
        clock::now() + std::chrono::duration_cast<clock::duration>(
                           std::chrono::duration<double>(seconds));
    for (;;) {
        if (until && until(out))
            return true;
        if (clock::now() >= deadline)
            return false;
        const auto wake = std::min(deadline, clock::now() + std::chrono::milliseconds(50));
        for (StreamSink::Item& item : sink.take(wake)) {
            if (item.is_record)
                out.records.push_back(std::move(item.record));
            else
                out.events.push_back(item.event);
        }
    }
}

void Kit::stop(Check& c, StreamSink& sink, const char* what)
{
    const auto t0 = clock::now();
    const yr_status st = sc().stop_stream(device_);
    const uint64_t pushes_at_stop = sink.pushes();
    const uint32_t in_push = sink.pushes_in_progress();
    sink.set_active(false);
    c.expect(st == YR_OK, std::string(what) + ": stop_stream returned " + status_text(st));
    c.expect(seconds_since(t0) <= 1.0,
             std::string(what) + ": stop_stream took longer than 1 s (RAD-15)");
    c.expect(in_push == 0, std::string(what) +
                               ": stop_stream returned while a push_record was still running "
                               "(RAD-33)");
    c.expect(sc().state(device_) == YR_DEVICE_OPEN,
             std::string(what) + ": state after stop_stream is not OPEN (RAD-34)");
    std::this_thread::sleep_for(kGrace);
    StreamResult counters;
    sink.counters(counters);
    c.expect(sink.pushes() == pushes_at_stop && counters.late == 0,
             std::string(what) + ": push_record or stream_event after stop_stream returned (RAD-33)");
}

void Kit::collect_names(const std::string& where, const std::string& name)
{
    names_.emplace_back(where, name);
}

// --- checks ----------------------------------------------------------------------

void Kit::check_load(Check& c)
{
    HostConfig config;
    config.print_level = YR_LOG_ERROR + 1; // the kit prints nothing itself
    host_ = Host::load(path_, config, load_error_);
    if (!host_) {
        c.fail("not loaded: " + load_error_);
        return;
    }
    const yr_descriptor& d = host_->descriptor();
    report_.plugin_id = d.id != nullptr ? d.id : "";
    report_.plugin_version = d.version != nullptr ? d.version : "";
    report_.api_version = radio::version_text(d.api_version);
    for (const std::string& p : host_->descriptor_problems())
        c.fail(p);
    c.note("plugin " + report_.plugin_id + " " + report_.plugin_version + ", radio API " +
           report_.api_version + ", " + std::to_string(d.schema_count) + " schemas");
}

void Kit::check_version(Check& c)
{
    const yr_descriptor& d = host_->descriptor();
    const yr_api_version host = yr_api_version_compiled();
    c.expect(radio::refusal_reason(host, &d).empty(), "this host would refuse the plugin");
    // A host of another major version, and one with an older minor
    // version, must refuse it (RAD-11); the same rule Yantra applies.
    yr_api_version other_major{d.api_version.major + 1u, d.api_version.minor};
    c.expect(!radio::refusal_reason(other_major, &d).empty(),
             "a host of radio API " + radio::version_text(other_major) + " would load it");
    if (d.api_version.minor > 0) {
        yr_api_version older{d.api_version.major, d.api_version.minor - 1u};
        c.expect(!radio::refusal_reason(older, &d).empty(),
                 "a host of the older radio API " + radio::version_text(older) + " would load it");
    }
    yr_descriptor newer = d;
    newer.api_version.minor = host.minor + 1u;
    c.expect(!radio::refusal_reason(host, &newer).empty(),
             "the refusal rule does not refuse a newer minor version");
    c.note("radio API " + report_.api_version + " accepted by host " +
           radio::version_text(host));
}

void Kit::check_enumerate(Check& c)
{
    struct Seen {
        std::vector<radios::DeviceInfo> devices;
        std::vector<std::string> problems;
    } seen;
    const yr_status st = sc().enumerate(
        host_->plugin(),
        [](void* user, const yr_device_info* info) {
            auto* s = static_cast<Seen*>(user);
            if (info == nullptr || !YR_HAS_FIELD(info, yr_device_info, flags_reason)) {
                s->problems.push_back("enumerate reported a NULL or too small yr_device_info");
                return;
            }
            auto text = [](const char* t) { return t != nullptr ? std::string(t) : std::string(); };
            const std::string key = text(info->key);
            if (key.empty())
                s->problems.push_back("a device has no key (RAD-30)");
            if (text(info->label).empty())
                s->problems.push_back("device " + key + " has no label (RAD-30)");
            if (text(info->driver).empty())
                s->problems.push_back("device " + key + " has no driver name (RAD-30)");
            if (info->serial == nullptr || info->module_version == nullptr ||
                info->hardware_info == nullptr || info->flags_reason == nullptr)
                s->problems.push_back("device " + key +
                                      ": serial, module_version, hardware_info and "
                                      "flags_reason must not be NULL (use \"\")");
            if ((info->flags & ~(YR_DEVICE_IN_USE | YR_DEVICE_NO_PERMISSION |
                                 YR_DEVICE_SIMULATED)) != 0)
                s->problems.push_back("device " + key + " has unknown flag bits");
            if ((info->flags & (YR_DEVICE_IN_USE | YR_DEVICE_NO_PERMISSION)) != 0 &&
                text(info->flags_reason).empty())
                s->problems.push_back("device " + key +
                                      " is IN_USE or NO_PERMISSION without flags_reason (RAD-30)");
            radios::DeviceInfo d;
            d.key = key;
            d.label = text(info->label);
            d.driver = text(info->driver);
            d.flags = info->flags;
            s->devices.push_back(d);
        },
        &seen);
    for (const std::string& p : seen.problems)
        c.fail(p);
    if (st == YR_ERR_NO_DEVICE) {
        const std::string diag = error_text(nullptr);
        c.expect(!diag.empty(), "enumerate returned NO_DEVICE without a diagnostic in "
                                "last_error(plugin, NULL) (RAD-30, RAD-52)");
        c.expect(seen.devices.empty(), "enumerate returned NO_DEVICE but listed devices");
        no_device_ = diag.empty() ? "no device" : "no device: " + diag;
        c.note(no_device_);
        return;
    }
    if (st != YR_OK) {
        c.fail("enumerate returned " + status_text(st) + ": " + error_text(nullptr));
        no_device_ = "enumerate failed";
        return;
    }
    if (seen.devices.empty()) {
        c.fail("enumerate returned OK without a device; with none it returns NO_DEVICE (RAD-30)");
        no_device_ = "no device";
        return;
    }
    std::set<std::string> keys;
    for (const auto& d : seen.devices)
        c.expect(keys.insert(d.key).second, "device key " + d.key + " is listed twice");
    for (const auto& d : seen.devices) {
        const bool usable = (d.flags & (YR_DEVICE_IN_USE | YR_DEVICE_NO_PERMISSION)) == 0;
        if ((options_.device.empty() && usable && device_key_.empty()) || d.key == options_.device)
            device_key_ = d.key;
    }
    if (device_key_.empty()) {
        if (!options_.device.empty())
            c.fail("device " + options_.device + " is not listed");
        no_device_ = "no usable device (all in use or without permission)";
    }
    c.note(std::to_string(seen.devices.size()) + " device(s); testing " +
           (device_key_.empty() ? std::string("none") : device_key_));
    report_.device = device_key_;
}

void Kit::check_open_close(Check& c)
{
    yr_device* none = nullptr;
    const yr_status bad = sc().open(host_->plugin(), "yantra-kit:no-such-device", &none);
    c.expect(bad != YR_OK, "open with an unknown key succeeded (RAD-31)");
    if (bad == YR_OK && none != nullptr)
        sc().close(none);
    else
        c.expect(!error_text(nullptr).empty(),
                 "open with an unknown key left no text in last_error (RAD-16)");
    for (int round = 0; round < 2; ++round) {
        if (!open_device(c))
            return;
        c.expect(sc().state(device_) == YR_DEVICE_OPEN, "state after open is not OPEN (RAD-34)");
        int calls = 0;
        std::string key;
        struct Ctx {
            int* calls;
            std::string* key;
        } ctx{&calls, &key};
        const yr_status st = sc().device_info(
            device_,
            [](void* user, const yr_device_info* info) {
                auto* x = static_cast<Ctx*>(user);
                ++*x->calls;
                if (info != nullptr && info->key != nullptr)
                    *x->key = info->key;
            },
            &ctx);
        c.expect(st == YR_OK && calls == 1,
                 "device_info must report exactly one entry (RAD-31), got " +
                     std::to_string(calls) + " with " + status_text(st));
        c.expect(key == device_key_, "device_info reports key " + key + ", opened " + device_key_);
        yr_stream_stats stats{};
        stats.struct_size = sizeof stats;
        c.expect(sc().stream_stats(device_, &stats) == YR_OK,
                 "stream_stats fails in state OPEN (RAD-24)");
        c.expect(sc().stop_stream(device_) == YR_OK,
                 "stop_stream without a stream is not harmless (RAD-33)");
        if (round == 0)
            close_device();
    }
    c.note("opened, closed and opened " + device_key_ + " again");
}

void Kit::check_settings(Check& c)
{
    original_settings_ = settings(c, "after open");
    std::set<std::string> keys;
    for (const SettingDef& d : original_settings_) {
        for (const std::string& p : d.problems)
            c.fail(p);
        c.expect(keys.insert(d.key).second, "setting " + d.key + " is listed twice");
        collect_names("setting", d.key);
        for (const auto& item : d.items)
            collect_names("setting " + d.key + " item", item.name);
        if (d.type == YR_TYPE_ACTION)
            continue;
        Value v;
        const yr_status st = read_value(*host_, device_, d.key, false, v);
        if (st != YR_OK) {
            c.fail("get_setting(" + d.key + ") returned " + status_text(st) + " (RAD-41)");
            continue;
        }
        c.expect(v.type == d.type, "get_setting(" + d.key + ") returned type " +
                                       type_text(v.type) + ", declared " + type_text(d.type));
        original_values_[d.key] = v;
    }
    Value dummy;
    c.expect(read_value(*host_, device_, "yantra_kit.no_such_setting", false, dummy) ==
                 YR_ERR_NOT_FOUND,
             "get_setting with an unknown key must return NOT_FOUND");
    Value any;
    any.type = YR_TYPE_BOOL;
    c.expect(write_value(*host_, device_, "yantra_kit.no_such_setting", any) == YR_ERR_NOT_FOUND,
             "set_setting with an unknown key must return NOT_FOUND");

    int tested = 0;
    for (const SettingDef& listed : original_settings_) {
        if (listed.type == YR_TYPE_ACTION)
            continue;
        // The current definition: availability may depend on others (RAD-44).
        const auto before = snapshot();
        const auto found = before.find(listed.key);
        if (found == before.end())
            continue;
        const SettingDef& d = found->second.first;
        const Value current = found->second.second;
        const std::string& k = d.key;

        auto set = [&](const Value& v) { return write_value(*host_, device_, k, v); };
        auto refused = [&](const Value& v, const std::string& what) {
            const yr_status st = set(v);
            if (st == YR_OK) {
                c.fail("set_setting(" + k + ") accepted " + what);
            } else {
                c.expect(st == YR_ERR_INVALID_ARGUMENT,
                         "set_setting(" + k + ") with " + what + " returned " + status_text(st) +
                             ", expected INVALID_ARGUMENT (RAD-41)");
                c.expect(!error_text(device_).empty(), "set_setting(" + k + ") refused " + what +
                                                           " without a last_error text (RAD-41)");
            }
        };
        // Writes `v`, reads it back and checks range, raster and rounding.
        auto round_trip = [&](const Value& v, const std::string& what) {
            const uint64_t changed_before = host_->settings_changed_count();
            const auto others_before = snapshot();
            const yr_status st = set(v);
            if (st != YR_OK) {
                c.fail("set_setting(" + k + ") with " + what + " returned " + status_text(st) +
                       ": " + error_text(device_));
                return;
            }
            Value back;
            if (read_value(*host_, device_, k, false, back) != YR_OK || back.type != d.type) {
                c.fail("get_setting(" + k + ") after set_setting fails or changes type");
                return;
            }
            if (is_number_type(d.type)) {
                const double req = number_of(v);
                const double got = number_of(back);
                if (std::isfinite(d.minimum) && got < d.minimum - 1e-9)
                    c.fail(k + " reads back " + format_value(back) + " below its minimum");
                if (std::isfinite(d.maximum) && got > d.maximum + 1e-9)
                    c.fail(k + " reads back " + format_value(back) + " above its maximum");
                if (d.step > 0.0 && std::isfinite(d.minimum)) {
                    const double n = (got - d.minimum) / d.step;
                    if (std::fabs(n - std::round(n)) > 1e-6)
                        c.fail(k + " reads back " + format_value(back) + ", not on its " +
                               radio::number_text(d.step) + " raster (RAD-41)");
                }
                const double tolerance = std::max(d.step, 1e-6 * std::max(1.0, std::fabs(req)));
                if (std::fabs(got - req) > tolerance + 1e-9)
                    c.fail(k + " set to " + format_value(v) + " reads back " + format_value(back) +
                           ", more than one step away (RAD-41)");
            } else if (!same_value(v, back)) {
                c.fail(k + " set to " + format_value(v, d.items) + " reads back " +
                       format_value(back, d.items) + " (RAD-41)");
            }
            // Dependencies: anything else that changed needs settings_changed (RAD-44).
            const auto others_after = snapshot();
            for (const auto& [key, pair] : others_after) {
                if (key == k)
                    continue;
                const auto it = others_before.find(key);
                if (it == others_before.end())
                    continue;
                const SettingDef& a = it->second.first;
                const SettingDef& b = pair.first;
                const bool def_changed = a.flags != b.flags || a.items.size() != b.items.size() ||
                                         a.unavailable_reason != b.unavailable_reason ||
                                         !(a.minimum == b.minimum ||
                                           (std::isnan(a.minimum) && std::isnan(b.minimum))) ||
                                         !(a.maximum == b.maximum ||
                                           (std::isnan(a.maximum) && std::isnan(b.maximum))) ||
                                         a.step != b.step;
                const bool value_changed = !same_value(it->second.second, pair.second);
                if ((def_changed || value_changed) &&
                    host_->settings_changed_count() == changed_before)
                    c.fail("setting " + k + " to " + format_value(v, d.items) + " changed " +
                           key + " without settings_changed (RAD-44)");
            }
        };

        if ((d.flags & (YR_SETTING_READ_ONLY | YR_SETTING_UNAVAILABLE)) != 0) {
            c.expect(set(current) != YR_OK,
                     "set_setting(" + k + ") succeeded on a read-only or unavailable setting");
            continue;
        }
        ++tested;
        round_trip(current, "its own value");
        Value wrong;
        if (d.type == YR_TYPE_STRING) {
            wrong.type = YR_TYPE_DOUBLE;
            wrong.f64 = 1.0;
        } else {
            wrong.type = YR_TYPE_STRING;
            wrong.str = "yantra-kit";
        }
        refused(wrong, "a value of the wrong type");
        switch (d.type) {
        case YR_TYPE_BOOL: {
            Value t = current;
            t.i64 = 1;
            round_trip(t, "true");
            t.i64 = 0;
            round_trip(t, "false");
            break;
        }
        case YR_TYPE_ENUM: {
            int64_t unused = 0;
            for (const auto& item : d.items) {
                Value e = current;
                e.i64 = item.value;
                round_trip(e, "item " + item.name);
                unused = std::max(unused, item.value + 1);
            }
            Value e = current;
            e.i64 = unused;
            refused(e, "a value that is not one of its items");
            break;
        }
        case YR_TYPE_STRING: {
            Value s = current;
            s.str = "yantra-kit";
            round_trip(s, "a text");
            break;
        }
        default: { // numbers
            const double lo = d.minimum;
            const double hi = d.maximum;
            if (std::isfinite(lo))
                round_trip(number_value(d.type, lo), "its minimum");
            if (std::isfinite(hi))
                round_trip(number_value(d.type, hi), "its maximum");
            if (std::isfinite(lo) && std::isfinite(hi) && d.step > 0.0 && hi - lo >= d.step &&
                d.type == YR_TYPE_DOUBLE)
                round_trip(number_value(d.type, lo + 0.4 * d.step), "a value between two steps");
            const double out = std::max(d.step, 1.0);
            if (std::isfinite(hi) && hi < 9e18)
                refused(number_value(d.type, hi + out), "a value above its maximum");
            if (std::isfinite(lo) && (d.type != YR_TYPE_UINT || lo >= out))
                refused(number_value(d.type, lo - out), "a value below its minimum");
            break;
        }
        }
        // Back to where it was, before the next setting is tested.
        if (set(current) != YR_OK)
            c.fail("could not write " + k + " back to " + format_value(current, d.items));
    }
    restore(c);
    c.note(std::to_string(original_settings_.size()) + " settings, " + std::to_string(tested) +
           " writable ones round-tripped");
}

void Kit::check_persistence(Check& c)
{
    // RAD-47: the plugin keeps no settings of its own; after close and open
    // every writable setting is at its default again.
    std::string changed;
    for (const SettingDef& d : original_settings_) {
        if (d.type == YR_TYPE_ACTION || d.type == YR_TYPE_STRING ||
            (d.flags & (YR_SETTING_READ_ONLY | YR_SETTING_UNAVAILABLE)) != 0 ||
            d.default_value.type == YR_TYPE_NONE)
            continue;
        Value other = d.default_value;
        if (d.type == YR_TYPE_BOOL) {
            other.i64 = d.default_value.i64 != 0 ? 0 : 1;
        } else if (d.type == YR_TYPE_ENUM) {
            for (const auto& item : d.items)
                if (item.value != d.default_value.i64)
                    other.i64 = item.value;
        } else if (std::isfinite(d.minimum) && std::isfinite(d.maximum)) {
            const double def = number_of(d.default_value);
            other = number_value(d.type, def != d.maximum ? d.maximum : d.minimum);
        } else {
            continue;
        }
        if (same_value(other, d.default_value) ||
            write_value(*host_, device_, d.key, other) != YR_OK)
            continue;
        changed = d.key;
        break;
    }
    if (changed.empty()) {
        c.skip("no writable setting with a default and a second value");
        return;
    }
    close_device();
    if (!open_device(c))
        return;
    std::vector<SettingDef> defs = settings(c, "after reopen");
    for (const SettingDef& d : defs) {
        if (d.key != changed)
            continue;
        Value v;
        read_value(*host_, device_, d.key, false, v);
        c.expect(same_value(v, d.default_value),
                 d.key + " kept " + format_value(v, d.items) + " after close and open instead of "
                         "its default " + format_value(d.default_value, d.items) +
                         " (RAD-47: Yantra stores and applies the values)");
    }
    restore(c);
    c.note("changed " + changed + ", closed and opened: back at its default");
}

void Kit::check_actions(Check& c)
{
    std::vector<SettingDef> actions;
    for (const SettingDef& d : original_settings_)
        if (d.type == YR_TYPE_ACTION)
            actions.push_back(d);
    if (actions.empty()) {
        c.skip("no ACTION settings");
        return;
    }
    if (dc().run_action == nullptr) {
        c.fail("ACTION settings are listed but run_action is NULL (RAD-42)");
        return;
    }
    for (const SettingDef& a : actions) {
        if ((a.flags & YR_SETTING_UNAVAILABLE) != 0)
            continue;
        std::string result;
        const auto t0 = clock::now();
        const yr_status st = run_action(*host_, device_, a.key, result);
        c.expect(st == YR_OK, "run_action(" + a.key + ") returned " + status_text(st) + ": " +
                                  error_text(device_));
        c.expect(seconds_since(t0) <= 10.0, "run_action(" + a.key + ") took longer than 10 s");
        c.expect(!result.empty() && single_line(result),
                 "run_action(" + a.key + ") must give a one-line result text (RAD-42)");
        Value v;
        c.expect(write_value(*host_, device_, a.key, v) != YR_OK,
                 "set_setting on the action " + a.key + " succeeded");
    }
    std::string result;
    c.expect(run_action(*host_, device_, "yantra_kit.no_such_action", result) != YR_OK,
             "run_action with an unknown key succeeded");
    restore(c);
    c.note(std::to_string(actions.size()) + " action(s) run");
}

void Kit::check_presets(Check& c)
{
    if (dc().list_presets == nullptr) {
        c.skip("no presets (list_presets is NULL)");
        return;
    }
    std::vector<PresetDef> presets;
    const yr_status st = list_presets(*host_, device_, presets);
    if (st != YR_OK) {
        c.fail("list_presets returned " + status_text(st));
        return;
    }
    for (const PresetDef& p : presets) {
        collect_names("preset", p.id);
        c.expect(radio::valid_key(p.id), "preset id " + p.id + " must match [a-z0-9_.]+");
        c.expect(preset_ids_.insert(p.id).second, "preset " + p.id + " is listed twice");
        c.expect(!p.label.empty() && !p.help.empty(),
                 "preset " + p.id + " needs a label and a help text (RAD-43)");
        const uint64_t before = host_->settings_changed_count();
        const yr_status a = dc().apply_preset(device_, p.id.c_str());
        c.expect(a == YR_OK, "apply_preset(" + p.id + ") returned " + status_text(a) + ": " +
                                 error_text(device_));
        c.expect(host_->settings_changed_count() > before,
                 "apply_preset(" + p.id + ") did not call settings_changed (RAD-43)");
        std::vector<SettingDef> after = settings(c, "after a preset");
        for (const SettingDef& d : after)
            for (const std::string& p2 : d.problems)
                c.fail("after preset " + p.id + ": " + p2);
    }
    c.expect(dc().apply_preset(device_, "yantra_kit.no_such_preset") != YR_OK,
             "apply_preset with an unknown id succeeded");
    restore(c);
    if (presets.empty())
        c.note("list_presets lists none");
    else
        c.note(std::to_string(presets.size()) + " preset(s) applied");
}

void Kit::check_sensors(Check& c)
{
    if (dc().list_sensors == nullptr) {
        c.skip("no sensors (list_sensors is NULL)");
        return;
    }
    const yr_status st = list_settings(*host_, device_, true, sensors_);
    if (st != YR_OK) {
        c.fail("list_sensors returned " + status_text(st));
        return;
    }
    std::set<std::string> keys;
    for (const SettingDef& s : sensors_) {
        collect_names("sensor", s.key);
        for (const std::string& p : s.problems)
            c.fail(p);
        c.expect(keys.insert(s.key).second, "sensor " + s.key + " is listed twice");
        Value v;
        const yr_status r = read_value(*host_, device_, s.key, true, v);
        c.expect(r == YR_OK, "read_sensor(" + s.key + ") returned " + status_text(r));
        c.expect(r != YR_OK || v.type == s.type || v.type == YR_TYPE_NONE,
                 "read_sensor(" + s.key + ") returned type " + type_text(v.type) +
                     ", declared " + type_text(s.type));
    }
    Value v;
    c.expect(read_value(*host_, device_, "yantra_kit.no_such_sensor", true, v) != YR_OK,
             "read_sensor with an unknown key succeeded");
    c.note(std::to_string(sensors_.size()) + " sensor(s) read");
}

void Kit::check_self_test(Check& c)
{
    if (dc().self_test == nullptr) {
        c.skip("no self-test (self_test is NULL)");
        return;
    }
    std::vector<SelfTestItem> items;
    const auto t0 = clock::now();
    const yr_status st = run_self_test(*host_, device_, items);
    c.expect(st == YR_OK, "self_test returned " + status_text(st) + ": " + error_text(device_));
    c.expect(seconds_since(t0) <= 10.0, "self_test took longer than 10 s (RAD-15)");
    c.expect(!items.empty(), "self_test reported no item (RAD-46)");
    for (const SelfTestItem& it : items) {
        collect_names("self-test item", it.name);
        selftest_names_.insert(it.name);
        c.expect(radio::valid_key(it.name), "self-test item " + it.name + " needs a [a-z0-9_.]+ name");
        c.expect(it.result <= YR_TEST_SKIPPED, "self-test item " + it.name + " has an unknown result");
        c.expect(!it.label.empty(), "self-test item " + it.name + " has no label");
        c.expect((it.result != YR_TEST_WARN && it.result != YR_TEST_FAIL) || !it.remedy.empty(),
                 "self-test item " + it.name + " warns or fails without a remedy (RAD-46)");
    }
    c.note(std::to_string(items.size()) + " item(s)");
}

void Kit::check_plans(Check& c)
{
    struct Seen {
        int calls = 0;
        yr_plan_ranges ranges{};
        std::vector<yr_freq_range> list;
    } seen;
    const yr_status st = sc().plan_ranges(
        device_,
        [](void* user, const yr_plan_ranges* r) {
            auto* s = static_cast<Seen*>(user);
            ++s->calls;
            if (r != nullptr && YR_HAS_FIELD(r, yr_plan_ranges, max_span_hz)) {
                s->ranges = *r;
                if (r->ranges != nullptr)
                    s->list.assign(r->ranges, r->ranges + r->range_count);
            }
        },
        &seen);
    if (st != YR_OK || seen.calls != 1) {
        c.fail("plan_ranges must report exactly one entry (RAD-32), got " +
               std::to_string(seen.calls) + " with " + status_text(st));
        return;
    }
    const uint32_t known = (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS) |
                           (1u << YR_PLAN_SCAN);
    const uint32_t types = seen.ranges.plan_types;
    c.expect(types != 0 && (types & ~known) == 0, "plan_types has no or unknown bits");
    c.expect(((types & (1u << YR_PLAN_SCAN)) != 0) == ((caps() & YR_CAP_SCAN) != 0),
             "the SCAN plan type is offered exactly with the SCAN capability (RAD-23)");
    c.expect(!seen.list.empty(), "plan_ranges lists no frequency range");
    for (const yr_freq_range& r : seen.list)
        c.expect(r.first_hz > 0 && r.first_hz <= r.last_hz, "a frequency range is empty or reversed");
    c.expect(seen.ranges.max_carriers >= 1, "max_carriers must be at least 1");
    if (!seen.list.empty() && seen_carriers_.empty())
        seen_carriers_.push_back(seen.list.front().first_hz / 2 + seen.list.front().last_hz / 2);

    // Unsupported and unknown plan types are refused; the state stays OPEN.
    for (uint32_t t : {YR_PLAN_SERVING, YR_PLAN_CARRIERS, YR_PLAN_SCAN, 99u}) {
        if (t != 99u && (types & (1u << t)) != 0)
            continue;
        yr_plan p = default_plan();
        p.type = t;
        StreamSink sink(*host_);
        sink.set_active(true);
        const yr_status s = sc().start_stream(device_, &p, sink.handle());
        if (s == YR_OK) {
            c.fail("start_stream accepted the unsupported plan type " + std::to_string(t));
            sc().stop_stream(device_);
        }
        sink.set_active(false);
        c.expect(sc().state(device_) == YR_DEVICE_OPEN,
                 "state is not OPEN after a refused start_stream");
    }
    yr_plan serving = default_plan();
    if ((types & (1u << YR_PLAN_SERVING)) == 0)
        serving.type = (types & (1u << YR_PLAN_CARRIERS)) != 0 ? YR_PLAN_CARRIERS : YR_PLAN_SCAN;
    const uint64_t hz = seen_carriers_.empty() ? 0 : seen_carriers_.front();
    serving.carriers_hz = &hz;
    serving.carrier_count = serving.type == YR_PLAN_CARRIERS ? 1 : 0;
    if (serving.type == YR_PLAN_SCAN && !seen.list.empty()) {
        serving.scan_first_hz = seen.list.front().first_hz;
        serving.scan_last_hz = seen.list.front().first_hz;
    }
    const yr_status sp = sc().set_plan(device_, &serving);
    c.expect(sp == YR_OK, "set_plan in state OPEN returned " + status_text(sp) + " (RAD-32)");
    c.note("plan types " + std::to_string(types) + ", " + std::to_string(seen.list.size()) +
           " range(s), up to " + std::to_string(seen.ranges.max_carriers) + " carriers");
}

void Kit::check_records(Check& c)
{
    const yr_plan plan = default_plan();
    const int64_t mono0 = mono();
    const int64_t utc0 = utc();
    std::vector<Record> records;
    std::vector<Event> events;
    StreamLimits limits;
    limits.max_seconds = std::max(1.5, options_.stream_seconds);
    const StreamResult r = run_stream(
        *host_, device_, plan, limits, [&](const Record& rec) { records.push_back(rec); },
        [&](const Event& e) { events.push_back(e); });
    const int64_t mono1 = mono();
    if (r.start_status != YR_OK) {
        c.fail("start_stream returned " + status_text(r.start_status) + ": " + r.start_error);
        return;
    }
    c.expect(r.stop_status == YR_OK, "stop_stream returned " + status_text(r.stop_status));
    c.expect(r.invalid == 0, std::to_string(r.invalid) + " record(s) did not match their schema "
                                                          "(RAD-21, RAD-22)");
    for (const std::string& p : r.problems)
        c.fail(p);
    c.expect(r.late == 0, "push after stop_stream returned (RAD-33)");
    c.expect(r.stop_reason != "error" && r.stop_reason != "device_lost",
             "the stream stopped with " + r.stop_reason + ": " + error_text(device_));
    c.expect(!records.empty(), "no record in " + radio::number_text(limits.max_seconds) + " s");
    c.expect(r.stats_ok, "stream_stats failed after the stream (RAD-24)");
    c.expect(!r.stats_ok || r.stats.records_delivered >= r.records,
             "stream_stats counts fewer records delivered than the host accepted (RAD-24)");
    for (const Event& e : events)
        c.expect(e.event >= YR_EVENT_SYNC_FOUND && e.event <= YR_EVENT_ERROR,
                 "unknown stream event " + std::to_string(e.event) + " (RAD-25)");

    // Time (RAD-27) and the once-per-second rule per carrier (RAD-21).
    const auto& schemas = host_->header().schemas;
    std::map<uint64_t, std::vector<int64_t>> times; // MEASUREMENT records per carrier
    int measurement = 0;
    for (const Record& rec : records) {
        const int64_t t = rec.time.monotonic_ns;
        c.expect(t >= mono0 && t <= mono1,
                 "a record's monotonic_ns lies outside the stream (RAD-27)");
        if (rec.time.utc_source == YR_TIME_SOURCE_SYSTEM) {
            const double off = std::fabs(static_cast<double>((rec.time.utc_ns - utc0) - (t - mono0)));
            c.expect(off < 5e9, "a record's system utc_ns is more than 5 s off the host clock");
        }
        const radios::SchemaDef& s = schemas[rec.schema_index];
        if (s.kind != YR_SCHEMA_MEASUREMENT || rec.values.empty() ||
            rec.values[YR_CORE_FREQUENCY_HZ].type != YR_TYPE_UINT)
            continue;
        ++measurement;
        times[rec.values[YR_CORE_FREQUENCY_HZ].u64].push_back(t);
    }
    c.expect(records.empty() || measurement > 0, "no MEASUREMENT record (RAD-21)");
    seen_carriers_.clear();
    for (auto& [hz, ts] : times) {
        seen_carriers_.push_back(hz);
        for (std::size_t i = 1; i < ts.size(); ++i) {
            if (ts[i] < ts[i - 1]) {
                c.fail("records of carrier " + std::to_string(hz) + " Hz are not in time order "
                                                                    "(RAD-27)");
                break;
            }
            // Once per second, with 0.25 s for scheduling (RAD-21).
            if (ts[i] - ts[i - 1] > 1'250'000'000) {
                c.fail("carrier " + std::to_string(hz) + " Hz went " +
                       radio::number_text(static_cast<double>(ts[i] - ts[i - 1]) / 1e9) +
                       " s without a record (at least one per second, RAD-21)");
                break;
            }
        }
    }
    c.note(std::to_string(r.records) + " records (" + std::to_string(measurement) +
           " measurements) on " + std::to_string(times.size()) + " carrier(s), " +
           std::to_string(r.events) + " events in " + radio::number_text(limits.max_seconds) +
           " s");
}

void Kit::check_plan_change(Check& c)
{
    struct Seen {
        yr_plan_ranges r{};
    } seen;
    sc().plan_ranges(
        device_,
        [](void* user, const yr_plan_ranges* r) {
            if (r != nullptr)
                static_cast<Seen*>(user)->r = *r;
        },
        &seen);
    const uint32_t types = seen.r.plan_types;
    const bool live = (caps() & YR_CAP_RETUNE_LIVE) != 0;
    std::vector<uint64_t> carriers = seen_carriers_;
    if (carriers.size() > std::max<uint32_t>(1u, seen.r.max_carriers))
        carriers.resize(std::max<uint32_t>(1u, seen.r.max_carriers));
    if (carriers.size() > 2)
        carriers.resize(2);
    if ((types & (1u << YR_PLAN_CARRIERS)) != 0 && !carriers.empty()) {
        yr_plan p = default_plan();
        p.type = YR_PLAN_CARRIERS;
        p.flags = 0;
        p.carriers_hz = carriers.data();
        p.carrier_count = static_cast<uint32_t>(carriers.size());
        auto sink = start(c, p, {}, "carrier plan");
        if (!sink)
            return;
        Collected got;
        std::set<uint64_t> wanted(carriers.begin(), carriers.end());
        auto measured = [&](const Collected& x) {
            std::set<uint64_t> seen_hz;
            for (const Record& r : x.records)
                if (host_->header().schemas[r.schema_index].kind == YR_SCHEMA_MEASUREMENT)
                    seen_hz.insert(r.values[YR_CORE_FREQUENCY_HZ].u64);
            return std::includes(seen_hz.begin(), seen_hz.end(), wanted.begin(), wanted.end());
        };
        c.expect(drain(*sink, got, 5.0, measured),
                 "the carrier plan did not give a record of every listed carrier in 5 s");
        for (const Record& r : got.records)
            if (host_->header().schemas[r.schema_index].kind == YR_SCHEMA_MEASUREMENT &&
                wanted.count(r.values[YR_CORE_FREQUENCY_HZ].u64) == 0) {
                c.fail("the carrier plan measured " +
                       std::to_string(r.values[YR_CORE_FREQUENCY_HZ].u64) +
                       " Hz, which is not in the list (RAD-32)");
                break;
            }
        // A plan change while streaming: RETUNE_LIVE or BUSY (RAD-32).
        yr_plan other = default_plan();
        if ((types & (1u << YR_PLAN_SERVING)) == 0) {
            other = p;
            other.carrier_count = 1;
        }
        const yr_status st = sc().set_plan(device_, &other);
        if (live) {
            c.expect(st == YR_OK, "set_plan while streaming with RETUNE_LIVE returned " +
                                      status_text(st));
            c.expect(drain(*sink, got, 5.0,
                           [](const Collected& x) { return x.has_event(YR_EVENT_PLAN_APPLIED); }),
                     "no PLAN_APPLIED event within 5 s of set_plan (RAD-25, RAD-32)");
        } else {
            c.expect(st == YR_ERR_BUSY, "set_plan while streaming without RETUNE_LIVE returned " +
                                            status_text(st) + ", expected BUSY (RAD-32)");
        }
        stop(c, *sink, "carrier plan");
        c.note("carrier plan with " + std::to_string(carriers.size()) + " carrier(s)");
    } else {
        c.note("no carrier plan (not offered or no carrier known)");
    }
    if ((caps() & YR_CAP_SCAN) != 0) {
        yr_plan p = default_plan();
        p.type = YR_PLAN_SCAN;
        p.flags = 0;
        const uint64_t first = carriers.empty() ? 0 : carriers.front();
        p.scan_first_hz = first;
        p.scan_last_hz = first + 2 * 25'000;
        p.scan_step_hz = 25'000;
        auto sink = start(c, p, {}, "scan plan");
        if (!sink)
            return;
        Collected got;
        c.expect(drain(*sink, got, 20.0,
                       [](const Collected& x) { return x.has_event(YR_EVENT_SCAN_DONE); }),
                 "no SCAN_DONE event within 20 s for a 3-channel scan (RAD-23)");
        for (const Record& r : got.records)
            c.expect(host_->header().schemas[r.schema_index].kind != YR_SCHEMA_MEASUREMENT ||
                         r.values[YR_CORE_ROLE].i64 != YR_ROLE_SERVING,
                     "a scan delivered a serving record");
        stop(c, *sink, "scan plan");
        c.note("scan of 3 channels: " + std::to_string(got.records.size()) + " records");
    }
}

void Kit::check_state_machine(Check& c)
{
    auto sink = start(c, default_plan(), {}, "stream");
    if (!sink)
        return;
    Collected got;
    drain(*sink, got, 5.0, [](const Collected& x) { return !x.records.empty(); });
    StreamSink second(*host_);
    second.set_active(true);
    const yr_plan plan = default_plan();
    const yr_status again = sc().start_stream(device_, &plan, second.handle());
    c.expect(again == YR_ERR_BUSY,
             "a second start_stream returned " + status_text(again) + ", expected BUSY (RAD-34)");
    if (again == YR_OK)
        c.fail("the device now streams twice");
    second.set_active(false);
    if (dc().self_test != nullptr) {
        std::vector<SelfTestItem> items;
        const yr_status st = run_self_test(*host_, device_, items);
        c.expect(st == YR_ERR_BUSY, "self_test while streaming returned " + status_text(st) +
                                        ", expected BUSY (RAD-46)");
    }
    for (const SettingDef& d : original_settings_) {
        if ((d.flags & YR_SETTING_RESTART) == 0 || d.type == YR_TYPE_ACTION ||
            (d.flags & (YR_SETTING_READ_ONLY | YR_SETTING_UNAVAILABLE)) != 0)
            continue;
        const auto it = original_values_.find(d.key);
        if (it == original_values_.end())
            continue;
        const yr_status st = write_value(*host_, device_, d.key, it->second);
        c.expect(st == YR_ERR_BUSY, "set_setting(" + d.key + ") of a RESTART setting while "
                                                             "streaming returned " +
                                        status_text(st) + ", expected BUSY (RAD-40)");
    }
    stop(c, *sink, "stream");
    c.expect(sc().stop_stream(device_) == YR_OK, "a second stop_stream is not harmless (RAD-33)");

    // close() while streaming stops the stream first (RAD-31).
    auto sink2 = start(c, default_plan(), {}, "stream before close");
    if (!sink2)
        return;
    sink2->wait_pushes(1, clock::now() + std::chrono::seconds(5));
    const auto t0 = clock::now();
    close_device();
    const uint64_t at_close = sink2->pushes();
    const uint32_t in_push = sink2->pushes_in_progress();
    sink2->set_active(false);
    c.expect(seconds_since(t0) <= 1.0, "close while streaming took longer than 1 s (RAD-15)");
    c.expect(in_push == 0, "close returned while a push_record was still running (RAD-31)");
    std::this_thread::sleep_for(kGrace);
    c.expect(sink2->pushes() == at_close, "push_record after close returned (RAD-31, RAD-33)");
    open_device(c);
    c.note("BUSY in wrong states, double stop, close while streaming");
}

void Kit::check_stop_during_push(Check& c)
{
    SinkOptions o;
    o.push_hold = std::chrono::milliseconds(100); // a slow host, for this check only
    auto sink = start(c, default_plan(), o, "stream");
    if (!sink)
        return;
    if (!sink->wait_push_in_progress(clock::now() + std::chrono::seconds(10))) {
        c.fail("no push_record within 10 s");
        stop(c, *sink, "stream");
        return;
    }
    // stop_stream now runs while the plugin's thread is inside push_record:
    // it must wait for that push and allow no further one (RAD-33).
    stop(c, *sink, "stop during a push");
    c.note("stop_stream issued while a push_record was in progress");
}

void Kit::check_sink_full(Check& c)
{
    SinkOptions o;
    o.capacity = 1; // the host keeps one item and drops the rest (RAD-24)
    auto sink = start(c, default_plan(), o, "stream");
    if (!sink)
        return;
    // Nothing is taken from the sink: the plugin must carry on pushing.
    const bool kept_on = sink->wait_pushes(6, clock::now() + std::chrono::seconds(10));
    StreamResult counters;
    sink->counters(counters);
    stop(c, *sink, "full sink");
    c.expect(kept_on, "the plugin stopped pushing when the host dropped records (only " +
                          std::to_string(sink->pushes()) +
                          " pushes in 10 s; the plugin never waits for Yantra, RAD-24)");
    c.expect(!kept_on || counters.dropped > 0, "the full sink dropped nothing");
    c.note(std::to_string(sink->pushes()) + " pushes with a full sink, " +
           std::to_string(counters.dropped) + " dropped by the host");
}

void Kit::check_hotplug(Check& c)
{
    if ((caps() & YR_CAP_HOTPLUG) == 0) {
        c.skip("no HOTPLUG capability");
        return;
    }
    if (!options_.unplug || !options_.replug) {
        c.skip("HOTPLUG: no unplug/replug command given (--unplug, --replug)");
        return;
    }
    auto sink = start(c, default_plan(), {}, "stream");
    if (!sink)
        return;
    Collected got;
    drain(*sink, got, 5.0, [](const Collected& x) { return !x.records.empty(); });
    const uint64_t changes = host_->devices_changed_count();
    std::string error;
    if (!options_.unplug(error)) {
        c.fail("the unplug command failed: " + error);
        stop(c, *sink, "stream");
        return;
    }
    c.expect(drain(*sink, got, 10.0,
                   [](const Collected& x) { return x.has_event(YR_EVENT_DEVICE_LOST); }),
             "no DEVICE_LOST event within 10 s of the unplug (RAD-35)");
    c.expect(sc().state(device_) == YR_DEVICE_LOST, "state after the unplug is not LOST (RAD-34)");
    const yr_plan plan = default_plan();
    StreamSink other(*host_);
    other.set_active(true);
    const yr_status st = sc().start_stream(device_, &plan, other.handle());
    other.set_active(false);
    c.expect(st == YR_ERR_DEVICE_LOST,
             "start_stream on a LOST device returned " + status_text(st) + " (RAD-34)");
    for (const SettingDef& d : original_settings_) {
        const auto it = original_values_.find(d.key);
        if (it == original_values_.end() || (d.flags & YR_SETTING_READ_ONLY) != 0)
            continue;
        const yr_status s = write_value(*host_, device_, d.key, it->second);
        c.expect(s == YR_ERR_DEVICE_LOST,
                 "set_setting on a LOST device returned " + status_text(s) + " (RAD-34)");
        break;
    }
    if (!options_.replug(error)) {
        c.fail("the replug command failed: " + error);
    } else {
        c.expect(drain(*sink, got, 10.0,
                       [](const Collected& x) { return x.has_event(YR_EVENT_DEVICE_BACK); }),
                 "no DEVICE_BACK event within 10 s of the replug (RAD-35)");
    }
    const auto deadline = clock::now() + std::chrono::seconds(10);
    while (host_->devices_changed_count() < changes + 2 && clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    c.expect(host_->devices_changed_count() >= changes + 2,
             "devices_changed was not called for the unplug and the replug (RAD-35)");
    sink->set_active(false);
    close_device(); // Yantra closes, reopens and restarts (RAD-35)
    if (open_device(c)) {
        auto again = start(c, default_plan(), {}, "stream after the replug");
        if (again) {
            Collected more;
            c.expect(drain(*again, more, 5.0, [](const Collected& x) { return !x.records.empty(); }),
                     "no record within 5 s after the replug");
            stop(c, *again, "stream after the replug");
        }
    }
    c.note("unplugged and replugged during a stream");
}

void Kit::check_context(Check& c)
{
    if ((caps() & YR_CAP_SCENE) == 0) {
        const yr_host_api& api = host_->host_api();
        c.expect(api.get_floor_plan == nullptr && api.get_scene == nullptr,
                 "the host offers context callbacks to a plugin without SCENE (RAD-14)");
        c.skip("no SCENE capability");
        return;
    }
    EmulatedContext& ctx = host_->context();
    std::string error;
    auto records_of = [&](double seconds, std::size_t n, Collected& got,
                          const std::function<void(StreamSink&, Collected&)>& script) {
        auto sink = start(c, default_plan(), {}, "context stream");
        if (!sink)
            return;
        if (script)
            script(*sink, got);
        drain(*sink, got, seconds, [n](const Collected& x) { return x.records.size() >= n; });
        stop(c, *sink, "context stream");
    };

    // A: no plan shown; the plugin keeps working (NO_PLAN, NO_POSITION).
    ContextOptions none;
    none.plan = false;
    c.expect(ctx.configure(none, error), "the kit cannot configure the context: " + error);
    Collected a;
    records_of(5.0, 3, a, nullptr);
    c.expect(!a.records.empty(), "no record without a floor plan (NO_PLAN must not stop it)");

    // B: a plan with its scene and scripted mouse and walk paths (RAD-18).
    ContextOptions shown;
    shown.plan = true;
    shown.plan_name = kKitPlanName;
    shown.example_id = kKitExampleId;
    shown.scene_json = options_.scene_json.empty() ? std::vector<std::string>{kBuiltinScene}
                                                   : options_.scene_json;
    shown.cursor = {{2.0, 2.0}, {6.0, 4.0}, {14.0, 6.0}};
    shown.walk = {{4.0, 3.0}, {12.0, 7.0}};
    shown.path_step_records = 3;
    if (!ctx.configure(shown, error)) {
        c.fail("the kit cannot configure the context: " + error);
        return;
    }
    host_->notify({YR_CONTEXT_PLAN_CHANGED, YR_CONTEXT_SCENE_CHANGED});
    Collected b;
    records_of(8.0, 24, b, [&](StreamSink& sink, Collected& got) {
        drain(sink, got, 4.0, [](const Collected& x) { return x.records.size() >= 6; });
        host_->notify(ctx.set_walk(std::nullopt)); // walk ends: the cursor is used
        drain(sink, got, 4.0, [](const Collected& x) { return x.records.size() >= 12; });
        host_->notify(ctx.set_plan_shown(false)); // plan hidden ...
        drain(sink, got, 4.0, [](const Collected& x) { return x.records.size() >= 15; });
        host_->notify(ctx.set_plan_shown(true)); // ... and shown again
    });
    host_->wait_notifications_idle();
    c.expect(!b.records.empty(), "no record with a floor plan shown");

    // sim_position: on the shown floor, at a scripted point (RAD-19).
    std::vector<PlanPoint> points = shown.cursor;
    points.insert(points.end(), shown.walk.begin(), shown.walk.end());
    std::string floor_id;
    {
        yr_floor_plan_info info{};
        info.struct_size = sizeof info;
        if (ctx.get_floor_plan(&info) == YR_OK)
            floor_id = info.floor_id;
    }
    int with_position = 0;
    const auto& schemas = host_->header().schemas;
    for (const Record& r : b.records) {
        if ((r.present & YR_GROUP_SIM_POSITION) == 0)
            continue;
        const radios::SchemaDef& s = schemas[r.schema_index];
        const int fi = s.field_index(YR_FIELD_SIM_FLOOR_ID);
        const int xi = s.field_index(YR_FIELD_SIM_X_M);
        const int yi = s.field_index(YR_FIELD_SIM_Y_M);
        if (fi < 0 || xi < 0 || yi < 0)
            continue;
        ++with_position;
        const Value& f = r.values[static_cast<std::size_t>(fi)];
        const Value& x = r.values[static_cast<std::size_t>(xi)];
        const Value& y = r.values[static_cast<std::size_t>(yi)];
        if (f.type == YR_TYPE_STRING && f.str != floor_id)
            c.fail("sim_floor_id " + f.str + " is not the shown floor " + floor_id);
        if (x.type == YR_TYPE_DOUBLE && y.type == YR_TYPE_DOUBLE &&
            std::none_of(points.begin(), points.end(), [&](const PlanPoint& p) {
                return std::fabs(p.x_m - x.f64) < 1e-6 && std::fabs(p.y_m - y.f64) < 1e-6;
            }))
            c.fail("sim_position (" + radio::number_text(x.f64) + ", " +
                   radio::number_text(y.f64) + ") is not a position the host gave");
    }
    if ((caps() & YR_CAP_SIMULATED) != 0)
        c.expect(with_position > 0, "a SIMULATED SCENE plugin delivered no sim_position (RAD-19)");

    // The plugin asked for the context, and every case was offered.
    const auto calls = ctx.call_counts();
    uint64_t asked = 0;
    for (const auto& [name, n] : calls)
        asked += n;
    c.expect(asked > 0, "a SCENE plugin never called a context callback (RAD-18)");
    std::string cases;
    for (const auto& [name, n] : calls)
        cases += (cases.empty() ? "" : ", ") + name + " " + std::to_string(n);

    // Leak check (RAD-19): no context text in records, none in logs above debug.
    const std::vector<std::string> texts = ctx.context_texts();
    auto leaks = [&](const std::string& s, bool floor_id_allowed) -> std::string {
        for (const std::string& t : texts) {
            if (t.size() < 3 || (floor_id_allowed && t == floor_id))
                continue;
            if (s.find(t) != std::string::npos)
                return t;
        }
        return "";
    };
    for (const Collected* got : {&a, &b})
        for (const Record& r : got->records) {
            const radios::SchemaDef& s = schemas[r.schema_index];
            for (std::size_t i = 0; i < r.values.size() && i < s.fields.size(); ++i) {
                if (r.values[i].type != YR_TYPE_STRING)
                    continue;
                const bool sim_floor = s.fields[i].name == YR_FIELD_SIM_FLOOR_ID;
                const std::string t = leaks(r.values[i].str, sim_floor);
                if (!t.empty()) {
                    c.fail("record field " + s.fields[i].name + " contains context text \"" + t +
                           "\" (RAD-19)");
                    break;
                }
            }
        }
    for (const LogLine& l : host_->logs()) {
        if (l.level == YR_LOG_DEBUG)
            continue;
        const std::string t = leaks(l.message, false);
        if (!t.empty())
            c.fail("log line above debug contains context text \"" + t + "\" (RAD-19): " +
                   l.message);
    }
    const auto sent = host_->notifications_sent();
    std::string notes;
    for (const auto& [e, n] : sent)
        notes += (notes.empty() ? "" : ", ") + std::to_string(e) + ":" + std::to_string(n);
    c.note(std::to_string(b.records.size()) + " records with a plan, " +
           std::to_string(with_position) + " with sim_position; callbacks: " + cases +
           "; notifications (event:count): " + notes);
    ContextOptions off;
    ctx.configure(off, error);
}

void Kit::check_completeness(Check& c)
{
    // RAD-49: Yantra builds every window from the data alone, so every
    // field, setting, sensor and preset needs its texts (RAD-48 for the
    // translations).
    const yr_descriptor& d = host_->descriptor();
    std::set<std::string> text_keys;
    for (uint32_t i = 0; i < d.schema_count; ++i) {
        const yr_schema* s = d.schemas[i];
        if (s == nullptr)
            continue;
        collect_names("schema", s->id != nullptr ? s->id : "");
        c.expect(radio::has_text(s->label), std::string("schema ") + s->id + " has no label");
        text_keys.insert(std::string("schema.") + s->id + ".label");
        for (uint32_t k = 0; k < s->field_count; ++k) {
            const yr_field_def* f = s->fields[k];
            if (f == nullptr)
                continue;
            const std::string fk = std::string("field.") + s->id + "." + f->name;
            collect_names(std::string("schema ") + s->id + " field", f->name);
            c.expect(radio::has_text(f->label) && radio::has_text(f->help),
                     std::string("field ") + s->id + "." + f->name +
                         " needs a label and a help text (RAD-49, Yantra DOC-07)");
            text_keys.insert(fk + ".label");
            text_keys.insert(fk + ".help");
            for (uint32_t e = 0; e < f->enum_count; ++e) {
                collect_names(std::string("field ") + f->name + " item", f->enum_items[e].name);
                text_keys.insert(fk + ".item." + f->enum_items[e].name);
            }
        }
    }
    for (const SettingDef& s : original_settings_) {
        c.expect(!s.group.empty(), "setting " + s.key + " has no group (RAD-40)");
        for (const char* part : {"label", "help", "warning", "unavailable"})
            text_keys.insert("setting." + s.key + "." + part);
        for (const auto& item : s.items)
            text_keys.insert("setting." + s.key + ".item." + item.name);
    }
    for (const SettingDef& s : sensors_) {
        c.expect(!s.group.empty(), "sensor " + s.key + " has no group (RAD-45)");
        text_keys.insert("sensor." + s.key + ".label");
        text_keys.insert("sensor." + s.key + ".help");
        for (const auto& item : s.items)
            text_keys.insert("sensor." + s.key + ".item." + item.name);
    }
    for (const std::string& p : preset_ids_) {
        text_keys.insert("preset." + p + ".label");
        text_keys.insert("preset." + p + ".help");
    }
    for (const std::string& n : selftest_names_)
        text_keys.insert("selftest." + n + ".label");
    std::set<std::pair<std::string, std::string>> seen;
    for (uint32_t i = 0; i < d.translation_count; ++i) {
        const yr_translation& t = d.translations[i];
        const std::string key = t.key != nullptr ? t.key : "";
        const std::string lang = t.lang != nullptr ? t.lang : "";
        c.expect(seen.insert({key, lang}).second, "translation " + key + " (" + lang +
                                                      ") is listed twice (RAD-48)");
        const bool device_text = key.rfind("setting.", 0) == 0 || key.rfind("sensor.", 0) == 0 ||
                                 key.rfind("preset.", 0) == 0 || key.rfind("selftest.", 0) == 0;
        if (text_keys.count(key) == 0 && (!device_text || device_ != nullptr))
            c.fail("translation key " + key + " names nothing the plugin has (RAD-48)");
    }
    c.note(std::to_string(text_keys.size()) + " text keys, " +
           std::to_string(d.translation_count) + " translations");
}

void Kit::check_scope(Check& c)
{
    for (const auto& [where, name] : names_)
        c.expect(!radio::is_out_of_scope_name(name),
                 where + " \"" + name + "\" is outside the scope boundary (RAD-17)");
    c.note(std::to_string(names_.size()) + " names checked");
}

void Kit::check_cleanup(Check& c)
{
    close_device();
    host_.reset(); // shutdown, then the library stays mapped (RTLD_NODELETE)
    if (threads_before_ < 0) {
        c.skip("thread count not available on this system");
        return;
    }
    const auto deadline = clock::now() + std::chrono::seconds(5);
    int now = thread_count();
    while (now > threads_before_ && clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        now = thread_count();
    }
    c.expect(now <= threads_before_, std::to_string(now - threads_before_) +
                                         " plugin thread(s) still run after close and "
                                         "shutdown (RAD-15, RAD-31)");
    c.note("no thread left after close and shutdown; leaks: see the asan-ubsan build");
}

ConformanceReport Kit::run()
{
    // A first thread makes runtimes start their helper threads (the
    // ThreadSanitizer background thread), so they are in the baseline.
    std::thread([] {}).join();
    threads_before_ = thread_count();
    check("load", "the plugin loads and its descriptor is valid", {"RAD-10", "RAD-12", "RAD-13"},
          [&](Check& c) { check_load(c); });
    if (!host_)
        return report_;
    check("version", "radio API version and refusal rule", {"RAD-11"},
          [&](Check& c) { check_version(c); });
    check("enumerate", "devices or a no-device diagnostic", {"RAD-16", "RAD-30"},
          [&](Check& c) { check_enumerate(c); });
    const std::vector<std::pair<std::string, std::string>> device_checks = {
        {"open_close", "open, device_info, close"},
        {"settings", "every setting round-tripped"},
        {"persistence", "no settings kept by the plugin"},
        {"actions", "actions"},
        {"presets", "presets"},
        {"sensors", "sensors"},
        {"self_test", "self-test"},
        {"plans", "plan ranges and plan types"},
        {"records", "records, time rules, stream counters"},
        {"plan_change", "plan changes"},
        {"state_machine", "state machine and BUSY"},
        {"stop_during_push", "stop while a push is in progress"},
        {"sink_full", "the plugin carries on when the host drops"},
        {"hotplug", "unplug and replug"},
        {"context", "context callbacks and leak check"}};
    bool opened = false;
    if (!device_key_.empty()) {
        check("open_close", "open, device_info, close", {"RAD-16", "RAD-31", "RAD-34"},
              [&](Check& c) {
                  check_open_close(c);
                  opened = device_ != nullptr;
              });
    }
    if (opened) {
        check("settings", "every setting round-tripped", {"RAD-40", "RAD-41", "RAD-44"},
              [&](Check& c) { check_settings(c); });
        check("persistence", "no settings kept by the plugin", {"RAD-47"},
              [&](Check& c) { check_persistence(c); });
        check("actions", "actions", {"RAD-42"}, [&](Check& c) { check_actions(c); });
        check("presets", "presets", {"RAD-43", "RAD-44"}, [&](Check& c) { check_presets(c); });
        check("sensors", "sensors", {"RAD-45"}, [&](Check& c) { check_sensors(c); });
        check("self_test", "self-test", {"RAD-46"}, [&](Check& c) { check_self_test(c); });
        check("plans", "plan ranges and plan types", {"RAD-32"}, [&](Check& c) { check_plans(c); });
        check("records", "records, time rules, stream counters",
              {"RAD-20", "RAD-21", "RAD-22", "RAD-24", "RAD-25", "RAD-27"},
              [&](Check& c) { check_records(c); });
        check("plan_change", "plan changes", {"RAD-23", "RAD-32"},
              [&](Check& c) { check_plan_change(c); });
        check("state_machine", "state machine and BUSY", {"RAD-31", "RAD-33", "RAD-34", "RAD-15"},
              [&](Check& c) { check_state_machine(c); });
        check("stop_during_push", "stop while a push is in progress", {"RAD-33"},
              [&](Check& c) { check_stop_during_push(c); });
        check("sink_full", "the plugin carries on when the host drops", {"RAD-24"},
              [&](Check& c) { check_sink_full(c); });
        check("hotplug", "unplug and replug", {"RAD-34", "RAD-35"},
              [&](Check& c) { check_hotplug(c); });
        check("context", "context callbacks and leak check", {"RAD-14", "RAD-18", "RAD-19", "RAD-36"},
              [&](Check& c) { check_context(c); });
    } else {
        std::vector<std::pair<std::string, std::string>> rest;
        for (const auto& item : device_checks)
            if (device_key_.empty() || item.first != "open_close")
                rest.push_back(item);
        skip_all(rest, device_key_.empty() ? no_device_ : "the device did not open");
    }
    check("completeness", "texts for every window", {"RAD-48", "RAD-49"},
          [&](Check& c) { check_completeness(c); });
    check("scope", "no name outside the scope boundary", {"RAD-17", "RAD-26"},
          [&](Check& c) { check_scope(c); });
    check("cleanup", "no thread left after close and shutdown", {"RAD-15", "RAD-31"},
          [&](Check& c) { check_cleanup(c); });
    return report_;
}

} // namespace

std::size_t ConformanceReport::count(const std::string& result) const
{
    return static_cast<std::size_t>(std::count_if(
        checks.begin(), checks.end(), [&](const CheckResult& c) { return c.result == result; }));
}

ConformanceReport run_conformance(const std::filesystem::path& plugin,
                                  const ConformanceOptions& options)
{
    try {
        Kit kit(plugin, options);
        return kit.run();
    } catch (const std::exception& e) {
        ConformanceReport r;
        r.plugin_path = plugin.string();
        CheckResult c;
        c.id = "kit";
        c.title = "the conformance kit itself";
        c.result = "fail";
        c.problems.push_back(std::string("the kit stopped: ") + e.what());
        r.checks.push_back(c);
        return r;
    }
}

std::string conformance_json(const ConformanceReport& report)
{
    nlohmann::ordered_json j;
    j["format"] = "yantra-radio-conformance";
    j["version"] = 1;
    j["host"] = std::string("yantra-radio-host ") + host_version();
    j["plugin"] = {{"path", report.plugin_path},
                   {"id", report.plugin_id},
                   {"version", report.plugin_version},
                   {"api_version", report.api_version}};
    j["device"] = report.device;
    j["result"] = report.passed() ? "pass" : "fail";
    j["counts"] = {{"pass", report.count("pass")},
                   {"fail", report.count("fail")},
                   {"skip", report.count("skip")}};
    nlohmann::ordered_json checks = nlohmann::ordered_json::array();
    for (const CheckResult& c : report.checks) {
        nlohmann::ordered_json x;
        x["id"] = c.id;
        x["title"] = c.title;
        x["requirements"] = c.requirements;
        x["result"] = c.result;
        x["detail"] = c.detail;
        x["problems"] = c.problems;
        x["seconds"] = std::round(c.seconds * 1000.0) / 1000.0;
        checks.push_back(std::move(x));
    }
    j["checks"] = std::move(checks);
    return j.dump(2) + "\n";
}

void print_conformance(const ConformanceReport& report, std::ostream& out)
{
    out << "conformance: " << report.plugin_path << "\n";
    if (!report.plugin_id.empty())
        out << "plugin: " << report.plugin_id << " " << report.plugin_version << ", radio API "
            << report.api_version << "\n";
    out << "device: " << (report.device.empty() ? "none" : report.device) << "\n";
    for (const CheckResult& c : report.checks) {
        out << "  " << c.result << " " << c.id << " - " << c.title;
        if (!c.requirements.empty()) {
            out << " (";
            for (std::size_t i = 0; i < c.requirements.size(); ++i)
                out << (i > 0 ? ", " : "") << c.requirements[i];
            out << ")";
        }
        out << "\n";
        if (!c.detail.empty())
            out << "      " << c.detail << "\n";
        for (const std::string& p : c.problems)
            out << "      problem: " << p << "\n";
    }
    out << "result: " << (report.passed() ? "PASS" : "FAIL") << " (" << report.count("pass")
        << " pass, " << report.count("fail") << " fail, " << report.count("skip") << " skip)\n";
}

const std::string& builtin_scene_json()
{
    static const std::string scene = kBuiltinScene;
    return scene;
}

} // namespace yantra::radio_host
