// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The example radio plugin (RAD-64): a complete, minimal radio plugin for
// the made-up "beacon" technology (beacon_proto.h). Read it together with
// docs/RADIO_PLUGINS.md, which builds it step by step; the blocks between
// "[snippet:...]" markers are quoted there and checked by ctest
// (doc_snippets), so the tutorial always shows the real code.
//
// What it shows:
//   - the descriptor and the record schemas (RAD-12, RAD-20..22);
//   - one simulated device "beacon:0" with three beacons on three
//     channels; the plugin makes its own IQ samples (beacon_gen.c) and
//     measures them inside the plugin (beacon_demod.c): level, SNR,
//     frequency error and the broadcast content of every frame. Only
//     records leave the plugin, never samples (RAD-26);
//   - the common stream control and its state machine (RAD-30..34);
//   - settings as data (four settings and one action), one preset, one
//     sensor and a self-test (RAD-40..46), with a dependency: automatic
//     gain makes the gain setting unavailable (RAD-44);
//   - translations of a few texts (RAD-48).
// It passes the conformance kit (yantra-radio-host conformance, RAD-91)
// and the completeness check (RAD-133) in ctest and CI.
//
// Threads (RAD-15): the control functions run on the host's control
// thread; each stream runs on one thread of the plugin (stream_main) that
// generates 50 ms of samples per beacon, demodulates them and pushes the
// records. Every device field the two threads share is guarded by
// Device::mutex. No C++ exception crosses the C boundary (GUARD).
#include <yantra/radio.h>
#include <yantra/radio.hpp>

extern "C" { // the signal code is plain C
#include "beacon_demod.h"
#include "beacon_gen.h"
#include "beacon_proto.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

// --- the made-up beacon network ------------------------------------------------

// Channel n is at 430 MHz + n * 25 kHz (a made-up band plan).
constexpr uint64_t kBaseHz = 430'000'000;
constexpr uint64_t kRasterHz = 25'000;
constexpr uint32_t kFirstChannel = 1;
constexpr uint32_t kLastChannel = 8;
constexpr uint64_t channel_hz(uint32_t channel) { return kBaseHz + channel * kRasterHz; }

// Every beacon broadcasts its identity and its neighbours (beacon_proto.h).
// The simulated level is relative to the gain setting (level at 20 dB gain).
struct Beacon {
    uint32_t channel;
    uint16_t id;
    uint8_t beacon_class;
    int8_t tx_power_dbm;
    double level_dbfs_at_20db;
    double cfo_hz;
    uint8_t neighbour_count;
    uint16_t neighbours[BEACON_MAX_NEIGHBOURS];
};
constexpr uint8_t kNetworkCode = 7;
constexpr uint32_t kMcc = 901; // shared "international" code: a made-up network
const Beacon kBeacons[] = {
    {1, 101, BEACON_CLASS_INDOOR, 20, -30.0, 120.0, 2, {102, 103, 0}},
    {2, 102, BEACON_CLASS_INDOOR, 17, -42.0, -80.0, 1, {101, 0, 0}},
    {3, 103, BEACON_CLASS_OUTDOOR, 30, -55.0, 0.0, 1, {101, 0, 0}},
};

const Beacon* beacon_on(uint64_t hz)
{
    for (const Beacon& b : kBeacons)
        if (channel_hz(b.channel) == hz)
            return &b;
    return nullptr;
}
const Beacon* beacon_with_id(uint16_t id)
{
    for (const Beacon& b : kBeacons)
        if (b.id == id)
            return &b;
    return nullptr;
}

// --- record schemas (RAD-12, RAD-20..22) ------------------------------------------

// [snippet:schema]
const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                               {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                               {YR_ROLE_OTHER, "other", "Other"}};
const yr_enum_item kClasses[] = {{BEACON_CLASS_INDOOR, "indoor", "Indoor"},
                                 {BEACON_CLASS_OUTDOOR, "outdoor", "Outdoor"},
                                 {BEACON_CLASS_TEST, "test", "Test"}};

constexpr yr_field_def field(uint32_t type, uint32_t group, uint32_t flags, const char* name,
                             const char* label, const char* unit, const char* help,
                             const yr_enum_item* items = nullptr, uint32_t item_count = 0)
{
    return {sizeof(yr_field_def), type, group, flags, name, label, unit, help, items,
            item_count, 0};
}

// The beacon record: the 8 core fields at their fixed indexes (RAD-21),
// then two quality fields and three broadcast parameters (RAD-22).
const yr_field_def kBeaconFields[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, YR_FIELD_FREQUENCY_HZ, "Frequency", "Hz",
          "Centre frequency of the beacon channel."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_DEFAULT, YR_FIELD_CHANNEL, "Channel", "",
          "Beacon channel number: frequency = 430 MHz + channel x 25 kHz."),
    field(YR_TYPE_ENUM, YR_GROUP_CORE, YR_FIELD_DEFAULT, YR_FIELD_ROLE, "Role", "",
          "Serving: the beacon followed; neighbour: announced by it; other: any other.",
          kRoles, 3),
    field(YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, YR_FIELD_LEVEL, "Level", "dBFS",
          "Mean power of the received frame relative to full scale."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, 0, YR_FIELD_MCC, "MCC", "",
          "Country code: always 901 (the beacon network is made up)."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, YR_FIELD_MNC, "Network", "",
          "Network code from payload byte 1 of the frame."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY | YR_FIELD_DEFAULT, YR_FIELD_LA, "Beacon id",
          "", "Beacon id from payload bytes 2-3 of the frame."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, 0, YR_FIELD_COLOUR_CODE, "Colour code", "",
          "Not used by beacons: always 0."),
    field(YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, YR_FIELD_SNR_DB, "SNR", "dB",
          "Frame power over the noise power measured before the frame."),
    field(YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, YR_FIELD_FREQ_ERROR_HZ, "Frequency error", "Hz",
          "Carrier offset measured on the preamble."),
    field(YR_TYPE_ENUM, YR_GROUP_BROADCAST, 0, "beacon_class", "Class", "",
          "Beacon class from payload byte 0 (low nibble).", kClasses, 3),
    field(YR_TYPE_INT, YR_GROUP_BROADCAST, 0, "tx_power_dbm", "TX power", "dBm",
          "Transmit power the beacon announces, payload byte 5."),
    field(YR_TYPE_UINT, YR_GROUP_BROADCAST, 0, "sequence", "Sequence", "",
          "Frame counter of the beacon, payload byte 4 (wraps at 256)."),
};
constexpr uint32_t kBeaconFieldCount = 13;
// Indexes of the optional fields in a beacon record.
constexpr uint32_t kSnr = 8, kFreqError = 9, kClass = 10, kTxPower = 11, kSequence = 12;

// The neighbour list a beacon broadcasts: one record per entry (RAD-22).
const yr_field_def kNeighbourFields[] = {
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, YR_FIELD_FREQUENCY_HZ, "Frequency", "Hz",
          "Channel of the beacon that broadcast the list."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, YR_FIELD_ENTRY_INDEX, "Entry", "",
          "Position in the broadcast neighbour list, from 0."),
    field(YR_TYPE_UINT, YR_GROUP_CORE, 0, YR_FIELD_ENTRY_COUNT, "Entries", "",
          "Number of neighbours in the list, payload byte 6."),
    field(YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, YR_FIELD_DEFAULT, "neighbour_id", "Neighbour id", "",
          "Beacon id of the neighbour, payload bytes 8-13."),
};

template <std::size_t N>
constexpr std::array<const yr_field_def*, N> pointers(const yr_field_def (&fields)[N])
{
    std::array<const yr_field_def*, N> out{};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = &fields[i];
    return out;
}
const auto kBeaconFieldPtrs = pointers(kBeaconFields);
const auto kNeighbourFieldPtrs = pointers(kNeighbourFields);

const yr_schema kBeaconSchema = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "beacon", "Beacon",
                                 1, kBeaconFieldCount, kBeaconFieldPtrs.data(),
                                 YR_GROUP_QUALITY | YR_GROUP_BROADCAST, 0};
const yr_schema kNeighbourSchema = {sizeof(yr_schema), YR_SCHEMA_NEIGHBOUR, "beacon_neighbour",
                                    "Beacon neighbour", 1, 4, kNeighbourFieldPtrs.data(),
                                    YR_GROUP_NEIGHBOURS, 0};
const yr_schema* const kSchemas[] = {&kBeaconSchema, &kNeighbourSchema};
constexpr uint32_t kBeaconSchemaIndex = 0, kNeighbourSchemaIndex = 1;
// [/snippet]

// --- plugin and device state ------------------------------------------------------

struct Plugin {
    const yr_host_api* host = nullptr;
    std::mutex mutex;
    std::string error;      // last plugin-level failure (enumerate, open)
    bool device_open = false;
};

// The settings of the device (RAD-40); defaults are those of list_settings.
struct Settings {
    double gain_db = 20.0;
    bool agc = false;
    double noise_dbfs = -80.0;
    uint64_t period_ms = 250;
};

struct Device {
    Plugin* plugin = nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    uint32_t state = YR_DEVICE_OPEN;
    std::string error;
    Settings settings;
    yr_plan plan{};                 // last plan (carriers_hz copied into `carriers`)
    std::vector<uint64_t> carriers;
    yr_stream* sink = nullptr;      // valid between start_stream and stop_stream
    std::thread thread;
    bool stop = false;
    std::atomic<uint64_t> delivered{0};
    std::atomic<uint64_t> frames_decoded{0}; // the sensor (RAD-45)
};

Plugin* P(yr_plugin* p) { return reinterpret_cast<Plugin*>(p); }
Device* D(yr_device* d) { return reinterpret_cast<Device*>(d); }

void set_device_error(Device* d, std::string text)
{
    std::lock_guard lock(d->mutex);
    d->error = std::move(text);
}
void set_plugin_error(Plugin* p, std::string text)
{
    std::lock_guard lock(p->mutex);
    p->error = std::move(text);
}

// Every function the host calls returns a status and never lets a C++
// exception escape (RAD-10).
#define GUARD(...)                                                                            \
    try {                                                                                     \
        __VA_ARGS__                                                                           \
    } catch (const std::bad_alloc&) {                                                         \
        return YR_ERR_NO_MEMORY;                                                              \
    } catch (...) {                                                                           \
        return YR_ERR_FAILED;                                                                 \
    }

// --- settings as data (RAD-40..44) ------------------------------------------------

// [snippet:settings]
// The gain the receiver really uses: the setting, or 30 dB with AGC.
double effective_gain(const Settings& s) { return s.agc ? 30.0 : s.gain_db; }

yr_setting_def setting(const char* key, uint32_t type, const char* label, const char* unit,
                       const char* group, const char* help, int32_t order)
{
    yr_setting_def s{};
    s.struct_size = sizeof s;
    s.type = type;
    s.key = key;
    s.label = label;
    s.unit = unit;
    s.group = group;
    s.help = help;
    s.minimum = NAN;
    s.maximum = NAN;
    s.sort_order = order;
    return s;
}

// Built from the current state, so availability follows the AGC (RAD-44).
std::vector<yr_setting_def> settings_of(const Settings& now)
{
    std::vector<yr_setting_def> out;
    yr_setting_def gain = setting("gain_db", YR_TYPE_DOUBLE, "Gain", "dB", "Gain",
                                  "Receiver gain in 1 dB steps; the level follows it.", 1);
    gain.flags = YR_SETTING_CALIBRATION;
    gain.minimum = 0.0;
    gain.maximum = 40.0;
    gain.step = 1.0;
    gain.default_value = yr_value_double(20.0);
    if (now.agc) {
        gain.flags |= YR_SETTING_UNAVAILABLE;
        gain.unavailable_reason = "automatic gain control sets the gain";
    }
    out.push_back(gain);

    yr_setting_def agc = setting("agc", YR_TYPE_BOOL, "Automatic gain", "", "Gain",
                                 "Lets the receiver choose the gain (30 dB).", 2);
    agc.flags = YR_SETTING_CALIBRATION;
    agc.default_value = yr_value_bool(0);
    out.push_back(agc);

    yr_setting_def noise = setting("noise_dbfs", YR_TYPE_DOUBLE, "Noise level", "dBFS",
                                   "Scenario", "Noise power the generator adds.", 3);
    noise.minimum = -120.0;
    noise.maximum = -20.0;
    noise.step = 1.0;
    noise.default_value = yr_value_double(-80.0);
    out.push_back(noise);

    yr_setting_def period = setting("period_ms", YR_TYPE_UINT, "Frame period", "ms", "Scenario",
                                    "Time between two frames of a beacon.", 4);
    period.flags = YR_SETTING_RESTART | YR_SETTING_ADVANCED;
    period.minimum = 150.0;
    period.maximum = 1000.0;
    period.step = 50.0;
    period.default_value = yr_value_uint(250);
    out.push_back(period);

    out.push_back(setting("reset_counters", YR_TYPE_ACTION, "Reset counters", "", "Receiver",
                          "Sets the decoded-frame counter back to 0.", 5));
    return out;
}
// [/snippet]

const yr_setting_def* find_setting(const std::vector<yr_setting_def>& defs, const char* key)
{
    for (const yr_setting_def& s : defs)
        if (key != nullptr && std::strcmp(s.key, key) == 0)
            return &s;
    return nullptr;
}

// --- the stream thread: samples in, records out -----------------------------------

// One measured channel: its generator (the "antenna") and demodulator.
struct Channel {
    uint64_t hz = 0;
    uint32_t channel = 0;
    uint32_t role = YR_ROLE_OTHER;
    const Beacon* beacon = nullptr; // nullptr: nobody transmits here
    beacon_gen* gen = nullptr;
    beacon_demod demod{};
    double window_power = 0.0;      // for NO_SYNC records without frames
    uint64_t window_samples = 0;
    int64_t last_record_ns = 0;

    ~Channel()
    {
        beacon_gen_destroy(gen);
        beacon_demod_free(&demod);
    }
};

// Builds the channels of a plan with the given settings. SERVING follows
// the given carrier or the strongest beacon; its broadcast neighbours are
// measured too with MEASURE_NEIGHBOURS. CARRIERS measures the list.
std::vector<std::unique_ptr<Channel>> make_channels(const yr_plan& plan,
                                                    const std::vector<uint64_t>& carriers,
                                                    const Settings& s)
{
    std::vector<std::pair<uint64_t, uint32_t>> wanted; // frequency, role
    if (plan.type == YR_PLAN_CARRIERS) {
        for (uint64_t hz : carriers)
            wanted.emplace_back(hz, YR_ROLE_OTHER);
    } else {
        const uint64_t serving = plan.serving_hz != 0 ? plan.serving_hz
                                                      : channel_hz(kBeacons[0].channel);
        wanted.emplace_back(serving, YR_ROLE_SERVING);
        const Beacon* b = beacon_on(serving);
        if (b != nullptr && (plan.flags & YR_PLAN_MEASURE_NEIGHBOURS) != 0)
            for (uint8_t i = 0; i < b->neighbour_count; ++i)
                if (const Beacon* n = beacon_with_id(b->neighbours[i]))
                    wanted.emplace_back(channel_hz(n->channel), YR_ROLE_NEIGHBOUR);
    }
    std::vector<std::unique_ptr<Channel>> out;
    for (const auto& [hz, role] : wanted) {
        auto c = std::make_unique<Channel>();
        c->hz = hz;
        c->channel = static_cast<uint32_t>((hz - kBaseHz) / kRasterHz);
        c->role = role;
        c->beacon = beacon_on(hz);
        beacon_gen_config cfg{};
        beacon_gen_parse(nullptr, &cfg, nullptr, 0); // the defaults
        cfg.seconds = 1e9;                           // endless
        cfg.period_ms = static_cast<double>(s.period_ms);
        cfg.noise_dbfs = s.noise_dbfs;
        cfg.seed = hz;
        if (c->beacon != nullptr) {
            const Beacon& b = *c->beacon;
            cfg.payload.network_code = kNetworkCode;
            cfg.payload.beacon_id = b.id;
            cfg.payload.beacon_class = b.beacon_class;
            cfg.payload.tx_power_dbm = b.tx_power_dbm;
            cfg.payload.neighbour_count = b.neighbour_count;
            std::copy(std::begin(b.neighbours), std::end(b.neighbours), cfg.payload.neighbours);
            cfg.level_dbfs = std::min(b.level_dbfs_at_20db + effective_gain(s) - 20.0, 3.0);
            cfg.cfo_hz = b.cfo_hz;
        } else {
            cfg.level_dbfs = -200.0; // silence: noise only
        }
        c->gen = beacon_gen_create(&cfg, BEACON_SAMPLE_RATE_HZ, nullptr, 0);
        if (c->gen == nullptr)
            throw std::bad_alloc();
        beacon_demod_init(&c->demod, BEACON_SAMPLE_RATE_HZ);
        out.push_back(std::move(c));
    }
    return out;
}

double to_db(double power) { return 10.0 * std::log10(std::max(power, 1e-20)); }

// [snippet:record]
// Pushes the record of one frame (or, without a frame, of the channel's
// noise) and, for a serving beacon, its neighbour list (RAD-20..22).
void push_frame(Device* d, const Channel& c, const beacon_frame_result* frame, double level_dbfs)
{
    const yr_host_api* h = d->plugin->host;
    const bool decoded = frame != nullptr && frame->crc_ok != 0;

    yr_value v[kBeaconFieldCount];
    for (yr_value& x : v)
        x = yr_value_none();
    v[YR_CORE_FREQUENCY_HZ] = yr_value_uint(c.hz);
    v[YR_CORE_CHANNEL] = yr_value_uint(c.channel);
    v[YR_CORE_ROLE] = yr_value_enum(c.role);
    v[YR_CORE_LEVEL] = yr_value_double(level_dbfs);

    yr_record r{};
    r.struct_size = sizeof r;
    r.schema_index = kBeaconSchemaIndex;
    r.time.monotonic_ns = h->monotonic_ns(h->host);
    r.time.utc_ns = h->utc_now_ns(h->host);
    r.time.utc_source = YR_TIME_SOURCE_SYSTEM;
    r.flags = YR_RECORD_SIMULATED; // our samples are generated (YR_CAP_SIMULATED)
    if (level_dbfs > -1.0)
        r.flags |= YR_RECORD_OVERLOAD;
    if (decoded) {
        const beacon_payload& p = frame->payload;
        v[YR_CORE_MCC] = yr_value_uint(kMcc);
        v[YR_CORE_MNC] = yr_value_uint(p.network_code);
        v[YR_CORE_LA] = yr_value_uint(p.beacon_id);
        v[YR_CORE_COLOUR_CODE] = yr_value_uint(0);
        v[kClass] = yr_value_enum(p.beacon_class);
        v[kTxPower] = yr_value_int(p.tx_power_dbm);
        v[kSequence] = yr_value_uint(p.sequence);
        r.present |= YR_GROUP_BROADCAST;
    } else {
        r.flags |= YR_RECORD_NO_SYNC; // level only, no identity
    }
    if (frame != nullptr) {
        v[kSnr] = frame->has_snr != 0 ? yr_value_double(frame->snr_db) : yr_value_none();
        v[kFreqError] = yr_value_double(frame->freq_offset_hz);
        r.present |= YR_GROUP_QUALITY;
    }
    r.value_count = kBeaconFieldCount;
    r.values = v;
    // The sink never blocks: when the host is busy it drops and counts the
    // record and still returns OK, and we carry on (RAD-24).
    if (h->push_record(d->sink, &r) == YR_OK)
        ++d->delivered;

    if (!decoded || c.role != YR_ROLE_SERVING)
        return;
    const beacon_payload& p = frame->payload;
    for (uint8_t i = 0; i < p.neighbour_count; ++i) {
        yr_value n[4] = {yr_value_uint(c.hz), yr_value_uint(i), yr_value_uint(p.neighbour_count),
                         yr_value_uint(p.neighbours[i])};
        yr_record nr = r;
        nr.schema_index = kNeighbourSchemaIndex;
        nr.flags = YR_RECORD_SIMULATED;
        nr.present = YR_GROUP_NEIGHBOURS;
        nr.value_count = 4;
        nr.values = n;
        if (h->push_record(d->sink, &nr) == YR_OK)
            ++d->delivered;
    }
}
// [/snippet]

// [snippet:stream_main]
void stream_main(Device* d)
{
    const yr_host_api* h = d->plugin->host;
    constexpr uint64_t kChunk = 1200; // 50 ms at 24 kHz
    std::vector<float> iq(2 * kChunk);
    std::vector<std::unique_ptr<Channel>> channels;
    Settings used{};
    bool first = true;
    auto next = std::chrono::steady_clock::now();

    std::unique_lock lock(d->mutex);
    while (!d->stop) {
        // Rebuild the channels when a setting changed (a new "front end").
        if (first || d->settings.gain_db != used.gain_db || d->settings.agc != used.agc ||
            d->settings.noise_dbfs != used.noise_dbfs) {
            used = d->settings;
            channels = make_channels(d->plan, d->carriers, used);
        }
        lock.unlock();

        // 50 ms of samples per channel, measured here; records go out.
        for (auto& cp : channels) {
            Channel& c = *cp;
            beacon_gen_read(c.gen, iq.data(), kChunk);
            for (uint64_t i = 0; i < kChunk; ++i)
                c.window_power += iq[2 * i] * iq[2 * i] + iq[2 * i + 1] * iq[2 * i + 1];
            c.window_samples += kChunk;
            if (beacon_demod_push(&c.demod, iq.data(), kChunk) != 0)
                throw std::bad_alloc();
            beacon_frame_result frame{};
            while (beacon_demod_next(&c.demod, &frame) == 1) {
                if (frame.crc_ok != 0)
                    ++d->frames_decoded;
                push_frame(d, c, &frame, frame.level_dbfs);
                c.window_power = 0.0;
                c.window_samples = 0;
                c.last_record_ns = h->monotonic_ns(h->host);
            }
            // Nothing decoded for 0.5 s: a level-only record (at least one
            // record per second and carrier, RAD-21).
            if (c.window_samples >= 12000) {
                push_frame(d, c, nullptr, to_db(c.window_power / c.window_samples));
                c.window_power = 0.0;
                c.window_samples = 0;
            }
        }
        if (first) {
            h->stream_event(d->sink, YR_EVENT_SYNC_FOUND, static_cast<int64_t>(channels[0]->hz));
            first = false;
        }

        // Real time: wait for the next 50 ms, or until stop_stream.
        next += std::chrono::milliseconds(50);
        lock.lock();
        d->wake.wait_until(lock, next, [d] { return d->stop; });
    }
}
// [/snippet]

void stream_thread(Device* d)
{
    try {
        stream_main(d);
    } catch (...) {
        // Report the failure and stop streaming (RAD-25); the host reads
        // last_error.
        std::lock_guard lock(d->mutex);
        d->error = "the beacon receiver ran out of memory; close and open the device again";
        d->state = YR_DEVICE_OPEN;
        d->plugin->host->stream_event(d->sink, YR_EVENT_ERROR, 0);
    }
}

// --- plugin and stream control (RAD-30..34) -------------------------------------

yr_status beacon_init(const yr_host_api* host, yr_plugin** out)
{
    GUARD({
        if (host == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        auto* p = new Plugin();
        p->host = host;
        *out = reinterpret_cast<yr_plugin*>(p);
        return YR_OK;
    })
}

void beacon_shutdown(yr_plugin* plugin) { delete P(plugin); }

// [snippet:enumerate]
const yr_device_info kDeviceInfo = {sizeof(yr_device_info), YR_DEVICE_SIMULATED, "beacon:0",
                                    "Beacon generator", "beacon", "0", "1.0",
                                    "channels=1-3\nsample_rate_hz=24000", ""};

yr_status beacon_enumerate(yr_plugin*, yr_device_cb cb, void* user)
{
    cb(user, &kDeviceInfo); // always present: the device is simulated
    return YR_OK;
}

yr_status beacon_open(yr_plugin* plugin, const char* key, yr_device** out)
{
    GUARD({
        Plugin* p = P(plugin);
        if (key == nullptr || out == nullptr || std::strcmp(key, kDeviceInfo.key) != 0) {
            set_plugin_error(p, "no beacon device with this key; use beacon:0");
            return YR_ERR_NOT_FOUND;
        }
        std::lock_guard lock(p->mutex);
        if (p->device_open) {
            p->error = "beacon:0 is already open; close it first";
            return YR_ERR_BUSY;
        }
        auto* d = new Device();
        d->plugin = p;
        d->plan.struct_size = sizeof d->plan;
        d->plan.type = YR_PLAN_SERVING;
        p->device_open = true;
        p->error.clear();
        *out = reinterpret_cast<yr_device*>(d);
        return YR_OK;
    })
}
// [/snippet]

// [snippet:start_stop]
yr_status beacon_start(yr_device* device, const yr_plan* plan, yr_stream* sink)
{
    GUARD({
        Device* d = D(device);
        if (plan == nullptr || sink == nullptr ||
            !YR_HAS_FIELD(plan, yr_plan, scan_step_hz)) {
            set_device_error(d, "start_stream needs a plan and a sink");
            return YR_ERR_INVALID_ARGUMENT;
        }
        if (plan->type == YR_PLAN_SCAN) {
            set_device_error(d, "the beacon receiver cannot scan; use a carrier list");
            return YR_ERR_UNSUPPORTED;
        }
        if (plan->type != YR_PLAN_SERVING && plan->type != YR_PLAN_CARRIERS) {
            set_device_error(d, "unknown plan type");
            return YR_ERR_INVALID_ARGUMENT;
        }
        if (plan->type == YR_PLAN_CARRIERS &&
            (plan->carrier_count == 0 || plan->carriers_hz == nullptr ||
             plan->carrier_count > kLastChannel)) {
            set_device_error(d, "a carrier list needs 1 to 8 beacon channels");
            return YR_ERR_INVALID_ARGUMENT;
        }
        std::lock_guard lock(d->mutex);
        if (d->state != YR_DEVICE_OPEN) // already streaming (RAD-34)
            return YR_ERR_BUSY;
        if (d->thread.joinable())
            d->thread.join(); // a stream that stopped on an error
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr; // valid during the call only (RAD-10)
        d->sink = sink;
        d->stop = false;
        d->delivered = 0;
        d->state = YR_DEVICE_STREAMING;
        d->thread = std::thread(stream_thread, d);
        return YR_OK;
    })
}

yr_status beacon_stop(yr_device* device)
{
    Device* d = D(device);
    {
        std::lock_guard lock(d->mutex);
        d->stop = true;
    }
    d->wake.notify_all();
    // After the join no push can happen any more (RAD-33).
    if (d->thread.joinable())
        d->thread.join();
    std::lock_guard lock(d->mutex);
    d->state = YR_DEVICE_OPEN;
    d->sink = nullptr;
    return YR_OK; // a second stop is harmless
}
// [/snippet]

void beacon_close(yr_device* device)
{
    Device* d = D(device);
    beacon_stop(device); // close stops a running stream first (RAD-31)
    {
        std::lock_guard lock(d->plugin->mutex);
        d->plugin->device_open = false;
    }
    delete d;
}

yr_status beacon_device_info(yr_device*, yr_device_cb cb, void* user)
{
    cb(user, &kDeviceInfo);
    return YR_OK;
}

uint32_t beacon_state(yr_device* device)
{
    std::lock_guard lock(D(device)->mutex);
    return D(device)->state;
}

yr_status beacon_plan_ranges(yr_device*, yr_plan_ranges_cb cb, void* user)
{
    static const yr_freq_range ranges[] = {{channel_hz(kFirstChannel), channel_hz(kLastChannel)}};
    const yr_plan_ranges r = {sizeof r, (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS), 1,
                              kLastChannel, ranges, 200'000.0};
    cb(user, &r);
    return YR_OK;
}

yr_status beacon_set_plan(yr_device* device, const yr_plan* plan)
{
    GUARD({
        Device* d = D(device);
        if (plan == nullptr ||
            (plan->type == YR_PLAN_CARRIERS && plan->carrier_count > 0 &&
             plan->carriers_hz == nullptr))
            return YR_ERR_INVALID_ARGUMENT;
        std::lock_guard lock(d->mutex);
        if (d->state == YR_DEVICE_STREAMING) { // no RETUNE_LIVE (RAD-32)
            d->error = "stop the stream to change the plan";
            return YR_ERR_BUSY;
        }
        if (plan->type != YR_PLAN_SERVING && plan->type != YR_PLAN_CARRIERS) {
            d->error = "the beacon receiver measures SERVING or CARRIERS plans only";
            return YR_ERR_UNSUPPORTED;
        }
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr;
        return YR_OK;
    })
}

yr_status beacon_stats(yr_device* device, yr_stream_stats* out)
{
    if (out == nullptr || !YR_HAS_FIELD(out, yr_stream_stats, sample_rate_hz))
        return YR_ERR_INVALID_ARGUMENT;
    out->records_delivered = D(device)->delivered.load();
    out->records_dropped = 0;
    out->samples_dropped = 0;
    out->overflows = 0;
    out->receiver_load = NAN;
    out->sample_rate_hz = BEACON_SAMPLE_RATE_HZ;
    return YR_OK;
}

yr_status beacon_last_error(yr_plugin* plugin, yr_device* device, char* buffer, uint32_t size)
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

// --- device control (RAD-40..46) --------------------------------------------------

yr_status beacon_list_settings(yr_device* device, yr_setting_cb cb, void* user)
{
    GUARD({
        Device* d = D(device);
        std::vector<yr_setting_def> defs;
        {
            std::lock_guard lock(d->mutex);
            defs = settings_of(d->settings);
        }
        for (const yr_setting_def& s : defs)
            cb(user, &s);
        return YR_OK;
    })
}

yr_status beacon_get(yr_device* device, const char* key, yr_value* out, char*, uint32_t)
{
    Device* d = D(device);
    if (key == nullptr || out == nullptr)
        return YR_ERR_INVALID_ARGUMENT;
    std::lock_guard lock(d->mutex);
    const Settings& s = d->settings;
    if (std::strcmp(key, "gain_db") == 0)
        *out = yr_value_double(effective_gain(s)); // the value really used (RAD-41)
    else if (std::strcmp(key, "agc") == 0)
        *out = yr_value_bool(s.agc ? 1 : 0);
    else if (std::strcmp(key, "noise_dbfs") == 0)
        *out = yr_value_double(s.noise_dbfs);
    else if (std::strcmp(key, "period_ms") == 0)
        *out = yr_value_uint(s.period_ms);
    else {
        d->error = std::string("unknown setting ") + key;
        return YR_ERR_NOT_FOUND;
    }
    return YR_OK;
}

// [snippet:set_setting]
yr_status beacon_set(yr_device* device, const char* key, const yr_value* value)
{
    GUARD({
        Device* d = D(device);
        std::unique_lock lock(d->mutex);
        const std::vector<yr_setting_def> defs = settings_of(d->settings);
        const yr_setting_def* def = find_setting(defs, key);
        if (def == nullptr || value == nullptr) {
            d->error = std::string("unknown setting ") + (key != nullptr ? key : "(null)");
            return YR_ERR_NOT_FOUND;
        }
        // Type, range, read-only and availability in one helper (RAD-41).
        std::string why;
        if (yantra::radio::check_setting_value(*def, *value, &why) != YR_OK) {
            d->error = why;
            return YR_ERR_INVALID_ARGUMENT;
        }
        if ((def->flags & YR_SETTING_RESTART) != 0 && d->state == YR_DEVICE_STREAMING) {
            d->error = std::string(def->label) + " changes only while stopped";
            return YR_ERR_BUSY;
        }
        Settings& s = d->settings;
        bool others_changed = false;
        if (std::strcmp(key, "gain_db") == 0) {
            s.gain_db = std::round(value->u.f64); // the device's 1 dB steps
        } else if (std::strcmp(key, "agc") == 0) {
            others_changed = s.agc != (value->u.i64 != 0); // gain availability
            s.agc = value->u.i64 != 0;
        } else if (std::strcmp(key, "noise_dbfs") == 0) {
            s.noise_dbfs = std::round(value->u.f64);
        } else if (std::strcmp(key, "period_ms") == 0) {
            s.period_ms = (value->u.u64 + 25) / 50 * 50;
        }
        lock.unlock();
        // Tell the host to list and read the settings again (RAD-44).
        if (others_changed)
            d->plugin->host->settings_changed(d->plugin->host->host, device);
        return YR_OK;
    })
}
// [/snippet]

yr_status beacon_action(yr_device* device, const char* key, char* result, uint32_t size)
{
    Device* d = D(device);
    if (key == nullptr || std::strcmp(key, "reset_counters") != 0) {
        set_device_error(d, "unknown action");
        return YR_ERR_NOT_FOUND;
    }
    d->frames_decoded = 0;
    return yantra::radio::copy_text("decoded-frame counter set to 0", result, size);
}

// [snippet:preset_sensor]
const yr_preset kQuietRoom = {sizeof(yr_preset), 0, "quiet_room", "Quiet room",
                              "Gain 30 dB without automatic gain, noise -100 dBFS."};

yr_status beacon_list_presets(yr_device*, yr_preset_cb cb, void* user)
{
    cb(user, &kQuietRoom);
    return YR_OK;
}

yr_status beacon_apply_preset(yr_device* device, const char* id)
{
    Device* d = D(device);
    if (id == nullptr || std::strcmp(id, kQuietRoom.id) != 0) {
        set_device_error(d, "unknown preset");
        return YR_ERR_NOT_FOUND;
    }
    {
        std::lock_guard lock(d->mutex);
        d->settings.gain_db = 30.0;
        d->settings.agc = false;
        d->settings.noise_dbfs = -100.0;
    }
    d->plugin->host->settings_changed(d->plugin->host->host, device);
    return YR_OK;
}

yr_status beacon_list_sensors(yr_device*, yr_setting_cb cb, void* user)
{
    yr_setting_def s = setting("frames_decoded", YR_TYPE_UINT, "Decoded frames", "frames",
                               "Receiver", "Frames with a valid CRC since the device was opened.",
                               1);
    s.flags = YR_SETTING_READ_ONLY;
    cb(user, &s);
    return YR_OK;
}

yr_status beacon_read_sensor(yr_device* device, const char* key, yr_value* out, char*, uint32_t)
{
    if (key == nullptr || out == nullptr || std::strcmp(key, "frames_decoded") != 0) {
        set_device_error(D(device), "unknown sensor");
        return YR_ERR_NOT_FOUND;
    }
    *out = yr_value_uint(D(device)->frames_decoded.load());
    return YR_OK;
}
// [/snippet]

// Self-test (RAD-46): one frame through the generator and the demodulator.
yr_status beacon_self_test(yr_device* device, yr_selftest_cb cb, void* user)
{
    GUARD({
        if (beacon_state(device) == YR_DEVICE_STREAMING)
            return YR_ERR_BUSY;
        beacon_gen_config cfg{};
        beacon_gen_parse("beacon=4242 seconds=0.3 noise_dbfs=-60", &cfg, nullptr, 0);
        beacon_gen* gen = beacon_gen_create(&cfg, BEACON_SAMPLE_RATE_HZ, nullptr, 0);
        if (gen == nullptr)
            return YR_ERR_NO_MEMORY;
        std::vector<float> iq(2 * 7200);
        const uint64_t n = beacon_gen_read(gen, iq.data(), 7200);
        beacon_gen_destroy(gen);
        beacon_demod demod;
        beacon_demod_init(&demod, BEACON_SAMPLE_RATE_HZ);
        beacon_frame_result frame{};
        const bool ok = beacon_demod_push(&demod, iq.data(), n) == 0 &&
                        beacon_demod_next(&demod, &frame) == 1 && frame.crc_ok != 0 &&
                        frame.payload.beacon_id == 4242;
        beacon_demod_free(&demod);
        const yr_selftest_item item = {
            sizeof(yr_selftest_item), ok ? YR_TEST_PASS : YR_TEST_FAIL, "loopback",
            "Decode of a known frame", ok ? "beacon 4242 decoded" : "the known frame was not decoded",
            ok ? "" : "report this as a bug of the beacon plugin"};
        cb(user, &item);
        return YR_OK;
    })
}

// --- translations (RAD-48) and the descriptor (RAD-12) ---------------------------

const yr_translation kTranslations[] = {
    {"setting.gain_db.label", "nl", "Versterking"},
    {"setting.gain_db.label", "fr", "Gain"},
    {"setting.gain_db.label", "de", "Verstärkung"},
    {"setting.agc.label", "nl", "Automatische versterking"},
    {"setting.agc.label", "fr", "Gain automatique"},
    {"setting.agc.label", "de", "Automatische Verstärkung"},
    {"preset.quiet_room.label", "nl", "Stille ruimte"},
    {"preset.quiet_room.label", "fr", "Pièce calme"},
    {"preset.quiet_room.label", "de", "Ruhiger Raum"},
    {"sensor.frames_decoded.label", "nl", "Gedecodeerde frames"},
    {"sensor.frames_decoded.label", "fr", "Trames décodées"},
    {"sensor.frames_decoded.label", "de", "Dekodierte Frames"},
};

const yr_stream_control kStreamControl = {
    sizeof(yr_stream_control), 0,          beacon_enumerate, beacon_open,
    beacon_close,              beacon_device_info, beacon_state, beacon_plan_ranges,
    beacon_set_plan,           beacon_start, beacon_stop,  beacon_stats,
    beacon_last_error};

const yr_device_control kDeviceControl = {
    sizeof(yr_device_control), 0, beacon_list_settings, beacon_get, beacon_set, beacon_action,
    beacon_list_presets, beacon_apply_preset, beacon_list_sensors, beacon_read_sensor,
    beacon_self_test};

} // namespace

// [snippet:entry]
extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const yr_descriptor descriptor = {
        sizeof(yr_descriptor),
        yr_api_version_compiled(), // refused by hosts of another major version (RAD-11)
        YR_CAP_SIMULATED,          // generated samples, never a real measurement
        "beacon",
        "Beacon example",
        "0.1.1",
        "The Yantra contributors",
        "GPL-3.0-or-later",
        "Example radio plugin for the made-up beacon technology",
        YANTRA_SOURCE_URL,
        "beacon",
        2,
        sizeof kTranslations / sizeof kTranslations[0],
        kSchemas,
        kTranslations,
        beacon_init,
        beacon_shutdown,
        &kStreamControl,
        &kDeviceControl,
        nullptr, // on_context_event: no YR_CAP_SCENE
    };
    return &descriptor;
}
// [/snippet]
