// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/plugin/state_store.h"

namespace yantra::plugin {

std::optional<bool> MemoryStateStore::enabled(const std::string& plugin_id) const {
    const std::lock_guard lock(mutex_);
    const auto it = enabled_.find(plugin_id);
    if (it == enabled_.end()) return std::nullopt;
    return it->second;
}

void MemoryStateStore::set_enabled(const std::string& plugin_id, bool enabled) {
    const std::lock_guard lock(mutex_);
    enabled_[plugin_id] = enabled;
}

std::optional<std::string> MemoryStateStore::setting(const std::string& plugin_id,
                                                     const std::string& key) const {
    const std::lock_guard lock(mutex_);
    const auto p = settings_.find(plugin_id);
    if (p == settings_.end()) return std::nullopt;
    const auto it = p->second.find(key);
    if (it == p->second.end()) return std::nullopt;
    return it->second;
}

void MemoryStateStore::set_setting(const std::string& plugin_id, const std::string& key,
                                   const std::optional<std::string>& value) {
    const std::lock_guard lock(mutex_);
    if (value)
        settings_[plugin_id][key] = *value;
    else if (auto p = settings_.find(plugin_id); p != settings_.end())
        p->second.erase(key);
}

std::vector<std::pair<std::string, Value>> MemoryStateStore::device_settings(
    const std::string& plugin_id, const std::string& device_key) const {
    const std::lock_guard lock(mutex_);
    const auto it = devices_.find({plugin_id, device_key});
    return it == devices_.end() ? std::vector<std::pair<std::string, Value>>{} : it->second;
}

void MemoryStateStore::set_device_setting(const std::string& plugin_id,
                                          const std::string& device_key, const std::string& key,
                                          const Value& value) {
    const std::lock_guard lock(mutex_);
    auto& list = devices_[{plugin_id, device_key}];
    for (auto& [k, v] : list) {
        if (k == key) {
            v = value;
            return;
        }
    }
    list.emplace_back(key, value);
}

}  // namespace yantra::plugin
