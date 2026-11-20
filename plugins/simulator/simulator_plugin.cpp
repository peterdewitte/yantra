// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator radio plugin (RAD-61): shown in Yantra as "Demo (simulated
// network)", capabilities SIMULATED, SCENE and SPECTRUM (RAD-140). The TETRA test-signal
// generator (RAD-70) makes IQ inside the plugin and the measurement engine
// of the SDR plugins (RAD-102) decodes it; only measurement records leave
// the plugin (RAD-20..26). Control functions here; the stream thread in
// sim_stream.cpp, settings in sim_settings.cpp.
//
// Structure after plugins/example (RAD-64); behaviour after yantra 68ea20a
// plugins/demo_radio (settings, presets, practice calibration) and
// yantra-v1 c24e8f5 (the simulated receiver).
#include "sim_device.hpp"
#include "sim_scenarios.hpp"

#include <yantra/gen/scenario_engine.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yantra::simulator {
namespace {

Plugin* P(yr_plugin* p) { return reinterpret_cast<Plugin*>(p); }
Device* D(yr_device* d) { return reinterpret_cast<Device*>(d); }

void set_device_error(Device* d, std::string text)
{
    std::lock_guard lock(d->mutex);
    d->error = std::move(text);
}

const plugins::TetraSchemaTable& schema_table()
{
    static const plugins::TetraSchemaTable t(radios::TetraSchemaOptions{false, true, false});
    return t;
}

constexpr uint32_t kMaxCarriers = 8;

// The span the device measures with its settings and the plan shown now.
yr_status current_span(Device* d, double& first, double& last, std::string& why)
{
    Settings s;
    {
        std::lock_guard lock(d->mutex);
        s = d->settings;
    }
    try {
        const gen::Scenario sc = base_scenario(s, read_plan(*d->plugin->host));
        scenario_span(sc, first, last);
        return YR_OK;
    } catch (const std::exception& e) {
        why = std::string("no scenario to simulate: ") + e.what();
        return YR_ERR_FAILED;
    }
}

// --- plugin and stream control (RAD-30..34) ---------------------------------------

yr_status sim_init(const yr_host_api* host, yr_plugin** out)
{
    YANTRA_PLUGIN_GUARD({
        if (host == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        (void)scenario_list(); // parse the embedded scenarios now, not in a stream
        auto* p = new Plugin();
        p->host = host;
        *out = reinterpret_cast<yr_plugin*>(p);
        return YR_OK;
    })
}

void sim_shutdown(yr_plugin* plugin) { delete P(plugin); }

// The device entry with its level unit (RAD-30): LEVEL_DBM in dBm mode.
const yr_device_info& device_info(bool level_dbm = true)
{
    static const std::string hw = "scenarios=" + std::to_string(scenario_list().size());
    static const yr_device_info dbm = {sizeof(yr_device_info),
                                       YR_DEVICE_SIMULATED | YR_DEVICE_LEVEL_DBM,
                                       "simulator:0", "Demo (simulated network)",
                                       "simulator", "0", "1.0", hw.c_str(), ""};
    static const yr_device_info dbfs = [] {
        yr_device_info i = dbm;
        i.flags = YR_DEVICE_SIMULATED;
        return i;
    }();
    return level_dbm ? dbm : dbfs;
}

yr_status sim_enumerate(yr_plugin* plugin, yr_device_cb cb, void* user)
{
    // Always present (the device is simulated); the unit of the open
    // device, else the default (dBm).
    Plugin* p = P(plugin);
    bool dbm = true;
    {
        std::lock_guard lock(p->mutex);
        if (p->device != nullptr)
            dbm = p->device->level_dbm.load();
    }
    cb(user, &device_info(dbm));
    return YR_OK;
}

yr_status sim_open(yr_plugin* plugin, const char* key, yr_device** out)
{
    YANTRA_PLUGIN_GUARD({
        Plugin* p = P(plugin);
        std::lock_guard lock(p->mutex);
        if (key == nullptr || out == nullptr || std::strcmp(key, device_info().key) != 0) {
            p->error = "no simulator device with this key; use simulator:0";
            return YR_ERR_NOT_FOUND;
        }
        if (p->device != nullptr) {
            p->error = "simulator:0 is already open; close it first";
            return YR_ERR_BUSY;
        }
        auto* d = new Device();
        d->plugin = p;
        d->plan.struct_size = sizeof d->plan;
        d->plan.type = YR_PLAN_SERVING;
        p->device = d;
        p->error.clear();
        *out = reinterpret_cast<yr_device*>(d);
        return YR_OK;
    })
}

yr_status check_plan(Device* d, const yr_plan* plan, plugins::PlanSetup& setup)
{
    if (plan == nullptr || !YR_HAS_FIELD(plan, yr_plan, scan_step_hz) ||
        (plan->type == YR_PLAN_CARRIERS && plan->carrier_count > 0 && plan->carriers_hz == nullptr)) {
        set_device_error(d, "a plan is needed");
        return YR_ERR_INVALID_ARGUMENT;
    }
    std::string why;
    double first = 0.0, last = 0.0;
    yr_status st = current_span(d, first, last, why);
    if (st == YR_OK) {
        std::vector<uint64_t> carriers;
        if (plan->type == YR_PLAN_CARRIERS)
            carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        st = plugins::plan_setup(*plan, carriers, first, last, kMaxCarriers, false, setup, why);
    }
    if (st != YR_OK)
        set_device_error(d, why);
    return st;
}

yr_status sim_start(yr_device* device, const yr_plan* plan, yr_stream* sink)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (sink == nullptr) {
            set_device_error(d, "start_stream needs a sink");
            return YR_ERR_INVALID_ARGUMENT;
        }
        {
            std::lock_guard lock(d->mutex);
            if (d->state != YR_DEVICE_OPEN) // already streaming (RAD-34)
                return YR_ERR_BUSY;
        }
        plugins::PlanSetup setup;
        if (const yr_status st = check_plan(d, plan, setup); st != YR_OK)
            return st;
        std::lock_guard lock(d->mutex);
        if (d->thread.joinable())
            d->thread.join(); // a stream that stopped on an error
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr; // valid during the call only (RAD-10)
        d->plan_setup = setup;
        d->sink = sink;
        d->stop = false;
        d->context_events = 0;
        d->counters.reset();
        d->samples_dropped = 0;
        d->overflows = 0;
        d->snapshot = Snapshot{};
        d->state = YR_DEVICE_STREAMING;
        d->thread = std::thread(stream_thread, d);
        return YR_OK;
    })
}

yr_status sim_stop(yr_device* device)
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

void sim_close(yr_device* device)
{
    Device* d = D(device);
    sim_stop(device); // close stops a running stream first (RAD-31)
    {
        std::lock_guard lock(d->plugin->mutex);
        d->plugin->device = nullptr;
    }
    delete d;
}

yr_status sim_device_info(yr_device* device, yr_device_cb cb, void* user)
{
    cb(user, &device_info(D(device)->level_dbm.load()));
    return YR_OK;
}

uint32_t sim_state(yr_device* device)
{
    std::lock_guard lock(D(device)->mutex);
    return D(device)->state;
}

yr_status sim_plan_ranges(yr_device* device, yr_plan_ranges_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        double first = 0.0, last = 0.0;
        std::string why;
        if (const yr_status st = current_span(d, first, last, why); st != YR_OK) {
            set_device_error(d, why);
            return st;
        }
        const yr_freq_range range = {static_cast<uint64_t>(std::ceil(first)),
                                     static_cast<uint64_t>(std::floor(last))};
        const yr_plan_ranges r = {sizeof r, (1u << YR_PLAN_SERVING) | (1u << YR_PLAN_CARRIERS), 1,
                                  kMaxCarriers, &range, last - first};
        cb(user, &r);
        return YR_OK;
    })
}

yr_status sim_set_plan(yr_device* device, const yr_plan* plan)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        {
            std::lock_guard lock(d->mutex);
            if (d->state == YR_DEVICE_STREAMING) { // no RETUNE_LIVE (RAD-32)
                d->error = "stop the stream to change the plan";
                return YR_ERR_BUSY;
            }
        }
        plugins::PlanSetup setup;
        if (const yr_status st = check_plan(d, plan, setup); st != YR_OK)
            return st;
        std::lock_guard lock(d->mutex);
        yr_plan_read(plan, &d->plan); // only what the host's struct_size holds (RAD-10)
        d->carriers.clear();
        if (plan->type == YR_PLAN_CARRIERS)
            d->carriers.assign(plan->carriers_hz, plan->carriers_hz + plan->carrier_count);
        d->plan.carriers_hz = nullptr;
        return YR_OK;
    })
}

yr_status sim_stats(yr_device* device, yr_stream_stats* out)
{
    if (out == nullptr || !YR_HAS_FIELD(out, yr_stream_stats, sample_rate_hz))
        return YR_ERR_INVALID_ARGUMENT;
    Device* d = D(device);
    out->records_delivered = d->counters.delivered.load();
    out->records_dropped = 0;
    out->samples_dropped = d->samples_dropped.load();
    out->overflows = d->overflows.load();
    out->receiver_load = d->load.load();
    out->sample_rate_hz = d->rate_hz.load();
    return YR_OK;
}

yr_status sim_last_error(yr_plugin* plugin, yr_device* device, char* buffer, uint32_t size)
{
    std::string text;
    if (device != nullptr) {
        std::lock_guard lock(D(device)->mutex);
        text = D(device)->error;
    } else if (plugin != nullptr) {
        std::lock_guard lock(P(plugin)->mutex);
        text = P(plugin)->error;
    }
    return radio::copy_text(text, buffer, size);
}

// Context events (RAD-18): noted for the stream thread, which reads the
// plan, scene and position through the host callbacks itself.
void sim_context_event(yr_plugin* plugin, uint32_t event)
{
    Plugin* p = P(plugin);
    if (p == nullptr || event < YR_CONTEXT_PLAN_CHANGED || event > YR_CONTEXT_WALK_POSITION)
        return;
    std::lock_guard plock(p->mutex);
    if (p->device == nullptr)
        return;
    std::lock_guard lock(p->device->mutex);
    p->device->context_events |= 1u << event;
}

// --- device control (RAD-40..46) --------------------------------------------------

yr_status sim_list_settings(yr_device* device, yr_setting_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
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

yr_status sim_get(yr_device* device, const char* key, yr_value* out, char* text, uint32_t size)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (key == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        std::lock_guard lock(d->mutex);
        const yr_status st = get_setting(d->settings, key, out, text, size);
        if (st == YR_ERR_NOT_FOUND)
            d->error = std::string("unknown setting ") + key;
        return st;
    })
}

// Settings that change the definitions (availability) of others (RAD-44).
bool others_differ(const std::vector<yr_setting_def>& a, const std::vector<yr_setting_def>& b)
{
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i].flags != b[i].flags)
            return true;
    return a.size() != b.size();
}

yr_status sim_set(yr_device* device, const char* key, const yr_value* value)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        std::unique_lock lock(d->mutex);
        const std::vector<yr_setting_def> before = settings_of(d->settings);
        const yr_setting_def* def = plugins::find_setting(before, key);
        if (def == nullptr || value == nullptr) {
            d->error = std::string("unknown setting ") + (key != nullptr ? key : "(null)");
            return YR_ERR_NOT_FOUND;
        }
        std::string why;
        if (radio::check_setting_value(*def, *value, &why) != YR_OK) {
            d->error = why;
            return YR_ERR_INVALID_ARGUMENT;
        }
        if ((def->flags & YR_SETTING_RESTART) != 0 && d->state == YR_DEVICE_STREAMING) {
            d->error = std::string(def->label) + " changes only while stopped";
            return YR_ERR_BUSY;
        }
        store_setting(d->settings, *def, *value);
        d->level_dbm = d->settings.level_unit == kLevelDbm;
        ++d->settings_version; // a running stream rebuilds the simulation (not for the unit)
        const bool notify = others_differ(before, settings_of(d->settings));
        lock.unlock();
        d->wake.notify_all();
        if (notify)
            d->plugin->host->settings_changed(d->plugin->host->host, device);
        return YR_OK;
    })
}

yr_status sim_action(yr_device* device, const char* key, char* result, uint32_t size)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (key != nullptr && std::strcmp(key, "inject_fault") == 0) {
            set_device_error(d, "");
            {
                std::lock_guard lock(d->mutex);
                d->fault = true;
            }
            return radio::copy_text("simulated fault: receiver noise +20 dB", result, size);
        }
        if (key != nullptr && std::strcmp(key, "clear_fault") == 0) {
            {
                std::lock_guard lock(d->mutex);
                d->fault = false;
            }
            return radio::copy_text("simulated fault cleared", result, size);
        }
        set_device_error(d, "unknown action");
        return YR_ERR_NOT_FOUND;
    })
}

yr_status sim_list_presets(yr_device*, yr_preset_cb cb, void* user)
{
    for (const yr_preset& p : presets())
        cb(user, &p);
    return YR_OK;
}

yr_status sim_apply_preset(yr_device* device, const char* id)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        {
            std::lock_guard lock(d->mutex);
            Settings s = d->settings;
            if (id == nullptr || !apply_preset(s, id)) {
                d->error = "unknown preset";
                return YR_ERR_NOT_FOUND;
            }
            if (d->state == YR_DEVICE_STREAMING &&
                (s.threads != d->settings.threads || s.gsmtap.enabled != d->settings.gsmtap.enabled ||
                 s.gsmtap.host != d->settings.gsmtap.host || s.gsmtap.port != d->settings.gsmtap.port)) {
                d->error = "this preset changes receiver settings that change only while stopped";
                return YR_ERR_BUSY;
            }
            d->settings = s;
            d->level_dbm = d->settings.level_unit == kLevelDbm;
            ++d->settings_version;
        }
        d->wake.notify_all();
        d->plugin->host->settings_changed(d->plugin->host->host, device);
        return YR_OK;
    })
}

yr_status sim_list_sensors(yr_device*, yr_setting_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
        for (const yr_setting_def& s : sensors_of())
            cb(user, &s);
        return YR_OK;
    })
}

yr_status sim_read_sensor(yr_device* device, const char* key, yr_value* out, char* text,
                          uint32_t size)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (key == nullptr || out == nullptr)
            return YR_ERR_INVALID_ARGUMENT;
        std::lock_guard lock(d->mutex);
        const yr_status st = read_sensor(*d, key, out, text, size);
        if (st == YR_ERR_NOT_FOUND)
            d->error = std::string("unknown sensor ") + key;
        return st;
    })
}

// Self-test (RAD-46): the embedded scenarios, a decode loopback through the
// generator and the receiver, and the context callbacks.
yr_status sim_self_test(yr_device* device, yr_selftest_cb cb, void* user)
{
    YANTRA_PLUGIN_GUARD({
        Device* d = D(device);
        if (sim_state(device) == YR_DEVICE_STREAMING)
            return YR_ERR_BUSY;
        using plugins::selftest_item;
        // 1. scenarios
        const bool has_default = std::any_of(scenario_list().begin(), scenario_list().end(),
                                             [](const gen::ScenarioInfo& s) { return s.is_default; });
        const bool ok = scenario_problems().empty() && has_default;
        const std::string detail = std::to_string(scenario_list().size()) + " scenarios" +
                                   (has_default ? ", with a default" : ", no default") +
                                   (scenario_problems().empty() ? "" : "; " + scenario_problems());
        yr_selftest_item a = selftest_item(ok ? YR_TEST_PASS : YR_TEST_FAIL, "scenarios",
                                           "Built-in scenarios", detail.c_str(),
                                           ok ? "" : "report this as a bug of the simulator plugin");
        cb(user, &a);

        // 2. loopback: a known carrier generated and decoded
        std::string loop;
        bool decoded = false;
        try {
            Settings s;
            s.calibration_generator = true;
            s.calibration_level_dbm = -70.0;
            gen::Scenario sc = base_scenario(s, PlanContext{});
            gen::ScenarioEngine g(sc);
            receiver::tetra::EngineConfig cfg;
            cfg.record_interval_s = 0.5;
            cfg.simulated = true;
            receiver::tetra::MeasurementEngine e(cfg);
            const gen::ScenarioCell& want = sc.cells.front();
            std::vector<std::complex<float>> x(static_cast<std::size_t>(sc.sample_rate_hz * 0.05));
            receiver::tetra::EngineOutput out;
            for (int k = 0; k < 60 && !decoded; ++k) {
                const std::size_t n = g.read(x.data(), x.size());
                receiver::tetra::BlockInfo bi;
                bi.monotonic_ns = k == 0 ? 0 : -1;
                e.process(x.data(), n, g.centre_frequency_hz(), g.sample_rate(), bi, out);
                for (const radios::Record& r : out.records)
                    if (r.schema_index == 0 && r.values[YR_CORE_LA].type == YR_TYPE_UINT &&
                        r.values[YR_CORE_LA].u64 == want.location_area &&
                        r.values[YR_CORE_MCC].u64 == sc.mcc) {
                        decoded = true;
                        const double dbm = r.values[YR_CORE_LEVEL].f64 + true_offset_db(sc);
                        loop = "cell " + want.id + " decoded at " + radio::number_text(std::round(dbm * 10) / 10) +
                               " dBm (generated -70 dBm)";
                    }
                out.clear();
            }
            if (!decoded)
                loop = "the generated cell was not decoded in 3 s of signal";
        } catch (const std::exception& ex) {
            loop = std::string("the loopback failed: ") + ex.what();
        }
        yr_selftest_item b = selftest_item(decoded ? YR_TEST_PASS : YR_TEST_FAIL, "loopback",
                                           "Generator and receiver loopback", loop.c_str(),
                                           decoded ? "" : "report this as a bug of the simulator plugin");
        cb(user, &b);

        // 3. context callbacks (RAD-18)
        const PlanContext pc = read_plan(*d->plugin->host);
        const char* what = !pc.plan ? "no floor plan is shown"
                           : pc.example_id.empty() ? "an own floor plan is shown"
                                                   : "a bundled example plan is shown";
        yr_selftest_item c = selftest_item(
            pc.plan ? YR_TEST_PASS : YR_TEST_WARN, "context", "Floor plan and positions", what,
            pc.plan ? "" : "open a floor plan to simulate at the mouse or walk position; until "
                           "then the scenario's own levels are used");
        cb(user, &c);
        return YR_OK;
    })
}

// --- translations (RAD-48) and the descriptor (RAD-12) ---------------------------

const yr_translation kTranslations[] = {
    {"setting.scenario.label", "nl", "Scenario"},
    {"setting.scenario.label", "fr", "Scénario"},
    {"setting.scenario.label", "de", "Szenario"},
    {"setting.playback_speed.label", "nl", "Afspeelsnelheid"},
    {"setting.playback_speed.label", "fr", "Vitesse de lecture"},
    {"setting.playback_speed.label", "de", "Wiedergabegeschwindigkeit"},
    {"setting.position_source.label", "nl", "Positiebron"},
    {"setting.position_source.label", "fr", "Source de position"},
    {"setting.position_source.label", "de", "Positionsquelle"},
    {"setting.calibration_generator.label", "nl", "Gesimuleerde signaalgenerator"},
    {"setting.calibration_generator.label", "fr", "Générateur de signal simulé"},
    {"setting.calibration_generator.label", "de", "Simulierter Signalgenerator"},
    {"setting.ms_max_power_dbm.label", "nl", "Maximaal zendvermogen terminal (C1)"},
    {"setting.ms_max_power_dbm.label", "fr", "Puissance maximale du terminal (C1)"},
    {"setting.ms_max_power_dbm.label", "de", "Maximale Sendeleistung des Endgeräts (C1)"},
    {"preset.weak_signal.label", "nl", "Zwak signaal"},
    {"preset.weak_signal.label", "fr", "Signal faible"},
    {"preset.weak_signal.label", "de", "Schwaches Signal"},
    {"sensor.true_levels.label", "nl", "Werkelijke niveaus"},
    {"sensor.true_levels.label", "fr", "Niveaux réels"},
    {"sensor.true_levels.label", "de", "Wahre Pegel"},
};

const yr_stream_control kStreamControl = {
    sizeof(yr_stream_control), 0,         sim_enumerate, sim_open,  sim_close,
    sim_device_info,           sim_state, sim_plan_ranges, sim_set_plan, sim_start,
    sim_stop,                  sim_stats, sim_last_error};

const yr_device_control kDeviceControl = {
    sizeof(yr_device_control), 0, sim_list_settings, sim_get, sim_set, sim_action,
    sim_list_presets, sim_apply_preset, sim_list_sensors, sim_read_sensor, sim_self_test};

} // namespace
} // namespace yantra::simulator

extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    using namespace yantra::simulator;
    static const yr_descriptor descriptor = {
        sizeof(yr_descriptor),
        yr_api_version_compiled(), // refused by hosts of another major version (RAD-11)
        YR_CAP_SIMULATED | YR_CAP_SCENE | YR_CAP_SPECTRUM,
        "simulator",
        "Demo (simulated network)",
        "0.4.0",
        "The Yantra contributors",
        "GPL-3.0-or-later AND AGPL-3.0-or-later", // the receiver uses osmo-tetra (RAD-05)
        "Simulated TETRA network: generated signal, decoded by the real receiver (RAD-61)",
        YANTRA_RADIOS_SOURCE_URL,
        "tetra",
        schema_table().count(),
        sizeof kTranslations / sizeof kTranslations[0],
        schema_table().schemas(),
        kTranslations,
        sim_init,
        sim_shutdown,
        &kStreamControl,
        &kDeviceControl,
        sim_context_event,
    };
    return &descriptor;
}
