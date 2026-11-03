<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Writing a radio plugin

This guide explains the radio API (RAD-10..49) and builds the example
plugin `beacon` (RAD-64, `plugins/beacon/`) step by step. Read it with
`include/yantra/radio.h` (the reference: `ninja -C build docs-radio-api`
writes it to `build/docs/radio-api/html`) and the plugin source next to
you. The code blocks below are the compiled code of the beacon plugin:
ctest (`doc_snippets`) fails when they differ.

Contents: [the radio boundary](#the-radio-boundary) -
[the API at a glance](#the-api-at-a-glance) -
[the beacon plugin step by step](#the-beacon-plugin-step-by-step) -
[settings as data](#settings-as-data-how-yantra-builds-the-windows) -
[record schemas](#record-schemas) -
[context callbacks](#context-callbacks-scene-plugins) -
[the conformance kit](#the-conformance-kit).

## The radio boundary

A radio plugin is a shared library that drives one kind of receiver,
processes its IQ samples itself and hands Yantra **measurement records**
only: level, cell identity, optional quality, C1, broadcast parameters
and neighbours. No IQ samples, PDUs, slots or bursts cross the API
(RAD-26), and nothing outside broadcast and system information: no
addresses, short data, voice or keys (RAD-17). Yantra has no DSP or
decoder code; it shows, stores and analyses records.

A plugin has no UI code either. It describes its device settings,
presets, sensors and self-test as data, and Yantra builds the windows from
that data (RAD-49).

## The API at a glance

| Part | What | Requirements |
|---|---|---|
| `yantra_radio_entry()` | The one exported symbol; returns the descriptor | RAD-12 |
| `yr_descriptor` | Version, id, name, licence, capabilities, technology id, schemas, translations, `init`/`shutdown`, the two function tables | RAD-11..13, RAD-48 |
| `yr_host_api` | What Yantra gives the plugin at `init`: log, clocks, the record sink, events, change notices, and context callbacks for SCENE plugins | RAD-14, RAD-18, RAD-20, RAD-24, RAD-25 |
| `yr_stream_control` | The same for every plugin: enumerate, open, close, device info, state, plan ranges, set plan, start, stop, statistics, last error | RAD-30..35 |
| `yr_device_control` | Device-specific control as data: settings, actions, presets, sensors, self-test | RAD-40..47 |
| `yr_schema` | Record schemas: fields with name, type, unit, label, help, group | RAD-20..22 |

Rules that hold everywhere (the main page of the radio.h reference):

- **Versioned structs.** Every struct that may grow starts with
  `struct_size`; a reader uses only the members that fit
  (`YR_HAS_FIELD`). New members are only added at the end (RAD-10).
- **Ownership.** Pointers handed to the plugin are valid during the call
  only; Yantra copies what it keeps (records, setting definitions,
  texts). The plugin's descriptor and schemas live as long as the library.
- **Errors.** Every function returns a `yr_status`; after an error,
  `last_error(plugin, device)` gives a one-line English text (RAD-16). No
  C++ exception and no `longjmp` crosses the C boundary.
- **Threads** (RAD-15). Control functions are called from one control
  thread at a time per device. Records and events may be pushed from any
  plugin thread. `push_record` never blocks: when Yantra cannot keep up it
  drops and counts the record (RAD-24). After `stop_stream` returns no
  push and no event of that stream may follow (RAD-33). `open`,
  `run_action` and `self_test` take at most 10 s; every other control
  call returns within 1 s.
- **Units.** Frequencies in Hz (uint64), levels in dBFS (or dBm with
  `YR_CAP_SELF_CALIBRATED`), times in ns: `monotonic_ns` from the host's
  monotonic clock, `utc_ns` with its source (RAD-27).
- **State machine** (RAD-34): a device is CLOSED, OPEN, STREAMING or
  LOST. A call in the wrong state returns `YR_ERR_BUSY` (for example a
  second `start_stream`, a self-test while streaming, or a RESTART setting
  while streaming); on a LOST device calls return `YR_ERR_DEVICE_LOST`.

## The beacon plugin step by step

The made-up beacon technology (`beacon_proto.h`) is a 2-FSK burst on a
25 kHz channel raster: preamble, sync word and a payload with the beacon's
class, network code, id, frame counter, transmit power and up to three
neighbour ids, protected by a CRC. The plugin simulates one device,
`beacon:0`, that receives three beacons on channels 1 to 3. It makes its
own IQ samples (`beacon_gen.c`) and measures them like a real receiver
would (`beacon_demod.c`): level, SNR, frequency error and the payload.
Only the resulting records leave the plugin.

Build and try it:

```sh
cmake -S . -B build -G Ninja && ninja -C build
build/bin/yantra-radio-host info --plugin build/bin/plugins/beacon.so
build/bin/yantra-radio-host settings --plugin build/bin/plugins/beacon.so --set agc=true
build/bin/yantra-radio-host stream --plugin build/bin/plugins/beacon.so --neighbours --duration 3
build/bin/yantra-radio-host conformance --plugin build/bin/plugins/beacon.so
```

### Step 1: the record schemas

A schema describes one kind of record. A MEASUREMENT schema starts with
the eight core fields at fixed indexes (`YR_CORE_*`: frequency, channel,
role, level, MCC, MNC, LA, colour code; RAD-21), then optional fields,
each in one group (`YR_GROUP_QUALITY`, `YR_GROUP_BROADCAST`, ...). A
record says which optional groups it carries (`present`); the fields of a
group it does not carry are absent. Every field has a label and a help
text: Yantra shows them as column titles and tooltips (Yantra DOC-07).
The neighbour list of a beacon goes into a second schema of kind
NEIGHBOUR, one record per list entry.

<!-- snippet: plugins/beacon/beacon_plugin.cpp#schema -->
```cpp
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
```

Common field names are defined in radio.h (`YR_FIELD_SNR_DB`,
`YR_FIELD_FREQ_ERROR_HZ`, ...): use them where the meaning is the same,
so Yantra shows the same column for every technology. Names must not
suggest content outside the scope (the kit checks them against the
RAD-17 list).

### Step 2: the descriptor and the entry point

The plugin exports exactly one symbol. The descriptor carries the radio
API version the plugin was compiled against: Yantra loads it only with
the same major version and a minor version not newer than its own
(RAD-11). The id is stable (`[a-z0-9][a-z0-9._-]*`), the licence an SPDX
expression compatible with GPL-3.0 (Yantra PLG-10), and the capability
bits tell Yantra what to expect: the beacon has only `YR_CAP_SIMULATED`,
so every record carries `YR_RECORD_SIMULATED` and Yantra marks it as
simulated (Yantra DEMO-07).

<!-- snippet: plugins/beacon/beacon_plugin.cpp#entry -->
```cpp
extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const yr_descriptor descriptor = {
        sizeof(yr_descriptor),
        yr_api_version_compiled(), // refused by hosts of another major version (RAD-11)
        YR_CAP_SIMULATED,          // generated samples, never a real measurement
        "beacon",
        "Beacon example",
        "0.1.0",
        "The Yantra contributors",
        "GPL-3.0-or-later",
        "Example radio plugin for the made-up beacon technology (RAD-64)",
        YANTRA_RADIOS_SOURCE_URL,
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
```

`init` gets the host table and returns the plugin's state; `shutdown`
frees it after every device is closed.

### Step 3: devices

`enumerate` reports every device; with none it returns `YR_ERR_NO_DEVICE`
and puts the reason in `last_error(plugin, NULL)` ("no HackRF found: is
the udev rule installed?"), which Yantra shows (RAD-30, RAD-52). Keys are
stable across runs (a serial number, not a USB port). `open` takes the
key and returns the device in state OPEN with its default settings: the
plugin keeps no settings of its own; Yantra stores and applies them
(RAD-47).

<!-- snippet: plugins/beacon/beacon_plugin.cpp#enumerate -->
```cpp
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
```

### Step 4: starting and stopping a stream

`start_stream` gets a plan (RAD-32: follow the serving carrier, measure a
list of carriers, or scan a band; optionally also the announced
neighbours) and the record sink. It starts the plugin's stream thread and
returns at once. `stop_stream` makes the thread finish, **joins it**, and
only then returns: from that moment no push may come (RAD-33). `close`
stops a running stream first. A plugin that cannot change its plan while
streaming returns `YR_ERR_BUSY` from `set_plan`; one with
`YR_CAP_RETUNE_LIVE` applies it and reports `YR_EVENT_PLAN_APPLIED`.

<!-- snippet: plugins/beacon/beacon_plugin.cpp#start_stop -->
```cpp
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
        d->plan = *plan;
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
```

### Step 5: the stream thread

The stream thread does the receiver's work. The beacon thread generates
50 ms of samples per channel, demodulates them and pushes the records,
then waits for the next 50 ms (or for `stop`). It never waits for Yantra:
`push_record` returns at once, also when Yantra drops the record (RAD-24).
It reports events such as `YR_EVENT_SYNC_FOUND` (RAD-25), and gives a
level-only record with `YR_RECORD_NO_SYNC` when nothing is decoded, so a
carrier is reported at least once per second while it is measured
(RAD-21).

<!-- snippet: plugins/beacon/beacon_plugin.cpp#stream_main -->
```cpp
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
```

### Step 6: building a record

A record is the schema index, the time, flags, the optional groups
present and one `yr_value` per field (absent values are
`yr_value_none()`). Strings point into the plugin's memory and are copied
by Yantra during the call. A decoded serving beacon also gives its
neighbour list as NEIGHBOUR records (RAD-22).

<!-- snippet: plugins/beacon/beacon_plugin.cpp#record -->
```cpp
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
```

### Step 7: settings, actions, presets and sensors as data

Settings are listed as data with their type, range and step, unit, group,
help text and flags (RAD-40). `set_setting` checks the value (range, type,
enum item), rounds it to the device's step and returns
`YR_ERR_INVALID_ARGUMENT` with a `last_error` text for a wrong one
(RAD-41). When one change alters other settings (here: AGC makes the gain
unavailable) the plugin calls `settings_changed` and Yantra lists and
reads them again (RAD-44).

<!-- snippet: plugins/beacon/beacon_plugin.cpp#settings -->
```cpp
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
```

<!-- snippet: plugins/beacon/beacon_plugin.cpp#set_setting -->
```cpp
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
```

A preset applies several settings at once and calls `settings_changed`
(RAD-43); sensors are read-only values with the same definitions
(RAD-45); an action is a setting of type ACTION that runs with
`run_action` and gives a one-line result (RAD-42); the self-test reports
items with pass, warn, fail or skipped and a remedy (RAD-46). The six
optional functions come in pairs: a plugin without presets leaves both
`list_presets` and `apply_preset` NULL.

<!-- snippet: plugins/beacon/beacon_plugin.cpp#preset_sensor -->
```cpp
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
```

### Step 8: build it and test it

A plugin is built with `yantra_add_radio_plugin()`
(`cmake/YantraRadioPlugin.cmake`): a self-contained module that exports
only `yantra_radio_entry`, hides every bundled symbol and links the GCC
runtime statically (RAD-131..133). The same call gives the plugin two
ctests: `completeness_<name>` (tools/ci/check_completeness.py: allowed
NEEDED libraries, one exported symbol) and `conformance_<name>` (the
conformance kit below). The beacon's `plugins/beacon/CMakeLists.txt` is
the whole build:

```cmake
yantra_add_radio_plugin(beacon
  SOURCES beacon_plugin.cpp beacon_demod.c beacon_gen.c beacon_proto.c)
```

A plugin outside this repository can include `cmake/YantraRadioPlugin.cmake`
the same way, or follow its flags, and run `yantra-radio-host conformance`
on its library.

## Settings as data: how Yantra builds the windows

Yantra never has code for one device. From the definitions it builds:

- the **settings window**: one section per `group`, ordered by
  `sort_order`; a field per setting by type (check box for BOOL, spin box
  with `minimum`, `maximum`, `step` and `unit` for numbers, drop-down for
  ENUM items, text field for STRING, button for ACTION). The `help` text
  is the tooltip and the "(?)" text. `YR_SETTING_ADVANCED` settings appear
  only with "Advanced settings"; `YR_SETTING_UNAVAILABLE` greys the field
  out with `unavailable_reason`; `YR_SETTING_WARNING` asks for
  confirmation with `warning`; `YR_SETTING_RESTART` makes Yantra stop the
  stream, apply and restart (RAD-40);
- the **presets** menu with each preset's label and help (RAD-43);
- the **device status** panel from the sensors (RAD-45);
- the **self-test** report with remedies (RAD-46);
- the **calibration key** from the settings flagged
  `YR_SETTING_CALIBRATION` (a calibration is valid only for the same
  values, Yantra CAL-*);
- translated texts from the descriptor's translations (`nl`, `fr`, `de`;
  keys such as `setting.gain_db.label`, RAD-48), English otherwise.

Yantra stores the values per device and applies them after `open`, so a
plugin must not keep its own settings file (RAD-47).

## Record schemas

### Core fields (every MEASUREMENT schema, fixed indexes, RAD-21)

| Index | Name | Type | Unit | Meaning |
|---|---|---|---|---|
| 0 | `frequency_hz` | uint | Hz | Carrier frequency |
| 1 | `channel` | uint | - | Channel number of the technology's band plan |
| 2 | `role` | enum | - | `serving`, `neighbour` or `other` |
| 3 | `level` | double | dBFS (dBm when self-calibrated) | Signal level |
| 4 | `mcc` | uint | - | Mobile country code |
| 5 | `mnc` | uint | - | Mobile network code |
| 6 | `la` | uint | - | Location area |
| 7 | `colour_code` | uint | - | Colour code |

The identity fields are absent in a `no_sync` record.

### Common optional fields (radio.h `YR_FIELD_*`)

| Name | Type | Unit | Group | Meaning |
|---|---|---|---|---|
| `c1_db` | double | dB | c1 | Path loss criterion C1 (EN 300 392-2 clause 23) |
| `hyperframe`, `multiframe`, `frame`, `slot` | uint | - | frame | TETRA frame number (RAD-27) |
| `mer_pct` | double | % | quality | Message erasure rate (AACH, 100 slots) |
| `ber_pct` | double | % | quality | Bit error rate estimate |
| `snr_db` | double | dB | quality | Signal-to-noise ratio |
| `evm_mer_db` | double | dB | quality | Modulation error ratio from EVM |
| `freq_error_hz`, `freq_error_ppm` | double | Hz, ppm | quality | Frequency error |
| `crc_error_pct_<channel>` | double | % | quality | CRC error rate per logical channel |
| `sim_floor_id`, `sim_x_m`, `sim_y_m` | string, double | -, m | sim_position | Position of a simulated record on the plan (RAD-19) |
| `entry_index`, `entry_count` | uint | - | core of a NEIGHBOUR schema | Position in and length of the neighbour list |

The TETRA schemas with every field, how it is measured and the
EN 300 392-2 clause of its broadcast message are documented with the
TETRA plugins (requirements DEC-*), when they are built.

### The beacon schemas (RAD-64)

`beacon` (MEASUREMENT, optional groups quality and broadcast):

| Name | Type | Group | Unit | How it is obtained |
|---|---|---|---|---|
| `frequency_hz` | uint | core | Hz | Centre frequency of the beacon channel. |
| `channel` | uint | core | - | Beacon channel number: frequency = 430 MHz + channel x 25 kHz. |
| `role` | enum | core | - | Serving: the beacon followed; neighbour: announced by it; other: any other. |
| `level` | double | core | dBFS | Mean power of the received frame relative to full scale. |
| `mcc` | uint | core | - | Country code: always 901 (the beacon network is made up). |
| `mnc` | uint | core | - | Network code from payload byte 1 of the frame. |
| `la` | uint | core | - | Beacon id from payload bytes 2-3 of the frame. |
| `colour_code` | uint | core | - | Not used by beacons: always 0. |
| `snr_db` | double | quality | dB | Frame power over the noise power measured before the frame. |
| `freq_error_hz` | double | quality | Hz | Carrier offset measured on the preamble. |
| `beacon_class` | enum | broadcast | - | Beacon class from payload byte 0 (low nibble). |
| `tx_power_dbm` | int | broadcast | dBm | Transmit power the beacon announces, payload byte 5. |
| `sequence` | uint | broadcast | - | Frame counter of the beacon, payload byte 4 (wraps at 256). |

`beacon_neighbour` (NEIGHBOUR, one record per announced neighbour):

| Name | Type | Group | Unit | How it is obtained |
|---|---|---|---|---|
| `frequency_hz` | uint | core | Hz | Channel of the beacon that broadcast the list. |
| `entry_index` | uint | core | - | Position in the broadcast neighbour list, from 0. |
| `entry_count` | uint | core | - | Number of neighbours in the list, payload byte 6. |
| `neighbour_id` | uint | neighbours | - | Beacon id of the neighbour, payload bytes 8-13. |

Record files (docs/RECORD_FILES.md) store these schemas in their header.

## Context callbacks (SCENE plugins)

A plugin with `YR_CAP_SCENE` (the simulator, RAD-61) gets four more host
callbacks (RAD-18), all NULL for other plugins:

- `get_floor_plan`: the floor shown (id, name, scale), or `YR_ERR_NO_PLAN`;
- `get_cursor`: the mouse position over the plan in plan metres, or
  `YR_ERR_NO_POSITION`;
- `get_walk_position`: the walk-test position, or `YR_ERR_NO_POSITION`;
- `get_scene`: the scene JSON of a floor (docs/SCENE_FORMAT.md), with
  `YR_ERR_BUFFER_TOO_SMALL` and the needed length when the buffer is too
  small.

Yantra also calls the descriptor's `on_context_event` with
`YR_CONTEXT_PLAN_CHANGED`, `SCENE_CHANGED`, `CURSOR_MOVED` (at most 30
per second) or `WALK_POSITION`; the plugin then asks again. The callbacks
never block and may be called from any plugin thread.

Context data stays in the plugin (RAD-19): no plan name, file path,
example id or scene text goes into records or into log lines above debug.
The only exception is the `sim_position` group of a simulated record: the
floor id and the plan position the record was simulated at.

The radio test host emulates these callbacks (`yantra-radio-host stream
--scene FILE --cursor X,Y;... --walk X,Y;... --path-step N`).

## The conformance kit

`yantra-radio-host conformance --plugin FILE` (RAD-91) runs every check of
the radio API rules it can against a plugin library and reports each as
pass, fail or skip:

| Check | What it does | Requirements |
|---|---|---|
| `load` | Loads the library, validates the descriptor and every schema | RAD-10, RAD-12, RAD-13 |
| `version` | The refusal rule: other major or older host minor refused | RAD-11 |
| `enumerate` | Devices with unique keys and complete info, or NO_DEVICE with a diagnostic | RAD-16, RAD-30 |
| `open_close` | Unknown key refused with a text; open, device_info, stats, close, open again | RAD-16, RAD-31, RAD-34 |
| `settings` | Every setting read; every writable one written at its minimum, maximum, between two steps and back; read-back in range, on the raster, within one step; wrong type, out of range and unknown items refused with a text; any other setting that changes needs `settings_changed` | RAD-40, RAD-41, RAD-44 |
| `persistence` | A changed setting is back at its default after close and open | RAD-47 |
| `actions`, `presets`, `sensors`, `self_test` | Each one run, with result texts, `settings_changed` after a preset, remedies for warnings | RAD-42, RAD-43, RAD-45, RAD-46 |
| `plans` | Plan ranges; SCAN offered exactly with the capability; unsupported plan types refused | RAD-23, RAD-32 |
| `records` | A stream: every record matches its schema, at least one MEASUREMENT, time inside the stream and in order per carrier, UTC near the host clock, a record per carrier at least every 1.25 s, known events, statistics | RAD-20..22, RAD-24, RAD-25, RAD-27 |
| `plan_change` | A carrier plan gives records on those carriers only; `set_plan` while streaming is BUSY or (RETUNE_LIVE) PLAN_APPLIED; a scan ends with SCAN_DONE | RAD-23, RAD-32 |
| `state_machine` | Second start, self-test and RESTART settings while streaming are BUSY; stop within 1 s, no push after it; second stop harmless; close while streaming | RAD-15, RAD-31, RAD-33, RAD-34 |
| `stop_during_push` | The host holds each push 100 ms; stop comes during a push and must wait for it | RAD-33 |
| `sink_full` | The host keeps one record and drops the rest; the plugin must carry on | RAD-24 |
| `hotplug` | With `--unplug CMD --replug CMD`: DEVICE_LOST, state LOST, DEVICE_LOST from calls, `devices_changed`, DEVICE_BACK, reopen | RAD-34, RAD-35 |
| `context` | SCENE plugins: no plan, then a plan with its scene and scripted cursor and walk paths, plan hidden and shown; sim_position on the shown floor at a given point; context texts never in records or logs above debug | RAD-14, RAD-18, RAD-19, RAD-36 |
| `completeness` | Labels and help texts everywhere; groups; every translation key names something | RAD-48, RAD-49 |
| `scope` | No schema, field, setting, sensor, preset or item name outside the scope | RAD-17, RAD-26 |
| `cleanup` | No plugin thread left after close and shutdown (leaks: run the kit in the asan-ubsan build) | RAD-15, RAD-31 |

Options: `--device KEY` (default: the first usable device), `--scene FILE`
(SCENE plugins; default a built-in scene), `--duration S` (length of the
records stream, default 2.5 s), `--unplug CMD --replug CMD` (shell
commands for HOTPLUG devices, e.g. with the fake SoapySDR module of
RAD-92) and `--report FILE` (`-` for standard output). Exit code 0 when no
check failed, 1 when one did, 2 for a usage error.

The JSON report (`"format": "yantra-radio-conformance"`, `"version": 1`)
holds the host and plugin versions, the device, the overall `result`,
`counts` and one entry per check with `id`, `title`, `requirements`,
`result`, `detail`, `problems` and `seconds`.

In this repository every plugin built with `yantra_add_radio_plugin()`
has the ctest `conformance_<name>`, which writes
`build/conformance/<name>.json`; CI runs the kit again in a clean
container (RAD-133). Yantra runs the same kit on the plugins it ships
(Yantra PLG-13).
