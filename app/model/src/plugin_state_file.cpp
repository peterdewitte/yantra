// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugins' state file, see plugin_state_file.h.
#include "yantra/app/plugin_state_file.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace yantra::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kFormat = "yantra-plugin-state";
constexpr int kVersion = 1;

json value_json(const plugin::Value& v) {
    return std::visit(
        [](const auto& x) -> json {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bool>) return {{"type", "bool"}, {"value", x}};
            else if constexpr (std::is_same_v<T, std::int64_t>) return {{"type", "int"}, {"value", x}};
            else if constexpr (std::is_same_v<T, std::uint64_t>) return {{"type", "uint"}, {"value", x}};
            else if constexpr (std::is_same_v<T, double>) return {{"type", "double"}, {"value", std::isfinite(x) ? x : 0.0}};
            else if constexpr (std::is_same_v<T, std::string>) return {{"type", "string"}, {"value", x}};
            else if constexpr (std::is_same_v<T, plugin::EnumValue>) return {{"type", "enum"}, {"value", x.value}};
            else return {{"type", "none"}};
        },
        v);
}

std::string str(const json& j, const char* member) {
    const auto it = j.find(member);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

std::int64_t i64(const json& j, const char* member) {
    const auto it = j.find(member);
    return it != j.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
}

plugin::Value value_of(const json& j) {
    const std::string t = str(j, "type");
    const json& v = j.at("value");
    if (t == "bool") return v.get<bool>();
    if (t == "int") return v.get<std::int64_t>();
    if (t == "uint") return v.get<std::uint64_t>();
    if (t == "double") return v.get<double>();
    if (t == "string") return v.get<std::string>();
    if (t == "enum") return plugin::EnumValue{v.get<std::int64_t>()};
    throw std::runtime_error("unknown value type '" + t + "'");
}

}  // namespace

std::string plugin_state_to_json(const std::map<std::string, bool>& enabled,
                                 const std::map<std::string, std::map<std::string, std::string>>& settings,
                                 const std::map<std::pair<std::string, std::string>,
                                                std::vector<std::pair<std::string, plugin::Value>>>& devices) {
    json j;
    j["format"] = kFormat;
    j["version"] = kVersion;
    j["enabled"] = json::object();
    for (const auto& [id, on] : enabled) j["enabled"][id] = on;
    j["settings"] = json::object();
    for (const auto& [id, kv] : settings)
        for (const auto& [k, v] : kv) j["settings"][id][k] = v;
    j["devices"] = json::array();
    for (const auto& [key, values] : devices) {
        json d;
        d["plugin"] = key.first;
        d["device"] = key.second;
        d["settings"] = json::array();
        for (const auto& [k, v] : values) {
            json s = value_json(v);
            s["key"] = k;
            d["settings"].push_back(std::move(s));
        }
        j["devices"].push_back(std::move(d));
    }
    return j.dump(2) + "\n";
}

PluginStateFile::PluginStateFile(fs::path file) : file_(std::move(file)) {
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    try {
        std::ifstream in(file_, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const json j = json::parse(ss.str());
        if (str(j, "format") != kFormat) throw std::runtime_error("not a Yantra plugin state file");
        if (i64(j, "version") > kVersion) throw std::runtime_error("made by a newer Yantra");
        decltype(enabled_) en;
        decltype(settings_) st;
        decltype(devices_) dv;
        const json jen = j.contains("enabled") ? j.at("enabled") : json::object();
        const json jst = j.contains("settings") ? j.at("settings") : json::object();
        const json jdv = j.contains("devices") ? j.at("devices") : json::array();
        for (const auto& [id, on] : jen.items()) en[id] = on.get<bool>();
        for (const auto& [id, kv] : jst.items())
            for (const auto& [k, v] : kv.items()) st[id][k] = v.get<std::string>();
        for (const json& d : jdv) {
            auto& list = dv[{d.at("plugin").get<std::string>(), d.at("device").get<std::string>()}];
            for (const json& s : d.at("settings")) list.emplace_back(s.at("key").get<std::string>(), value_of(s));
        }
        enabled_ = std::move(en);
        settings_ = std::move(st);
        devices_ = std::move(dv);
    } catch (const std::exception& e) {
        load_error_ = file_.string() + ": " + e.what();
    }
}

void PluginStateFile::save() {
    // Caller holds mutex_. An unreadable file is never overwritten.
    if (!load_error_.empty()) return;
    const std::string text = plugin_state_to_json(enabled_, settings_, devices_);
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    const fs::path tmp = file_.string() + ".tmp" + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            save_error_ = "cannot write " + tmp.string();
            return;
        }
    }
    fs::rename(tmp, file_, ec);
    save_error_ = ec ? "cannot replace " + file_.string() + ": " + ec.message() : std::string();
}

std::string PluginStateFile::save_error() const {
    const std::lock_guard lock(mutex_);
    return save_error_;
}

std::optional<bool> PluginStateFile::enabled(const std::string& plugin_id) const {
    const std::lock_guard lock(mutex_);
    const auto it = enabled_.find(plugin_id);
    return it == enabled_.end() ? std::nullopt : std::optional<bool>(it->second);
}

void PluginStateFile::set_enabled(const std::string& plugin_id, bool enabled) {
    const std::lock_guard lock(mutex_);
    enabled_[plugin_id] = enabled;
    save();
}

std::optional<std::string> PluginStateFile::setting(const std::string& plugin_id, const std::string& key) const {
    const std::lock_guard lock(mutex_);
    const auto p = settings_.find(plugin_id);
    if (p == settings_.end()) return std::nullopt;
    const auto it = p->second.find(key);
    return it == p->second.end() ? std::nullopt : std::optional<std::string>(it->second);
}

void PluginStateFile::set_setting(const std::string& plugin_id, const std::string& key,
                                  const std::optional<std::string>& value) {
    const std::lock_guard lock(mutex_);
    if (value) settings_[plugin_id][key] = *value;
    else if (auto p = settings_.find(plugin_id); p != settings_.end()) p->second.erase(key);
    save();
}

std::vector<std::pair<std::string, plugin::Value>> PluginStateFile::device_settings(const std::string& plugin_id,
                                                                                    const std::string& device_key) const {
    const std::lock_guard lock(mutex_);
    const auto it = devices_.find({plugin_id, device_key});
    return it == devices_.end() ? std::vector<std::pair<std::string, plugin::Value>>{} : it->second;
}

void PluginStateFile::set_device_setting(const std::string& plugin_id, const std::string& device_key,
                                         const std::string& key, const plugin::Value& value) {
    const std::lock_guard lock(mutex_);
    auto& list = devices_[{plugin_id, device_key}];
    bool found = false;
    for (auto& [k, v] : list)
        if (k == key) {
            v = value;
            found = true;
        }
    if (!found) list.emplace_back(key, value);
    save();
}

}  // namespace yantra::app
