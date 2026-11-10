// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only fake radio plugin that drives the radio test host (RAD-91) end
// to end: one simulated device "fake:0" with the SCENE capability. It
// pulls the scene and the walk/cursor position through the host's context
// callbacks (RAD-18, RAD-36) and delivers SIMULATED records with
// sim_position (RAD-19) at a level from the distance to the cell's
// nearest antenna. Settings of every type, a preset, sensors, an action
// and a self-test exercise device control (RAD-40..46). Not an example to
// copy: the example beacon plugin (RAD-64) is that.
//
// Built twice: as fake_radio and, with FAKE_API_MAJOR=2, as a plugin the
// host must refuse (RAD-11).
#include <yantra/radio.h>
#include <yantra/radio.hpp>
#include <yantra/radios/scene.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef FAKE_API_MAJOR
#  define FAKE_API_MAJOR YR_API_VERSION_MAJOR
#endif

namespace {

// --- schemas ---------------------------------------------------------------

const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                               {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                               {YR_ROLE_OTHER, "other", "Other"}};

constexpr yr_field_def field(uint32_t type, uint32_t group, const char* name, const char* label,
                             const char* unit, uint32_t flags = 0,
                             const yr_enum_item* items = nullptr, uint32_t count = 0)
{
    return {sizeof(yr_field_def), type, group, flags, name, label, unit, "Fake value for tests",
            items, count, 0};
}

const yr_field_def kCell[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_FREQUENCY_HZ, "Frequency", "Hz", YR_FIELD_KEY),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_CHANNEL, "Channel", ""),
    field(YR_TYPE_ENUM, YR_GROUP_CORE, YR_FIELD_ROLE, "Role", "", 0, kRoles, 3),
    field(YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_LEVEL, "Level", "dBFS", YR_FIELD_DEFAULT),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_MCC, "MCC", ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_MNC, "MNC", ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_LA, "LA", ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_COLOUR_CODE, "Colour code", ""),
    field(YR_TYPE_DOUBLE, YR_GROUP_QUALITY, YR_FIELD_SNR_DB, "SNR", "dB"),
    field(YR_TYPE_STRING, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_FLOOR_ID, "Floor", ""),
    field(YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_X_M, "x", "m"),
    field(YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, YR_FIELD_SIM_Y_M, "y", "m"),
};
const yr_field_def* const kCellPtrs[] = {&kCell[0], &kCell[1], &kCell[2],  &kCell[3],
                                         &kCell[4], &kCell[5], &kCell[6],  &kCell[7],
                                         &kCell[8], &kCell[9], &kCell[10], &kCell[11]};
constexpr uint32_t kCellFields = 12;

const yr_field_def kNb[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_FREQUENCY_HZ, "Frequency", "Hz", YR_FIELD_KEY),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_ENTRY_INDEX, "Entry", ""),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_ENTRY_COUNT, "Entries", ""),
    field(YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, "neighbour_hz", "Neighbour carrier", "Hz"),
};
const yr_field_def* const kNbPtrs[] = {&kNb[0], &kNb[1], &kNb[2], &kNb[3]};

const yr_schema kCellSchema = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "fake_cell", "Fake cell",
                               1, kCellFields, kCellPtrs,
                               YR_GROUP_QUALITY | YR_GROUP_SIM_POSITION, 0};
const yr_schema kNbSchema = {sizeof(yr_schema), YR_SCHEMA_NEIGHBOUR, "fake_neighbour",
                             "Fake neighbour", 1, 4, kNbPtrs, YR_GROUP_NEIGHBOURS, 0};
const yr_schema* const kSchemas[] = {&kCellSchema, &kNbSchema};

// --- settings ----------------------------------------------------------------

const yr_enum_item kModes[] = {{0, "indoor", "Indoor"}, {1, "outdoor", "Outdoor"}};

yr_setting_def setting(uint32_t type, const char* key, const char* label, const char* unit,
                       const char* group, double lo, double hi, double step, yr_value def,
                       int32_t order, uint32_t flags = 0)
{
    yr_setting_def s{};
    s.struct_size = sizeof s;
    s.type = type;
    s.flags = flags;
    s.key = key;
    s.label = label;
    s.unit = unit;
    s.help = "Fake setting for the test host";
    s.group = group;
    s.minimum = lo;
    s.maximum = hi;
    s.step = step;
    s.default_value = def;
    s.sort_order = order;
    return s;
}

struct Device;

struct Plugin {
    const yr_host_api* host = nullptr;
    std::mutex mutex;
    std::string error;
    Device* open = nullptr;
    std::atomic<uint64_t> context_events{0};
    std::atomic<bool> scene_dirty{true};
    /** Faults for the conformance kit's own tests (YANTRA_FAKE_FAULT, a
     *  comma list read at init): no_settings_changed, off_raster, no_busy,
     *  slow_stop, any_plan, level_dbm (records flagged LEVEL_DBM while the
     *  device entry says dBFS). */
    std::string faults;
    bool fault(const char* name) const
    {
        return ("," + faults + ",").find(std::string(",") + name + ",") != std::string::npos;
    }
};

struct Device {
    Plugin* plugin = nullptr;
    std::mutex mutex;
    uint32_t state = YR_DEVICE_OPEN;
    std::string error;
    // settings
    double gain_db = 20.0;
    int64_t mode = 0;
    bool agc = false;
    std::string label = "fake";
    uint64_t stop_after = 0;
    // stream
    yr_plan plan{};
    std::vector<uint64_t> carriers;
    yr_stream* sink = nullptr;
    std::thread thread;
    std::condition_variable cv;
    bool stop = false;
    std::atomic<uint64_t> delivered{0};
    // scene
    yantra::radios::Scene scene;
    bool have_scene = false;
    std::atomic<uint64_t> scene_walls{0};
};

const char kDefaultLabel[] = "fake";

std::vector<yr_setting_def> settings_of(const Device& d)
{
    std::vector<yr_setting_def> out;
    yr_setting_def gain = setting(YR_TYPE_DOUBLE, "gain_db", "Gain", "dB", "Gain", 0.0, 40.0, 0.5,
                                  yr_value_double(20.0), 1, YR_SETTING_CALIBRATION);
    if (d.agc) {
        gain.flags |= YR_SETTING_UNAVAILABLE;
        gain.unavailable_reason = "automatic gain control is on";
    }
    out.push_back(gain);
    out.push_back(setting(YR_TYPE_BOOL, "agc", "Automatic gain", "", "Gain", NAN, NAN, 0,
                          yr_value_bool(0), 2));
    yr_setting_def mode = setting(YR_TYPE_ENUM, "mode", "Mode", "", "Receiver", NAN, NAN, 0,
                                  yr_value_enum(0), 3);
    mode.items = kModes;
    mode.item_count = 2;
    out.push_back(mode);
    out.push_back(setting(YR_TYPE_STRING, "label", "Label", "", "Receiver", NAN, NAN, 0,
                          yr_value_string(kDefaultLabel), 4));
    out.push_back(setting(YR_TYPE_UINT, "stop_after", "Stop after", "records", "Scenario", 0,
                          1e6, 1, yr_value_uint(0), 5, YR_SETTING_RESTART));
    out.push_back(setting(YR_TYPE_ACTION, "reset", "Reset counters", "", "Receiver", NAN, NAN, 0,
                          yr_value_none(), 6));
    return out;
}

std::vector<yr_setting_def> sensors_of()
{
    return {setting(YR_TYPE_DOUBLE, "temperature_c", "Temperature", "degC", "Device", NAN, NAN, 0,
                    yr_value_none(), 1, YR_SETTING_READ_ONLY),
            setting(YR_TYPE_UINT, "context_events", "Context events", "", "Debug", NAN, NAN, 0,
                    yr_value_none(), 2, YR_SETTING_READ_ONLY),
            setting(YR_TYPE_UINT, "scene_walls", "Scene walls", "", "Debug", NAN, NAN, 0,
                    yr_value_none(), 3, YR_SETTING_READ_ONLY)};
}

void set_error(Plugin* p, Device* d, const std::string& text)
{
    if (d != nullptr) {
        std::lock_guard lock(d->mutex);
        d->error = text;
    } else if (p != nullptr) {
        std::lock_guard lock(p->mutex);
        p->error = text;
    }
}

// --- context -----------------------------------------------------------------

// Reloads the scene through get_scene: first with a tiny buffer to learn
// the size (BUFFER_TOO_SMALL), then with a buffer of that size.
void load_scene(Device& d)
{
    const yr_host_api* h = d.plugin->host;
    d.have_scene = false;
    d.scene_walls = 0;
    if (h->get_scene == nullptr)
        return;
    char small[8];
    uint32_t len = 0;
    yr_status st = h->get_scene(h->host, nullptr, small, sizeof small, &len);
    if (st != YR_ERR_BUFFER_TOO_SMALL)
        return;
    std::string buffer(len + 1u, '\0');
    st = h->get_scene(h->host, "", buffer.data(), len + 1u, &len);
    if (st != YR_OK)
        return;
    buffer.resize(len);
    std::string error;
    if (!yantra::radios::parse_scene(buffer, d.scene, error)) {
        h->log(h->host, nullptr, YR_LOG_WARNING, "the scene from the host is invalid");
        return;
    }
    d.have_scene = true;
    d.scene_walls = d.scene.walls.size();
    yr_floor_plan_info info{};
    info.struct_size = sizeof info;
    if (h->get_floor_plan(h->host, &info) == YR_OK) {
        // Context data only at debug level (RAD-19).
        const std::string text = std::string("scene of plan ") + info.plan_name;
        h->log(h->host, nullptr, YR_LOG_DEBUG, text.c_str());
    }
}

// --- stream thread -------------------------------------------------------------

void stream_main(Device* d)
{
    const yr_host_api* h = d->plugin->host;
    bool first = true;
    uint64_t pushed = 0;
    std::unique_lock lock(d->mutex);
    while (!d->stop) {
        // A consistent copy of what the control thread may change.
        const yr_plan plan = d->plan;
        const std::vector<uint64_t> carriers = d->carriers;
        const double gain_db = d->gain_db;
        const uint64_t stop_after = d->stop_after;
        lock.unlock();
        if (d->plugin->scene_dirty.exchange(false))
            load_scene(*d);
        // Carriers: the plan's list, the scene's cells, or one default cell.
        struct Cell {
            uint64_t hz;
            uint32_t mcc, mnc, la, cc;
            std::string id;
        };
        std::vector<Cell> cells;
        if (plan.type == YR_PLAN_CARRIERS) {
            for (uint64_t hz : carriers)
                cells.push_back({hz, 901, 1, 1, 1, ""});
        } else if (d->have_scene && !d->scene.cells.empty()) {
            const auto& c = d->scene.cells.front();
            cells.push_back({c.carrier_hz, c.mcc, c.mnc, c.la, c.colour_code, c.id});
        } else {
            cells.push_back({plan.serving_hz != 0 ? plan.serving_hz : 392'012'500u, 901, 1,
                             1, 1, ""});
        }
        // Position: the walk position, else the cursor.
        yr_plan_position pos{};
        pos.struct_size = sizeof pos;
        bool have_pos = false;
        if (h->get_walk_position != nullptr)
            have_pos = h->get_walk_position(h->host, &pos) == YR_OK;
        if (!have_pos && h->get_cursor != nullptr) {
            pos = yr_plan_position{};
            pos.struct_size = sizeof pos;
            have_pos = h->get_cursor(h->host, &pos) == YR_OK;
        }
        if (first) {
            h->stream_event(d->sink, YR_EVENT_SYNC_FOUND, static_cast<int64_t>(cells[0].hz));
            first = false;
        }
        for (const Cell& c : cells) {
            double level = -50.0 + (gain_db - 20.0);
            if (have_pos && d->have_scene) {
                double best = 1e9;
                for (const auto& a : d->scene.antennas)
                    if (a.cell == c.id)
                        best = std::min(best, std::hypot(a.x_m - pos.x_m, a.y_m - pos.y_m));
                if (best < 1e9)
                    level = -30.0 - 20.0 * std::log10(std::max(best, 1.0)) + (gain_db - 20.0);
            }
            yr_value v[kCellFields];
            v[YR_CORE_FREQUENCY_HZ] = yr_value_uint(c.hz);
            v[YR_CORE_CHANNEL] = yr_value_uint(3600);
            v[YR_CORE_ROLE] = yr_value_enum(YR_ROLE_SERVING);
            v[YR_CORE_LEVEL] = yr_value_double(level);
            v[YR_CORE_MCC] = yr_value_uint(c.mcc);
            v[YR_CORE_MNC] = yr_value_uint(c.mnc);
            v[YR_CORE_LA] = yr_value_uint(c.la);
            v[YR_CORE_COLOUR_CODE] = yr_value_uint(c.cc);
            v[8] = yr_value_double(20.0);
            v[9] = have_pos ? yr_value_string(pos.floor_id) : yr_value_none();
            v[10] = have_pos ? yr_value_double(pos.x_m) : yr_value_none();
            v[11] = have_pos ? yr_value_double(pos.y_m) : yr_value_none();
            yr_record r{};
            r.struct_size = sizeof r;
            r.schema_index = 0;
            r.time.monotonic_ns = h->monotonic_ns(h->host);
            r.time.utc_ns = h->utc_now_ns(h->host);
            r.time.utc_source = YR_TIME_SOURCE_SYSTEM;
            r.flags = YR_RECORD_SIMULATED | (d->plugin->fault("level_dbm") ? YR_RECORD_LEVEL_DBM : 0u);
            r.present = YR_GROUP_QUALITY | (have_pos ? YR_GROUP_SIM_POSITION : 0u);
            r.value_count = kCellFields;
            r.values = v;
            if (h->push_record(d->sink, &r) == YR_OK)
                ++d->delivered;
            ++pushed;
            if ((plan.flags & YR_PLAN_MEASURE_NEIGHBOURS) != 0) {
                yr_value nv[4] = {yr_value_uint(c.hz), yr_value_uint(0), yr_value_uint(1),
                                  yr_value_uint(c.hz + 25'000)};
                yr_record n = r;
                n.schema_index = 1;
                n.present = YR_GROUP_NEIGHBOURS;
                n.value_count = 4;
                n.values = nv;
                if (h->push_record(d->sink, &n) == YR_OK)
                    ++d->delivered;
            }
        }
        lock.lock();
        if (stop_after != 0 && pushed >= stop_after) {
            d->state = YR_DEVICE_OPEN;
            lock.unlock();
            h->stream_event(d->sink, YR_EVENT_END_OF_STREAM, 0);
            return;
        }
        // Pace the fake receiver: one round per millisecond.
        d->cv.wait_for(lock, std::chrono::milliseconds(1), [&] { return d->stop; });
    }
}

// --- plugin functions ------------------------------------------------------------

#define FAKE_GUARD(body)                                                                          \
    try {                                                                                         \
        body                                                                                      \
    } catch (...) {                                                                               \
        return YR_ERR_FAILED;                                                                     \
    }

yr_status fake_init(const yr_host_api* host, yr_plugin** out)
{
    FAKE_GUARD({
        if (host == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        auto* p = new Plugin();
        p->host = host;
        if (const char* f = std::getenv("YANTRA_FAKE_FAULT"))
            p->faults = f;
        *out = reinterpret_cast<yr_plugin*>(p);
        host->log(host->host, nullptr, YR_LOG_INFO, "fake radio ready");
        return YR_OK;
    })
}

void fake_shutdown(yr_plugin* plugin)
{
    delete reinterpret_cast<Plugin*>(plugin);
}

Plugin* P(yr_plugin* p)
{
    return reinterpret_cast<Plugin*>(p);
}
Device* D(yr_device* d)
{
    return reinterpret_cast<Device*>(d);
}

yr_status fake_enumerate(yr_plugin* plugin, yr_device_cb cb, void* user)
{
    if (std::getenv("YANTRA_FAKE_NO_DEVICE") != nullptr) {
        set_error(P(plugin), nullptr, "no fake device: YANTRA_FAKE_NO_DEVICE is set");
        return YR_ERR_NO_DEVICE;
    }
    yr_device_info info{sizeof(yr_device_info), YR_DEVICE_SIMULATED, "fake:0", "Fake device 0",
                        "fake", "0000", "1.0", "board=fake", ""};
    cb(user, &info);
    return YR_OK;
}

yr_status fake_open(yr_plugin* plugin, const char* key, yr_device** out)
{
    FAKE_GUARD({
        Plugin* p = P(plugin);
        if (key == nullptr || std::strcmp(key, "fake:0") != 0) {
            set_error(p, nullptr, "unknown device key");
            return YR_ERR_NOT_FOUND;
        }
        if (p->open != nullptr) {
            set_error(p, nullptr, "fake:0 is already open");
            return YR_ERR_BUSY;
        }
        auto* d = new Device();
        d->plugin = p;
        p->open = d;
        *out = reinterpret_cast<yr_device*>(d);
        return YR_OK;
    })
}

yr_status fake_stop(yr_device* device)
{
    Device* d = D(device);
    {
        std::lock_guard lock(d->mutex);
        d->stop = true;
    }
    d->cv.notify_all();
    if (d->thread.joinable())
        d->thread.join();
    if (d->plugin->fault("slow_stop")) // just over the 1 s limit of RAD-15
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::lock_guard lock(d->mutex);
    if (d->state == YR_DEVICE_STREAMING)
        d->state = YR_DEVICE_OPEN;
    d->sink = nullptr;
    return YR_OK;
}

void fake_close(yr_device* device)
{
    Device* d = D(device);
    fake_stop(device);
    d->plugin->open = nullptr;
    delete d;
}

yr_status fake_device_info(yr_device*, yr_device_cb cb, void* user)
{
    return fake_enumerate(nullptr, cb, user);
}

uint32_t fake_state(yr_device* device)
{
    std::lock_guard lock(D(device)->mutex);
    return D(device)->state;
}

yr_status fake_plan_ranges(yr_device*, yr_plan_ranges_cb cb, void* user)
{
    static const yr_freq_range ranges[] = {{380'000'000, 400'000'000}};
    yr_plan_ranges r{sizeof r, (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS), 1, 4, ranges,
                     1e6};
    cb(user, &r);
    return YR_OK;
}

yr_status fake_set_plan(yr_device* device, const yr_plan* plan)
{
    Device* d = D(device);
    std::lock_guard lock(d->mutex);
    if (d->state == YR_DEVICE_STREAMING)
        return YR_ERR_BUSY;
    if (plan == nullptr || (plan->type != YR_PLAN_SERVING && plan->type != YR_PLAN_CARRIERS &&
                            !d->plugin->fault("any_plan")))
        return YR_ERR_UNSUPPORTED;
    d->plan = *plan;
    d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
    d->plan.carriers_hz = nullptr;
    return YR_OK;
}

yr_status fake_start(yr_device* device, const yr_plan* plan, yr_stream* sink)
{
    FAKE_GUARD({
        Device* d = D(device);
        if (plan == nullptr || sink == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        if (plan->type == YR_PLAN_SCAN) {
            set_error(nullptr, d, "the fake radio cannot scan");
            return YR_ERR_UNSUPPORTED;
        }
        if (plan->type != YR_PLAN_SERVING && plan->type != YR_PLAN_CARRIERS &&
            !d->plugin->fault("any_plan")) {
            set_error(nullptr, d, "unknown plan type");
            return YR_ERR_UNSUPPORTED;
        }
        if (plan->type == YR_PLAN_CARRIERS && (plan->carrier_count == 0 || plan->carriers_hz == nullptr)) {
            set_error(nullptr, d, "a carrier plan needs at least one carrier");
            return YR_ERR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(d->mutex);
        if (d->state != YR_DEVICE_OPEN)
            return YR_ERR_BUSY;
        if (d->thread.joinable())
            d->thread.join(); // a stream that ended by itself
        d->plan = *plan;
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr;
        d->sink = sink;
        d->stop = false;
        d->delivered = 0;
        d->state = YR_DEVICE_STREAMING;
        d->plugin->scene_dirty = true;
        d->thread = std::thread(stream_main, d);
        return YR_OK;
    })
}

yr_status fake_stats(yr_device* device, yr_stream_stats* out)
{
    if (out == nullptr || out->struct_size < sizeof(yr_stream_stats))
        return YR_ERR_INVALID_ARGUMENT;
    out->records_delivered = D(device)->delivered;
    out->records_dropped = 0;
    out->samples_dropped = 0;
    out->overflows = 0;
    out->receiver_load = NAN;
    out->sample_rate_hz = NAN;
    return YR_OK;
}

yr_status fake_last_error(yr_plugin* plugin, yr_device* device, char* buffer, uint32_t size)
{
    std::string text;
    if (device != nullptr) {
        std::lock_guard lock(D(device)->mutex);
        text = D(device)->error;
    } else if (plugin != nullptr) {
        std::lock_guard lock(P(plugin)->mutex);
        text = P(plugin)->error;
    }
    return yantra::radio::copy_text(text, buffer, size);
}

yr_status fake_list_settings(yr_device* device, yr_setting_cb cb, void* user)
{
    FAKE_GUARD({
        std::vector<yr_setting_def> defs;
        {
            std::lock_guard lock(D(device)->mutex);
            defs = settings_of(*D(device));
        }
        for (const yr_setting_def& s : defs)
            cb(user, &s);
        return YR_OK;
    })
}

yr_status fake_get(yr_device* device, const char* key, yr_value* out, char* text, uint32_t size)
{
    Device* d = D(device);
    std::lock_guard lock(d->mutex);
    const std::string k = key != nullptr ? key : "";
    if (k == "gain_db") {
        *out = yr_value_double(d->gain_db);
    } else if (k == "agc") {
        *out = yr_value_bool(d->agc ? 1 : 0);
    } else if (k == "mode") {
        *out = yr_value_enum(d->mode);
    } else if (k == "label") {
        const yr_status st = yantra::radio::copy_text(d->label, text, size);
        if (st != YR_OK)
            return st;
        *out = yr_value_string(text);
    } else if (k == "stop_after") {
        *out = yr_value_uint(d->stop_after);
    } else {
        d->error = "unknown setting " + k;
        return YR_ERR_NOT_FOUND;
    }
    return YR_OK;
}

yr_status fake_set(yr_device* device, const char* key, const yr_value* value)
{
    FAKE_GUARD({
        Device* d = D(device);
        const std::string k = key != nullptr ? key : "";
        std::vector<yr_setting_def> defs;
        {
            std::lock_guard lock(d->mutex);
            defs = settings_of(*d);
        }
        const yr_setting_def* def = nullptr;
        for (const yr_setting_def& s : defs)
            if (k == s.key)
                def = &s;
        if (def == nullptr || value == nullptr) {
            set_error(nullptr, d, "unknown setting " + k);
            return YR_ERR_NOT_FOUND;
        }
        std::string why;
        if (yantra::radio::check_setting_value(*def, *value, &why) != YR_OK) {
            set_error(nullptr, d, why);
            return YR_ERR_INVALID_ARGUMENT;
        }
        bool changed_others = false;
        {
            std::lock_guard lock(d->mutex);
            if ((def->flags & YR_SETTING_RESTART) != 0 && d->state == YR_DEVICE_STREAMING &&
                !d->plugin->fault("no_busy"))
                return YR_ERR_BUSY;
            if (k == "gain_db") {
                d->gain_db = d->plugin->fault("off_raster")
                                 ? value->u.f64
                                 : std::round(value->u.f64 / 0.5) * 0.5; // the device's 0.5 dB step
            } else if (k == "agc") {
                changed_others = d->agc != (value->u.i64 != 0);
                d->agc = value->u.i64 != 0;
            } else if (k == "mode") {
                changed_others = d->mode != value->u.i64;
                d->mode = value->u.i64;
            } else if (k == "label") {
                d->label = value->u.str;
            } else if (k == "stop_after") {
                d->stop_after = value->u.u64;
            }
        }
        if (changed_others && !d->plugin->fault("no_settings_changed"))
            d->plugin->host->settings_changed(d->plugin->host->host, device);
        return YR_OK;
    })
}

yr_status fake_action(yr_device* device, const char* key, char* result, uint32_t size)
{
    if (key == nullptr || std::strcmp(key, "reset") != 0) {
        set_error(nullptr, D(device), "unknown action");
        return YR_ERR_NOT_FOUND;
    }
    D(device)->delivered = 0;
    return yantra::radio::copy_text("counters reset", result, size);
}

yr_status fake_list_presets(yr_device*, yr_preset_cb cb, void* user)
{
    const yr_preset p{sizeof(yr_preset), 0, "indoor", "Indoor, sensitive",
                      "Gain 30 dB, mode indoor, no AGC"};
    cb(user, &p);
    return YR_OK;
}

yr_status fake_apply_preset(yr_device* device, const char* id)
{
    Device* d = D(device);
    if (id == nullptr || std::strcmp(id, "indoor") != 0) {
        set_error(nullptr, d, "unknown preset");
        return YR_ERR_NOT_FOUND;
    }
    {
        std::lock_guard lock(d->mutex);
        d->gain_db = 30.0;
        d->mode = 0;
        d->agc = false;
    }
    if (!d->plugin->fault("no_settings_changed"))
        d->plugin->host->settings_changed(d->plugin->host->host, device);
    return YR_OK;
}

yr_status fake_list_sensors(yr_device*, yr_setting_cb cb, void* user)
{
    FAKE_GUARD({
        for (const yr_setting_def& s : sensors_of())
            cb(user, &s);
        return YR_OK;
    })
}

yr_status fake_read_sensor(yr_device* device, const char* key, yr_value* out, char*, uint32_t)
{
    Device* d = D(device);
    const std::string k = key != nullptr ? key : "";
    if (k == "temperature_c")
        *out = yr_value_double(42.5);
    else if (k == "context_events")
        *out = yr_value_uint(d->plugin->context_events.load());
    else if (k == "scene_walls")
        *out = yr_value_uint(d->scene_walls.load());
    else
        return YR_ERR_NOT_FOUND;
    return YR_OK;
}

yr_status fake_self_test(yr_device* device, yr_selftest_cb cb, void* user)
{
    if (fake_state(device) == YR_DEVICE_STREAMING)
        return YR_ERR_BUSY;
    const yr_selftest_item items[] = {
        {sizeof(yr_selftest_item), YR_TEST_PASS, "usb", "USB link", "simulated", ""},
        {sizeof(yr_selftest_item), YR_TEST_WARN, "clock", "Reference clock", "no external clock",
         "connect a 10 MHz reference for better accuracy"},
        {sizeof(yr_selftest_item), YR_TEST_SKIPPED, "antenna", "Antenna port",
         "not measurable on a fake device", ""}};
    for (const yr_selftest_item& it : items)
        cb(user, &it);
    return YR_OK;
}

void fake_context(yr_plugin* plugin, uint32_t event)
{
    // Returns at once; the stream thread asks for what it needs (RAD-18).
    Plugin* p = P(plugin);
    ++p->context_events;
    if (event == YR_CONTEXT_PLAN_CHANGED || event == YR_CONTEXT_SCENE_CHANGED)
        p->scene_dirty = true;
}

const yr_stream_control kStream = {sizeof(yr_stream_control), 0, fake_enumerate, fake_open,
                                   fake_close, fake_device_info, fake_state, fake_plan_ranges,
                                   fake_set_plan, fake_start, fake_stop, fake_stats,
                                   fake_last_error};

const yr_device_control kDevice = {sizeof(yr_device_control), 0, fake_list_settings, fake_get,
                                   fake_set, fake_action, fake_list_presets, fake_apply_preset,
                                   fake_list_sensors, fake_read_sensor, fake_self_test};

} // namespace

extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const yr_descriptor d = {
        sizeof(yr_descriptor),
        // fake_radio_v2 is a plugin of radio API 2.0 (the host refuses it).
        {FAKE_API_MAJOR, FAKE_API_MAJOR == YR_API_VERSION_MAJOR ? YR_API_VERSION_MINOR : 0u},
        YR_CAP_SIMULATED | YR_CAP_SCENE,
        "fake-radio",
        "Fake radio (tests)",
        "0.1.0",
        "The Yantra contributors",
        "GPL-3.0-or-later",
        "Test-only plugin for the radio test host",
        "https://example.invalid/yantra-radios-test-plugin",
        "fake",
        2,
        0,
        kSchemas,
        nullptr,
        fake_init,
        fake_shutdown,
        &kStream,
        &kDevice,
        fake_context,
    };
    return &d;
}
