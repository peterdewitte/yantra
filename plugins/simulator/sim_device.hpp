// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator device (RAD-61): settings, state shared between the
// control functions and the stream thread, and the stream thread itself.
// Locking: Device::mutex guards everything except the atomics; the stream
// thread owns its generator (gen::ScenarioEngine) and receiver
// (MeasurementEngine), so each stream has its own (they are not
// thread-safe) and nothing else touches them.
#pragma once

#include <yantra/plugins/tetra_plugin.hpp>

#include <yantra/gen/scenario.hpp>
#include <yantra/radio.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace yantra::simulator {

/// Position sources (setting position_source).
enum PositionSource : uint32_t { kPosAuto = 0, kPosMouse = 1, kPosWalk = 2, kPosFixed = 3 };
/// Networks (setting network).
enum Network : uint32_t { kNetScenario = 0, kNetTest00101 = 1, kNetFictitious90199 = 2 };
/// Receiver noise (setting receiver_noise).
enum Noise : uint32_t { kNoiseScenario = 0, kNoiseOff = 1, kNoiseCustom = 2 };

/// The device settings (RAD-40, RAD-61); defaults are list_settings'.
struct Settings {
    uint32_t scenario = 0; ///< 0: as the plan says (RAD-18); i + 1: scenario_list()[i]
    uint32_t network = kNetScenario;
    double playback_speed = 1.0;
    double record_rate_hz = 5.0;
    uint64_t seed = 0;            ///< 0: the scenario's seed
    uint32_t sample_rate = 0;     ///< index into kSampleRates (0: the scenario's)
    double signal_offset_db = 0.0;
    uint32_t receiver_noise = kNoiseScenario;
    double noise_floor_dbm = -124.0; ///< custom noise, dBm in 18 kHz
    double oscillator_ppm = 0.0;     ///< added to the scenario's receiver ppm
    uint32_t fading = 0;             ///< 0: the scenario's; k: gen::FadingModel(k - 1)
    double gain_db = 0.0;
    bool clipping = false;
    bool calibration_generator = false;
    double calibration_level_dbm = -60.0;
    uint32_t position_source = kPosAuto;
    double fixed_x_m = 0.0;
    double fixed_y_m = 0.0;
    double ms_max_power_dbm = radios::kTetraC1MsMaxPowerDbm;
    uint64_t threads = 2;
    double device_buffer_s = 1.0;
    plugins::GsmtapSettings gsmtap;
};

/// Sample rates of the sample_rate setting (index 0: the scenario's).
inline constexpr double kSampleRates[] = {0.0, 250e3, 500e3, 1e6, 2e6};

/// What the stream thread last saw, for the sensors (RAD-45, RAD-61).
struct Snapshot {
    bool valid = false;
    std::string scenario;  ///< scenario id
    std::string simulating; ///< "example plan <id>", "own plan", "no plan", ...
    std::string floor;      ///< floor of the position, "" none
    std::string position;   ///< "mouse 3.20 m, 4.00 m", "scenario levels"
    std::string true_levels;      ///< per cell, the level the samples carry
    std::string estimated_levels; ///< per cell, the multi-wall estimate
    std::string generator;        ///< frame numbers and bursts per cell
    double noise_dbm = 0.0;       ///< in 18 kHz, -inf without noise
    uint64_t clipped = 0;
    double signal_s = 0.0;
    double load = 0.0;
    double offset_db = 0.0;       ///< dBm = dBFS + this
};

struct Plugin;

struct Device {
    Plugin* plugin = nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    uint32_t state = YR_DEVICE_OPEN;
    std::string error;
    Settings settings;
    uint64_t settings_version = 0; ///< bumped on every change: the stream rebuilds
    yr_plan plan{};                ///< carriers_hz copied into `carriers`
    std::vector<uint64_t> carriers;
    plugins::PlanSetup plan_setup; ///< the checked plan of the stream
    yr_stream* sink = nullptr;
    std::thread thread;
    bool stop = false;
    uint32_t context_events = 0; ///< bit (1 << YR_CONTEXT_*) pending
    bool fault = false;          ///< the simulated noisy receiver
    Snapshot snapshot;
    plugins::SinkCounters counters;
    std::atomic<uint64_t> samples_dropped{0};
    std::atomic<uint64_t> overflows{0};
    std::atomic<double> load{0.0};
    std::atomic<double> rate_hz{0.0};
};

struct Plugin {
    const yr_host_api* host = nullptr;
    std::mutex mutex;
    std::string error;
    Device* device = nullptr; ///< the one device while open
};

// --- settings (sim_settings.cpp) -------------------------------------------------

/// The definitions as they are with `now` (availability depends on it).
std::vector<yr_setting_def> settings_of(const Settings& now);
/// get_setting of `key`; NOT_FOUND for an unknown key.
yr_status get_setting(const Settings& s, const char* key, yr_value* out, char* text,
                      uint32_t text_size);
/// Stores a checked value (on the setting's raster).
void store_setting(Settings& s, const yr_setting_def& def, const yr_value& value);
/// The sensor definitions and values.
std::vector<yr_setting_def> sensors_of();
yr_status read_sensor(const Device& d, const char* key, yr_value* out, char* text,
                      uint32_t text_size);
/// The presets and what they set.
const std::vector<yr_preset>& presets();
bool apply_preset(Settings& s, const char* id);

// --- the simulated scenario (sim_stream.cpp) -------------------------------------

/// What the host shows (RAD-18), read through the context callbacks.
struct PlanContext {
    bool plan = false;      ///< a plan is shown
    std::string example_id; ///< "" for an own plan
    std::string floor_id;
    bool scale = false;
};
PlanContext read_plan(const yr_host_api& host);
/// The scenario the settings and plan select (gen::ScenarioError when
/// none): its id, with the device settings applied.
gen::Scenario base_scenario(const Settings& s, const PlanContext& plan);
/// The usable span of a scenario: centre +- 40 % of its sample rate.
void scenario_span(const gen::Scenario& s, double& first_hz, double& last_hz);
/// The true calibration: dBm = dBFS + this, for the settings.
double true_offset_db(const gen::Scenario& s);

/// The stream thread (start_stream to stop_stream).
void stream_thread(Device* d);

/// Logs through the host (`device` may be null).
void log(const Plugin& p, Device* d, uint32_t level, const std::string& text);

} // namespace yantra::simulator
