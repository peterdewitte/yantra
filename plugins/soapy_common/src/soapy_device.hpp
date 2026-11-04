// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal state of the shared SoapySDR plugin (RAD-54): what the module
// reports (Caps), the generated setting table (RAD-51), the settings, the
// device and the plugin with its hot-plug watcher (RAD-35).
#pragma once

#include <yantra/plugins/soapy_plugin.hpp>
#include <yantra/plugins/tetra_plugin.hpp>

#include <SoapySDR/Device.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yantra::soapy {

struct GainElement {
    std::string name;
    double min = 0.0, max = 0.0, step = 0.0;
};

/// A module setting (SoapySDR::ArgInfo) offered as `arg_<key>`.
struct ModuleArg {
    std::string module_key;
    std::string key;
    uint32_t type = YR_TYPE_STRING;
    std::vector<std::string> options; ///< ENUM: the module's values
    std::string default_text;
};

/// What the module reports for the opened device (RAD-51).
struct Caps {
    std::vector<double> rates;      ///< offered rates (within the receiver's range)
    std::vector<double> bandwidths; ///< baseband filters, empty: none to choose
    std::vector<GainElement> gains;
    bool agc = false;
    std::vector<std::string> antennas;
    bool frequency_correction = false;
    std::vector<std::string> clock_sources;
    std::vector<ModuleArg> args;
    std::vector<std::pair<std::string, std::string>> sensors; ///< module key, setting key
    double first_hz = 0.0, last_hz = 0.0;
    std::string hardware_info; ///< "key=value" lines
};

struct Settings {
    uint32_t rate = 0;      ///< index into Caps::rates
    uint32_t bandwidth = 0; ///< 0: automatic, i + 1: Caps::bandwidths[i]
    std::vector<double> gains;
    bool agc = false;
    uint32_t antenna = 0;
    double ppm = 0.0;
    uint32_t clock_source = 0;
    std::vector<std::string> args; ///< text values, as the module takes them
    double tune_mhz = 392.5;
    double record_rate_hz = 5.0;
    uint64_t threads = 2;
    double ms_max_power_dbm = radios::kTetraC1MsMaxPowerDbm;
    plugins::GsmtapSettings gsmtap;
};

/// The settings of one device: built once at open from the Caps.
struct SettingTable {
    std::deque<std::string> text; ///< stable storage of keys, labels and help
    std::deque<std::vector<yr_enum_item>> items;
    std::vector<yr_setting_def> defs;
    std::vector<yr_setting_def> sensors;
    const char* keep(std::string s)
    {
        text.push_back(std::move(s));
        return text.back().c_str();
    }
};

struct Plugin;

struct Device {
    Plugin* plugin = nullptr;
    Found found;
    std::string module_text; ///< "SoapyHackRF 0.3.6 (libhackrf 2026.01.3)"
    SoapySDR::Device* sdr = nullptr;
    Caps caps;
    SettingTable table;

    std::mutex mutex;
    std::condition_variable wake;
    uint32_t state = YR_DEVICE_OPEN;
    std::string error;
    Settings settings;
    uint64_t settings_version = 0;
    yr_plan plan{};
    std::vector<uint64_t> carriers;
    plugins::PlanSetup plan_setup;
    yr_stream* sink = nullptr;
    bool stop = false;
    int absent_scans = 0;

    std::mutex join_mutex;
    std::thread thread;

    std::atomic<int64_t> last_data_ns{0}; ///< host monotonic time of the last samples
    plugins::SinkCounters counters;
    std::atomic<uint64_t> samples_dropped{0};
    std::atomic<uint64_t> overflows{0};
    std::atomic<double> load{0.0};
    std::atomic<double> rate_hz{0.0};

    yr_device_info info{};
    std::string info_text[6];
};

struct Plugin {
    const yr_host_api* host = nullptr;
    const Options* options = nullptr;
    std::mutex mutex;
    std::string error;
    std::vector<Device*> devices;
    std::vector<std::string> listed; ///< keys of the last scan
    std::condition_variable quit_cv;
    bool quit = false;
    std::thread watcher;
};

void log(const Plugin& p, Device* d, uint32_t level, const std::string& text);

/// Reads the Caps of an opened device and builds its setting table.
void read_caps(Device& d);
void build_table(Device& d);
Settings default_settings(const Caps& c);
yr_status get_setting(const Device& d, const char* key, yr_value* out, char* text, uint32_t size);
void store_setting(Device& d, const yr_setting_def& def, const yr_value& value);
yr_status read_sensor(Device& d, const char* key, yr_value* out, char* text, uint32_t size);
double total_gain_db(const Settings& s);

/// Applies the settings to the device (all, or only the live ones).
void apply_settings(Device& d, const Settings& s, bool all);

void stream_thread(Device* d);
void join_stream(Device* d);

} // namespace yantra::soapy
