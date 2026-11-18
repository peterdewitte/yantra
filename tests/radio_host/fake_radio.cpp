// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A fake radio plugin for the radio host tests (PLG-25, PLG-27): radio
// API 1.2 (yantra/radio.h of the yantra-radios submodule) with the
// SIMULATED, SCENE, HOTPLUG and (API 1.2) SPECTRUM capabilities, one device "fake:0", one
// MEASUREMENT schema with the core fields and the sim_position group. It
// processes no signal: the test drives it through fake_control.h.
// Built a second time with FAKE_API_MAJOR=2 for the version refusal.

#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <yantra/radio.hpp>

#include "fake_control.h"

#ifndef FAKE_API_MAJOR
#define FAKE_API_MAJOR YR_API_VERSION_MAJOR
#endif

struct yr_plugin {
    const yr_host_api* host = nullptr;
};
struct yr_device {
    int id = 0;
};

namespace {

fake::Control g_ctl;
yr_plugin g_plugin;
yr_device g_device;
bool g_open = false;
bool g_streaming = false;
bool g_lost = false;
yr_stream* g_sink = nullptr;     // guarded by g_ctl.m
yr_stream* g_lost_sink = nullptr;  // the stream that saw DEVICE_LOST

// Waits in a call that is set to hang until released (lock held).
void maybe_hang(std::unique_lock<std::mutex>& lock, fake::Call c) {
    if (!g_ctl.hang[c]) return;
    ++g_ctl.hanging;
    g_ctl.cv.notify_all();
    g_ctl.cv.wait(lock, [] { return g_ctl.released; });
    --g_ctl.hanging;
    g_ctl.cv.notify_all();
}

yr_status result_of(fake::Call c, yr_status real) {
    return g_ctl.result[c] != 0 ? g_ctl.result[c] : real;
}

// --- schema --------------------------------------------------------------------

const yr_enum_item role_items[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                                   {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                                   {YR_ROLE_OTHER, "other", "Other"}};

#define FIELD(var, name, type, group, unit, items, count)                                   \
    const yr_field_def var = {sizeof(yr_field_def), type, group, 0, name, "Label " name, unit, \
                              "Help " name, items, count, 0}

FIELD(f_freq, YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, "Hz", nullptr, 0);
FIELD(f_chan, YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, "", nullptr, 0);
FIELD(f_role, YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, "", role_items, 3);
FIELD(f_level, YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, "dBFS", nullptr, 0);
FIELD(f_mcc, YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, "", nullptr, 0);
FIELD(f_mnc, YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, "", nullptr, 0);
FIELD(f_la, YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, "", nullptr, 0);
FIELD(f_cc, YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, "", nullptr, 0);
FIELD(f_sim_floor, YR_FIELD_SIM_FLOOR_ID, YR_TYPE_STRING, YR_GROUP_SIM_POSITION, "", nullptr, 0);
FIELD(f_sim_x, YR_FIELD_SIM_X_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", nullptr, 0);
FIELD(f_sim_y, YR_FIELD_SIM_Y_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", nullptr, 0);

const yr_field_def* const fields[] = {&f_freq, &f_chan,  &f_role,      &f_level,
                                      &f_mcc,  &f_mnc,   &f_la,        &f_cc,
                                      &f_sim_floor, &f_sim_x, &f_sim_y};
const yr_schema schema = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "fake_cell", "Fake cell", 1,
                          11, fields, YR_GROUP_SIM_POSITION, 0};
const yr_schema* const schemas[] = {&schema};

// --- stream control ----------------------------------------------------------------

const yr_device_info info = {sizeof(yr_device_info), 0, "fake:0", "Fake receiver", "fake",
                             "F0", "1.0", "none", nullptr};

yr_status enumerate(yr_plugin*, yr_device_cb cb, void* user) {
    std::unique_lock lock(g_ctl.m);
    maybe_hang(lock, fake::kEnumerate);
    if (!g_ctl.present) return result_of(fake::kEnumerate, YR_ERR_NO_DEVICE);
    const yr_status st = result_of(fake::kEnumerate, YR_OK);
    yr_device_info shown = info;
    shown.flags = g_ctl.device_flags;
    const std::string reason = g_ctl.flags_reason;
    shown.flags_reason = reason.c_str();
    lock.unlock();
    if (st == YR_OK) cb(user, &shown);
    return st;
}

yr_status open(yr_plugin*, const char* key, yr_device** out) {
    std::unique_lock lock(g_ctl.m);
    ++g_ctl.opens;
    maybe_hang(lock, fake::kOpen);
    if (key == nullptr || std::strcmp(key, "fake:0") != 0) return YR_ERR_NOT_FOUND;
    if (!g_ctl.present) return YR_ERR_NO_DEVICE;
    if (g_open) return YR_ERR_BUSY;
    const yr_status st = result_of(fake::kOpen, YR_OK);
    if (st != YR_OK) return st;
    g_open = true;
    g_lost = false;
    g_ctl.settings.clear();
    g_ctl.setting_order.clear();
    *out = &g_device;
    return YR_OK;
}

void close(yr_device*) {
    const std::lock_guard lock(g_ctl.m);
    ++g_ctl.closes;
    g_sink = nullptr;
    g_lost_sink = nullptr;
    g_streaming = false;
    g_open = false;
    g_lost = false;
}

yr_status device_info(yr_device*, yr_device_cb cb, void* user) {
    cb(user, &info);
    return YR_OK;
}

std::uint32_t state(yr_device*) {
    const std::lock_guard lock(g_ctl.m);
    if (g_lost) return YR_DEVICE_LOST;
    return g_streaming ? YR_DEVICE_STREAMING : YR_DEVICE_OPEN;
}

yr_status plan_ranges(yr_device*, yr_plan_ranges_cb, void*) { return YR_ERR_UNSUPPORTED; }

// Under g_ctl.m.
void remember_plan(const yr_plan* plan) {
    yr_plan p{};
    if (plan != nullptr) yr_plan_read(plan, &p);
    g_ctl.last_plan_hz = p.serving_hz;
    g_ctl.last_plan_flags = p.flags;
    g_ctl.last_spectrum_first_hz = p.spectrum_first_hz;
    g_ctl.last_spectrum_last_hz = p.spectrum_last_hz;
}

yr_status set_plan(yr_device*, const yr_plan* plan) {
    const std::lock_guard lock(g_ctl.m);
    if (g_lost) return YR_ERR_DEVICE_LOST;
    if (g_streaming) return YR_ERR_BUSY;  // no RETUNE_LIVE
    remember_plan(plan);
    return YR_OK;
}

yr_status start_stream(yr_device*, const yr_plan* plan, yr_stream* sink) {
    std::unique_lock lock(g_ctl.m);
    ++g_ctl.starts;
    maybe_hang(lock, fake::kStart);
    if (g_lost) return YR_ERR_DEVICE_LOST;
    if (g_streaming) return YR_ERR_BUSY;
    const yr_status st = result_of(fake::kStart, YR_OK);
    if (st != YR_OK) return st;
    remember_plan(plan);
    g_sink = sink;
    g_streaming = true;
    return YR_OK;
}

yr_status stop_stream(yr_device*) {
    std::unique_lock lock(g_ctl.m);
    ++g_ctl.stops;
    maybe_hang(lock, fake::kStop);
    g_sink = nullptr;
    g_streaming = false;
    return result_of(fake::kStop, g_lost ? YR_ERR_DEVICE_LOST : YR_OK);
}

yr_status stream_stats(yr_device*, yr_stream_stats* out) {
    if (out == nullptr) return YR_ERR_INVALID_ARGUMENT;
    out->records_delivered = 0;
    return YR_OK;
}

yr_status last_error(yr_plugin*, yr_device*, char* buffer, std::uint32_t size) {
    return yantra::radio::copy_text(g_ctl.present ? "fake error" : "fake device unplugged",
                                    buffer, size);
}

const yr_stream_control stream_control = {
    sizeof(yr_stream_control), 0, enumerate, open, close, device_info, state, plan_ranges,
    set_plan, start_stream, stop_stream, stream_stats, last_error};

// --- device control ----------------------------------------------------------------

// "gain" is part of the calibration key (CALIBRATION, HW-01b); "rate"
// needs a restart.
const yr_setting_def gain_def = {sizeof(yr_setting_def), YR_TYPE_DOUBLE, YR_SETTING_CALIBRATION, 0,
                                 "gain", "Gain", "dB", "Receiver gain.", "Gain", nullptr, nullptr,
                                 nullptr, 0.0, 60.0, 1.0, {YR_TYPE_DOUBLE, 0, {.f64 = 0.0}}, 0, 0};
const yr_setting_def rate_def = {sizeof(yr_setting_def), YR_TYPE_INT, YR_SETTING_RESTART, 0,
                                 "rate", "Rate", "", "Record rate.", "Receiver", nullptr, nullptr,
                                 nullptr, 1.0, 10.0, 1.0, {YR_TYPE_INT, 0, {.i64 = 1}}, 0, 0};

yr_status list_settings(yr_device*, yr_setting_cb cb, void* user) {
    cb(user, &gain_def);
    cb(user, &rate_def);
    return YR_OK;
}

std::string value_text(const yr_value* v) {
    switch (v->type) {
    case YR_TYPE_BOOL:
    case YR_TYPE_INT:
    case YR_TYPE_ENUM: return std::to_string(v->u.i64);
    case YR_TYPE_UINT: return std::to_string(v->u.u64);
    case YR_TYPE_DOUBLE: return std::to_string(v->u.f64);
    case YR_TYPE_STRING: return v->u.str != nullptr ? v->u.str : "";
    default: return "?";
    }
}

yr_status get_setting(yr_device*, const char* key, yr_value* out, char* text, std::uint32_t size) {
    const std::lock_guard lock(g_ctl.m);
    const auto it = g_ctl.settings.find(key != nullptr ? key : "");
    if (it == g_ctl.settings.end()) return YR_ERR_NOT_FOUND;
    *out = yr_value_double(std::stod(it->second));
    if (text != nullptr && size > 0) yantra::radio::copy_text(it->second, text, size);
    return YR_OK;
}

// "gain" (DOUBLE, live) and "rate" (INT, RESTART: BUSY while streaming).
yr_status set_setting(yr_device*, const char* key, const yr_value* v) {
    std::unique_lock lock(g_ctl.m);
    maybe_hang(lock, fake::kSetSetting);
    if (g_ctl.result[fake::kSetSetting] != 0) return g_ctl.result[fake::kSetSetting];
    const std::string k = key != nullptr ? key : "";
    if (k == "gain") {
        if (v->type != YR_TYPE_DOUBLE) return YR_ERR_INVALID_ARGUMENT;
    } else if (k == "rate") {
        if (v->type != YR_TYPE_INT) return YR_ERR_INVALID_ARGUMENT;
        if (g_streaming) return YR_ERR_BUSY;
    } else {
        return YR_ERR_NOT_FOUND;
    }
    g_ctl.settings[k] = value_text(v);
    g_ctl.setting_order.push_back(k);
    return YR_OK;
}

yr_status run_action(yr_device*, const char*, char*, std::uint32_t) { return YR_ERR_NOT_FOUND; }

yr_status self_test(yr_device*, yr_selftest_cb, void*) { return YR_ERR_UNSUPPORTED; }

const yr_device_control device_control = {sizeof(yr_device_control), 0, list_settings, get_setting,
                                          set_setting, run_action, nullptr, nullptr, nullptr,
                                          nullptr, self_test};

// --- plugin ------------------------------------------------------------------------

yr_status init(const yr_host_api* host, yr_plugin** out) {
    const std::lock_guard lock(g_ctl.m);
    ++g_ctl.inits;
    g_plugin.host = host;
    *out = &g_plugin;
    host->log(host->host, nullptr, YR_LOG_INFO, "fake radio ready");
    return YR_OK;
}

void shutdown(yr_plugin* p) {
    const std::lock_guard lock(g_ctl.m);
    ++g_ctl.shutdowns;
    p->host = nullptr;
}

void on_context_event(yr_plugin*, std::uint32_t event) {
    std::unique_lock lock(g_ctl.m);
    ++g_ctl.context_waiting;
    g_ctl.cv.notify_all();
    g_ctl.cv.wait(lock, [] { return !g_ctl.block_context || g_ctl.released; });
    --g_ctl.context_waiting;
    ++g_ctl.context_events[event];
    g_ctl.cv.notify_all();
}

const yr_descriptor descriptor = {sizeof(yr_descriptor),
                                  {FAKE_API_MAJOR, YR_API_VERSION_MINOR},
                                  YR_CAP_SIMULATED | YR_CAP_SCENE | YR_CAP_HOTPLUG | YR_CAP_SPECTRUM,
                                  "test.fake",
                                  "Fake radio",
                                  "0.1.0",
                                  "The Yantra contributors",
                                  "GPL-3.0-or-later",
                                  "A fake radio plugin for the radio host tests.",
                                  "https://example.org/fake-radio",
                                  "faketech",
                                  1,
                                  0,
                                  schemas,
                                  nullptr,
                                  init,
                                  shutdown,
                                  &stream_control,
                                  &device_control,
                                  on_context_event};

}  // namespace

extern "C" {

YR_EXPORT const yr_descriptor* yantra_radio_entry(void) { return &descriptor; }

YR_EXPORT fake::Control* fake_control() { return &g_ctl; }

// Pushes `count` records into the running stream (from the caller's
// thread, as a plugin's stream thread would); `invalid` makes them break
// the schema. Returns how many the host answered with YR_OK.
YR_EXPORT int fake_push(int count, int invalid) {
    const std::lock_guard lock(g_ctl.m);
    if (g_plugin.host == nullptr) return 0;
    int ok = 0;
    for (int i = 0; i < count; ++i) {
        yr_value v[11];
        v[0] = yr_value_uint(390'012'500ull + static_cast<std::uint64_t>(i % 4) * 25'000ull);
        v[1] = yr_value_uint(1000 + static_cast<std::uint64_t>(i % 4));
        v[2] = yr_value_enum(YR_ROLE_SERVING);
        v[3] = invalid != 0 ? yr_value_uint(5) : yr_value_double(-40.0 - i % 10);
        v[4] = yr_value_uint(901);
        v[5] = yr_value_uint(99);
        v[6] = yr_value_uint(7);
        v[7] = yr_value_uint(1);
        v[8] = yr_value_string("ground");
        v[9] = yr_value_double(1.5);
        v[10] = yr_value_double(2.5);
        yr_record r{};
        r.struct_size = sizeof r;
        r.schema_index = 0;
        r.time.monotonic_ns = g_plugin.host->monotonic_ns(g_plugin.host->host);
        r.flags = YR_RECORD_SIMULATED;
        r.present = YR_GROUP_SIM_POSITION;
        r.value_count = 11;
        r.values = v;
        // The host answers BUSY outside a stream; a NULL sink is the host's problem.
        if (g_plugin.host->push_record(g_sink, &r) == YR_OK) ++ok;
    }
    return ok;
}

// Pushes one serving record measured at `monotonic_ns` (0: now) on `hz`
// with `level` dBFS and extra record flags (late, out-of-order, burst and
// duplicate records of the measurement pipeline tests, TST-23). Returns
// the host's result.
YR_EXPORT int fake_push_one(std::int64_t monotonic_ns, std::uint64_t hz, double level,
                            std::uint32_t flags) {
    const std::lock_guard lock(g_ctl.m);
    if (g_plugin.host == nullptr) return YR_ERR_BUSY;
    const bool no_sync = (flags & YR_RECORD_NO_SYNC) != 0;
    yr_value v[11];
    v[0] = yr_value_uint(hz);
    v[1] = no_sync ? yr_value_none() : yr_value_uint(1000);
    v[2] = yr_value_enum(YR_ROLE_SERVING);
    v[3] = yr_value_double(level);
    for (int i = 4; i < 8; ++i) v[i] = no_sync ? yr_value_none() : yr_value_uint(static_cast<std::uint64_t>(i));
    v[8] = yr_value_string("ground");
    v[9] = yr_value_double(1.5);
    v[10] = yr_value_double(2.5);
    yr_record r{};
    r.struct_size = sizeof r;
    r.schema_index = 0;
    r.time.monotonic_ns =
        monotonic_ns != 0 ? monotonic_ns : g_plugin.host->monotonic_ns(g_plugin.host->host);
    r.flags = YR_RECORD_SIMULATED | flags;
    r.present = YR_GROUP_SIM_POSITION;
    r.value_count = 11;
    r.values = v;
    return g_plugin.host->push_record(g_sink, &r);
}

YR_EXPORT int fake_push_spectrum(std::uint32_t bins, double start_hz, double width_hz, int invalid) {
    const std::lock_guard lock(g_ctl.m);
    if (g_plugin.host == nullptr) return YR_ERR_BUSY;
    const yr_push_spectrum_fn push = yr_host_spectrum_sink(g_plugin.host);
    if (push == nullptr || g_sink == nullptr) return YR_ERR_BUSY;
    std::vector<double> power(bins, -100.0);
    for (std::uint32_t i = 0; i < bins; i += 10) power[i] = -60.0;
    if (invalid != 0 && bins > 0) power[bins / 2] = std::nan("");
    yr_spectrum s{};
    s.struct_size = sizeof s;
    s.flags = YR_RECORD_SIMULATED;
    s.time.monotonic_ns = g_plugin.host->monotonic_ns(g_plugin.host->host);
    s.time.utc_ns = g_plugin.host->utc_now_ns(g_plugin.host->host);
    s.time.utc_source = YR_TIME_SOURCE_SYSTEM;
    s.start_hz = start_hz;
    s.bin_width_hz = width_hz;
    s.bin_count = bins;
    s.stop_hz = start_hz + bins * width_hz;
    s.power_db = power.data();
    return push(g_sink, &s);
}

YR_EXPORT void fake_event(std::uint32_t event, std::int64_t value) {
    const std::lock_guard lock(g_ctl.m);
    if (g_plugin.host != nullptr && g_sink != nullptr) g_plugin.host->stream_event(g_sink, event, value);
}

// The device is unplugged: DEVICE_LOST on the stream, devices_changed.
YR_EXPORT void fake_unplug() {
    std::unique_lock lock(g_ctl.m);
    g_ctl.present = false;
    if (!g_open) return;
    g_lost = true;
    g_streaming = false;
    g_lost_sink = g_sink;
    g_sink = nullptr;
    const yr_host_api* host = g_plugin.host;
    if (host != nullptr && g_lost_sink != nullptr)
        host->stream_event(g_lost_sink, YR_EVENT_DEVICE_LOST, 0);
    lock.unlock();
    if (host != nullptr) host->devices_changed(host->host);
}

YR_EXPORT void fake_replug(int send_back) {
    std::unique_lock lock(g_ctl.m);
    g_ctl.present = true;
    const yr_host_api* host = g_plugin.host;
    if (host == nullptr) return;
    if (send_back != 0 && g_lost_sink != nullptr) {
        host->stream_event(g_lost_sink, YR_EVENT_DEVICE_BACK, 0);
        return;
    }
    lock.unlock();
    host->devices_changed(host->host);
}

// The device changed a setting by itself (e.g. an automatic gain) and
// tells the host (settings_changed, RAD-44).
YR_EXPORT void fake_change_setting(const char* key, double value) {
    std::unique_lock lock(g_ctl.m);
    g_ctl.settings[key] = std::to_string(value);
    const yr_host_api* host = g_plugin.host;
    lock.unlock();
    if (host != nullptr) host->settings_changed(host->host, &g_device);
}

// Asks the host for the context (PLG-27, RAD-18/19) like a SCENE plugin.
YR_EXPORT void fake_query_context(fake::ContextAnswer* a) {
    const yr_host_api* host = g_plugin.host;
    if (host == nullptr) return;
    a->functions_set = host->get_floor_plan != nullptr && host->get_cursor != nullptr &&
                       host->get_walk_position != nullptr && host->get_scene != nullptr;
    if (!a->functions_set) return;
    a->plan_info.struct_size = sizeof a->plan_info;
    a->plan = host->get_floor_plan(host->host, &a->plan_info);
    a->cursor_pos.struct_size = sizeof a->cursor_pos;
    a->cursor = host->get_cursor(host->host, &a->cursor_pos);
    a->walk_pos.struct_size = sizeof a->walk_pos;
    a->walk = host->get_walk_position(host->host, &a->walk_pos);
    char small[4];
    a->scene_small = host->get_scene(host->host, nullptr, small, sizeof small, &a->scene_length);
    std::string buffer(a->scene_length + 1, '\0');
    a->scene = host->get_scene(host->host, nullptr, buffer.data(),
                               static_cast<std::uint32_t>(buffer.size()), nullptr);
    if (a->scene == YR_OK) a->scene_json = buffer.c_str();
}

}  // extern "C"
