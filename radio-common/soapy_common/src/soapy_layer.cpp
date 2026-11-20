// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Modules, enumeration and diagnostics of the shared SoapySDR layer
// (RAD-54, RAD-52, RAD-30).
#include <yantra/plugins/soapy_plugin.hpp>

#include <yantra/radio.hpp>

#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Version.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace yantra::soapy {
namespace {

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::string clean_key(const std::string& k)
{
    std::string out;
    for (char c : k) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '_')
            out += '_';
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    return out;
}

std::string scoped(const char* prefix, const std::string& module_key)
{
    const std::string k = clean_key(module_key);
    if (k.empty())
        return "";
    const std::string key = prefix + k;
    return radio::is_out_of_scope_name(key) ? "" : key;
}

} // namespace

std::vector<std::string> remote_servers()
{
    std::vector<std::string> out;
    const char* env = std::getenv(kRemoteServersEnv);
    if (env == nullptr)
        return out;
    for (std::string s : split(env, ',')) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
        if (!s.empty())
            out.push_back(s);
    }
    return out;
}

std::string arg_setting_key(const std::string& module_key) { return scoped("arg_", module_key); }
std::string sensor_setting_key(const std::string& module_key) { return scoped("sensor_", module_key); }

bool DriverFilter::takes(const std::string& driver) const
{
    if (driver == "null")
        return false;
    if (!only.empty())
        return std::find(only.begin(), only.end(), driver) != only.end();
    return std::find(exclude.begin(), exclude.end(), driver) == exclude.end();
}

const char*& module_table()
{
    static const char* table = "";
    return table;
}

std::vector<ModuleInfo> modules()
{
    const auto finds = SoapySDR::Registry::listFindFunctions();
    std::vector<ModuleInfo> out;
    for (const std::string& entry : split(module_table(), ',')) {
        const std::vector<std::string> f = split(entry, '|');
        if (f.size() < 4 || finds.count(f[0]) == 0)
            continue;
        out.push_back({f[0], f[1], f[2], f[3]});
    }
    return out;
}

std::vector<Found> find_devices(const DriverFilter& filter, std::string* problems)
{
    std::vector<Found> out;
    for (const auto& [driver, find] : SoapySDR::Registry::listFindFunctions()) {
        if (!filter.takes(driver))
            continue;
        SoapySDR::KwargsList list;
        try {
            if (driver == kRemoteDriver) {
                // SoapyRemote: only the configured servers, one connection
                // each; its discovery (SSDP multicast, a thread that stays)
                // never runs inside a plugin.
                for (const std::string& server : remote_servers()) {
                    SoapySDR::Kwargs args{{"driver", driver}, {"remote", server}};
                    for (auto& entry : find(args))
                        list.push_back(std::move(entry));
                }
            } else {
                SoapySDR::Kwargs args;
                args["driver"] = driver;
                list = find(args);
            }
        } catch (const std::exception& e) {
            if (problems != nullptr)
                *problems += driver + ": " + e.what() + "; ";
            continue;
        } catch (...) {
            if (problems != nullptr)
                *problems += driver + ": enumeration failed; ";
            continue;
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            Found f;
            f.driver = driver;
            f.args.insert(list[i].begin(), list[i].end());
            f.args["driver"] = driver;
            const auto serial = list[i].find("serial");
            f.serial = serial != list[i].end() && !serial->second.empty() ? serial->second
                                                                           : std::to_string(i);
            const auto label = list[i].find("label");
            f.label = label != list[i].end() ? label->second : driver + " " + f.serial;
            f.key = driver + ":" + f.serial;
            out.push_back(std::move(f));
        }
    }
    std::sort(out.begin(), out.end(), [](const Found& a, const Found& b) { return a.key < b.key; });
    return out;
}

std::string diagnostics(const DriverFilter& filter)
{
    std::ostringstream o;
    o << "SoapySDR " << SoapySDR::getLibVersion() << " (static, modules compiled in)";
    o << "; modules:";
    bool any = false;
    for (const ModuleInfo& m : modules()) {
        if (!filter.takes(m.driver))
            continue;
        o << (any ? ", " : " ") << m.module << ' ' << m.version << " (driver " << m.driver
          << ", " << m.library << ')';
        any = true;
    }
    if (!any)
        o << " none";
    std::string problems;
    const std::vector<Found> found = find_devices(filter, &problems);
    o << "; devices: " << found.size();
    if (!problems.empty())
        o << "; problems: " << problems.substr(0, problems.size() - 2);
    if (found.empty())
        o << "; no device found: check that it is plugged in, that the udev rules of the "
             "yantra-radios package are installed (USB access without root), and for an "
             "RTL-SDR that the DVB-T kernel driver dvb_usb_rtl28xxu is blacklisted";
    if (filter.takes(kRemoteDriver) && SoapySDR::Registry::listFindFunctions().count(kRemoteDriver)) {
        const std::vector<std::string> servers = remote_servers();
        if (servers.empty()) {
            o << "; network radios (SoapyRemote): set " << kRemoteServersEnv
              << " to the SoapySDRServer addresses (host[:port], comma-separated)";
        } else {
            o << "; network radio servers:";
            for (std::size_t i = 0; i < servers.size(); ++i)
                o << (i == 0 ? " " : ", ") << servers[i];
        }
    }
    return o.str();
}

} // namespace yantra::soapy
