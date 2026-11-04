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

/// The diagnostics text (RAD-52): modules and versions, SoapySDR version,
/// the devices found and the usual reasons none is (udev rules, DVB
/// kernel driver).
std::string diagnostics(const DriverFilter& filter);

/// What a plugin built on the layer is.
struct Options {
    const char* name;        ///< plugin name ("soapysdr")
    const char* label;       ///< "SDR receiver (SoapySDR)"
    const char* version;     ///< plugin version
    const char* description; ///< one line
    const char* source_url;  ///< RAD-12
    const char* modules;     ///< the module table (see module_table())
    DriverFilter filter;
};

/// The plugin's descriptor; `options` must outlive the plugin (a static).
const yr_descriptor* descriptor(const Options& options);

/// Setting keys of a module setting and a module sensor (RAD-51): `arg_`
/// and `sensor_` plus the module key in `[a-z0-9_]`; "" when the module
/// key would be outside the scope boundary (RAD-17) and is left out.
std::string arg_setting_key(const std::string& module_key);
std::string sensor_setting_key(const std::string& module_key);

} // namespace yantra::soapy
