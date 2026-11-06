// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugins' state kept across runs (PLG-09, PLG-25, RAD-47): whether
// the user enabled a plugin, the plugins' own settings and the stored
// settings of every radio device (by radio plugin id and device key),
// applied again when the device opens. A JSON file (docs/APP.md "Radio
// windows", NFR-05), written atomically after each change; a missing
// file is an empty state, an unreadable one is left alone (kept in
// memory, never overwritten: load_error() says why).
#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/core/plugin/state_store.h"

namespace yantra::app {

class PluginStateFile final : public plugin::PluginStateStore {
public:
    explicit PluginStateFile(std::filesystem::path file);

    std::optional<bool> enabled(const std::string& plugin_id) const override;
    void set_enabled(const std::string& plugin_id, bool enabled) override;
    std::optional<std::string> setting(const std::string& plugin_id, const std::string& key) const override;
    void set_setting(const std::string& plugin_id, const std::string& key,
                     const std::optional<std::string>& value) override;
    std::vector<std::pair<std::string, plugin::Value>> device_settings(const std::string& plugin_id,
                                                                       const std::string& device_key) const override;
    void set_device_setting(const std::string& plugin_id, const std::string& device_key, const std::string& key,
                            const plugin::Value& value) override;

    const std::filesystem::path& file() const noexcept { return file_; }
    /// Why the file could not be read ("" when it was read or absent).
    const std::string& load_error() const noexcept { return load_error_; }
    /// Why the last save failed ("" when it worked).
    std::string save_error() const;

private:
    void save();

    std::filesystem::path file_;
    std::string load_error_;
    mutable std::mutex mutex_;
    std::string save_error_;
    std::map<std::string, bool> enabled_;
    std::map<std::string, std::map<std::string, std::string>> settings_;
    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, plugin::Value>>> devices_;
};

/// The file's text and back (tests, docs).
std::string plugin_state_to_json(const std::map<std::string, bool>& enabled,
                                 const std::map<std::string, std::map<std::string, std::string>>& settings,
                                 const std::map<std::pair<std::string, std::string>,
                                                std::vector<std::pair<std::string, plugin::Value>>>& devices);

}  // namespace yantra::app
