// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin state kept across runs (PLG-07, PLG-09, PLG-25): whether the user
// enabled a plugin, each plugin's own settings (yantra_host_api::
// settings_get/set) and the stored settings of radio devices (applied on
// open, RAD-30..35). PluginStateStore is the interface the loader and the
// radio host use; MemoryStateStore keeps everything in memory (tests and,
// until the app's settings file exists, the app). The app's settings layer
// (phase 9) persists it in a documented file (NFR-05).
// Adapted from yantra-v2 68ea20a: core/include/yantra/core/plugin/
// settings_store.h (interface split from the JSON file, radio devices).
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/core/plugin/record.h"

namespace yantra::plugin {

class PluginStateStore {
public:
    virtual ~PluginStateStore() = default;

    // Enabled state chosen by the user; nullopt when never chosen.
    virtual std::optional<bool> enabled(const std::string& plugin_id) const = 0;
    virtual void set_enabled(const std::string& plugin_id, bool enabled) = 0;

    // A plugin's own setting; nullopt when unset. set with nullopt removes it.
    virtual std::optional<std::string> setting(const std::string& plugin_id,
                                               const std::string& key) const = 0;
    virtual void set_setting(const std::string& plugin_id, const std::string& key,
                             const std::optional<std::string>& value) = 0;

    // Stored settings of a radio device (by radio plugin id and device
    // key), in the order they were first stored; applied in that order.
    virtual std::vector<std::pair<std::string, Value>> device_settings(
        const std::string& plugin_id, const std::string& device_key) const = 0;
    virtual void set_device_setting(const std::string& plugin_id, const std::string& device_key,
                                    const std::string& key, const Value& value) = 0;
};

// Thread-safe in-memory store.
class MemoryStateStore final : public PluginStateStore {
public:
    std::optional<bool> enabled(const std::string& plugin_id) const override;
    void set_enabled(const std::string& plugin_id, bool enabled) override;
    std::optional<std::string> setting(const std::string& plugin_id,
                                       const std::string& key) const override;
    void set_setting(const std::string& plugin_id, const std::string& key,
                     const std::optional<std::string>& value) override;
    std::vector<std::pair<std::string, Value>> device_settings(
        const std::string& plugin_id, const std::string& device_key) const override;
    void set_device_setting(const std::string& plugin_id, const std::string& device_key,
                            const std::string& key, const Value& value) override;

private:
    mutable std::mutex mutex_;
    std::map<std::string, bool> enabled_;
    std::map<std::string, std::map<std::string, std::string>> settings_;
    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, Value>>>
        devices_;
};

}  // namespace yantra::plugin
