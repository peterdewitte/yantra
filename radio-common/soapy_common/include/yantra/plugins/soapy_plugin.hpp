// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared SoapySDR code of the SDR plugins (RAD-54): the SoapySDR modules
// compiled into the plugin, enumeration with stable keys, the settings
// generated from what the module reports (RAD-51), the diagnostics
// (RAD-52), the 10 MHz reference (RAD-53), the stream into the
// measurement engine with overflow and drop events (RAD-24, RAD-25) and
// hot-plug (RAD-35). A plugin is this layer plus an Options block:
//
//   const yr_descriptor* yantra_radio_entry() {
//       static const yantra::soapy::Options o = {...};
//       return yantra::soapy::descriptor(o);
//   }
//
// The SoapySDR headers stay inside the layer; plugins see only this one.
#pragma once

#include <yantra/radio.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace yantra::soapy {

/// A SoapySDR module compiled into the plugin (from YANTRA_SOAPY_MODULES or
/// a test module): its driver name, module name and version, vendor library.
struct ModuleInfo {
    std::string driver;
    std::string module;
    std::string version;
    std::string library;
};

/// The modules whose drivers are registered in this plugin, from the
/// plugin's table (Options::modules).
std::vector<ModuleInfo> modules();
/// The table: "driver|module|version|library" entries separated by ','
/// (CMake: yantra_soapy_module_table()).
const char*& module_table();

/// One device found by a module.
struct Found {
    std::string key;    ///< "<driver>:<serial>" (stable across replugs, RAD-30)
    std::string label;  ///< the module's label
    std::string driver;
    std::string serial; ///< the module's serial, else the index
    std::map<std::string, std::string> args; ///< what make() needs
};

/// Which drivers a plugin takes: `only` empty means every registered
/// driver except "null" and `exclude` (drivers with their own plugin).
struct DriverFilter {
    std::vector<std::string> only;
    std::vector<std::string> exclude;
    bool takes(const std::string& driver) const;
};

/// Enumerates the devices of the filtered drivers with their find
/// functions (no SoapySDR cache). Never throws: a failing module is left
/// out and named in `problems`.
std::vector<Found> find_devices(const DriverFilter& filter, std::string* problems = nullptr);

/// SoapyRemote (RAD-50, network radios): its driver name, and the
/// environment variable that lists the SoapySDRServer addresses
/// (host[:port] or tcp://host:port, comma-separated) whose devices the
/// plugins list. Without it no network radio is listed: the module's own
/// discovery (SSDP multicast and a thread that outlives the call) is never
/// run inside a plugin.
inline constexpr const char* kRemoteDriver = "remote";
inline constexpr const char* kRemoteServersEnv = "YANTRA_SOAPY_REMOTE";
std::vector<std::string> remote_servers();

/// The diagnostics text (RAD-52): modules and versions, SoapySDR version,
/// the devices found and the usual reasons none is (udev rules, DVB
/// kernel driver).
std::string diagnostics(const DriverFilter& filter);

/// A gain element the plugin knows by its module name (RAD-51: a
/// device-specific plugin may use the names its module documents): its
/// setting key, label and help; as_switch offers a stage with one on step
/// (e.g. the HackRF's 14 dB amplifier) as an on/off setting. A listed
/// element the module does not report is shown UNAVAILABLE with
/// `missing` as the reason.
struct GainControl {
    const char* module_name;
    const char* key;
    const char* label;
    const char* help;
    bool as_switch = false;
    const char* missing = "the module does not report this gain element";
};

/// A module setting the plugin offers under its own key and label.
/// `power` marks antenna port power (bias tee): a bool with the WARNING
/// flag, off by default, switched off at every open and replug, never
/// part of a preset.
struct ArgControl {
    const char* module_key;
    const char* key;
    const char* label;
    const char* help;
    const char* group;
    bool power = false;
    const char* missing = "the module does not offer this setting";
};

/// A sensor read from the module's hardware information (`info_key` ""
/// = the hardware key, e.g. "HackRF One"); `fallback_serial` takes the
/// enumeration's serial when the module reports none.
struct InfoSensor {
    const char* info_key;
    const char* key;
    const char* label;
    const char* help;
    bool fallback_serial = false;
};

/// A preset (RAD-43): setting key and value pairs (bools as 0/1); the
/// values of UNAVAILABLE settings are skipped.
struct PresetValue {
    const char* key;
    double value;
};
struct Preset {
    const char* id;
    const char* label;
    const char* help;
    std::vector<PresetValue> values;
};

/// What a device-specific plugin adds to the layer (RAD-60). The
/// defaults are the generic plugin's (RAD-62).
struct Profile {
    double min_rate_hz = 200e3;     ///< rates offered: the module's within this range
    double max_rate_hz = 10e6;
    double default_rate_hz = 2e6;
    /// > 0: offset tuning keeps the carriers this far from the tuned
    /// frequency (the DC spike); the setting offset_khz.
    double offset_tuning_hz = 0.0;
    bool scan = false; ///< SCAN plans (RAD-23): a sweep with retuning
    /// The settings c1_enabled and c1_level_offset_db: C1 (RAD-22) from the
    /// dBFS level plus a calibration offset (the levels stay dBFS).
    bool c1 = false;
    std::vector<GainControl> gains;
    std::vector<ArgControl> args;
    std::vector<InfoSensor> info_sensors;
    std::vector<Preset> presets;
    /// Shown by the overload sensor while the samples clip.
    const char* saturation_remedy = "reduce the gain";
};

/// What a plugin built on the layer is.
struct Options {
    const char* name;        ///< plugin name ("soapysdr")
    const char* label;       ///< "SDR receiver (SoapySDR)"
    const char* version;     ///< plugin version
    const char* description; ///< one line
    const char* source_url;  ///< RAD-12
    const char* modules;     ///< the module table (see module_table())
    DriverFilter filter;
    Profile profile = {};
};

/// The plugin's descriptor; `options` must outlive the plugin (a static).
const yr_descriptor* descriptor(const Options& options);

/// Setting keys of a module setting and a module sensor (RAD-51): `arg_`
/// and `sensor_` plus the module key in `[a-z0-9_]`; "" when the module
/// key would be outside the scope boundary (RAD-17) and is left out.
std::string arg_setting_key(const std::string& module_key);
std::string sensor_setting_key(const std::string& module_key);

} // namespace yantra::soapy
