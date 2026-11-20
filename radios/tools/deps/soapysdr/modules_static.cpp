// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replaces SoapySDR 0.8.1 lib/Modules.in.cpp in the static build (RAD-132,
// RAD-136). Upstream's loader searches SOAPY_SDR_ROOT, SOAPY_SDR_PLUGIN_PATH
// and the system module folders and dlopens every module it finds; inside a
// self-contained plugin that would mix a system SoapySDR's modules (built
// against another copy of the library) into ours. Here the modules are
// compiled into the plugin and register themselves with static
// SoapySDR::Registry objects when the plugin is loaded, so:
//   - there are no search paths and no module files (listModules is empty);
//   - loadModule/unloadModule refuse with a reason, never dlopen;
//   - loadModules only registers the built-in "null" device, as upstream.
// The declarations are upstream's (SoapySDR/Modules.hpp); the helpers the
// other library files use (getEnvImpl, getModuleLoading, getLoaderResults,
// getModuleVersions, automaticLoadModules) keep their upstream meaning.
#include <SoapySDR/Modules.hpp>

#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
std::recursive_mutex& module_mutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}
const char* const kStatic = "static SoapySDR build: modules are compiled in, nothing is loaded";
} // namespace

std::string getEnvImpl(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? value : "";
}

std::string SoapySDR::getRootPath(void) { return ""; }

std::vector<std::string> SoapySDR::listSearchPaths(void) { return {}; }

std::vector<std::string> SoapySDR::listModules(void) { return {}; }

std::vector<std::string> SoapySDR::listModules(const std::string&) { return {}; }

// The module being loaded: always "" (static registration at library load).
std::string& getModuleLoading(void)
{
    static std::string loading;
    return loading;
}

std::map<std::string, SoapySDR::Kwargs>& getLoaderResults(void)
{
    static std::map<std::string, SoapySDR::Kwargs> results;
    return results;
}

std::map<std::string, std::string>& getModuleVersions(void)
{
    static std::map<std::string, std::string> versions;
    return versions;
}

SoapySDR::ModuleVersion::ModuleVersion(const std::string& version)
{
    std::lock_guard<std::recursive_mutex> lock(module_mutex());
    getModuleVersions()[getModuleLoading()] = version;
}

std::string SoapySDR::loadModule(const std::string& path) { return path + ": " + kStatic; }

SoapySDR::Kwargs SoapySDR::getLoaderResult(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(module_mutex());
    const auto it = getLoaderResults().find(path);
    return it == getLoaderResults().end() ? SoapySDR::Kwargs() : it->second;
}

std::string SoapySDR::getModuleVersion(const std::string& path)
{
    std::lock_guard<std::recursive_mutex> lock(module_mutex());
    const auto it = getModuleVersions().find(path);
    return it == getModuleVersions().end() ? "" : it->second;
}

std::string SoapySDR::unloadModule(const std::string& path) { return path + ": " + kStatic; }

void lateLoadNullDevice(void);

void automaticLoadModules(void)
{
    std::lock_guard<std::recursive_mutex> lock(module_mutex());
    lateLoadNullDevice();
}

void SoapySDR::loadModules(void) { automaticLoadModules(); }

void SoapySDR::unloadModules(void) {}

SoapySDR::ModuleManager::ModuleManager(const bool load)
{
    if (load)
        SoapySDR::loadModules();
}

SoapySDR::ModuleManager::~ModuleManager(void) {}
