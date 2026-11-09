// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Configuration profiles (NFR-06), see config_profile.h.
#include "yantra/app/config_profile.h"

#include "yantra/app/i18n.h"
#include "yantra/core/calibration/table.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>

namespace yantra::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr const char* kFormat = "yantra-profile/1";

/// The JSON of `file`; null when it does not exist.
bool read_json(const fs::path& file, json& out, std::string& error) {
    out = nullptr;
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) return true;
    std::ifstream in(file, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    out = json::parse(text, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        error = file.string() + tr(" is not a JSON object");
        out = nullptr;
        return false;
    }
    return true;
}

bool write_atomic(const fs::path& file, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) {
            error = tr("cannot write ") + tmp.string();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        error = tr("cannot replace ") + file.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

/// Writes `part` to `file`, or removes the file when the part is null.
bool put(const fs::path& file, const json& part, std::string& error) {
    if (file.empty()) return true;
    std::error_code ec;
    if (part.is_null()) {
        fs::remove(file, ec);
        if (ec) error = tr("cannot remove ") + file.string() + ": " + ec.message();
        return !ec;
    }
    return write_atomic(file, part.dump(1) + "\n", error);
}

}  // namespace

bool profile_to_json(const AppSettings& settings, const SettingsFiles& files, std::string& out, std::string& error) {
    AppSettings s = settings;
    s.recent_projects.clear();
    s.recent_folders.clear();
    s.tour = TourProgress{};
    json j;
    j["format"] = kFormat;
    j["settings"] = json::parse(settings_to_json(s));
    j["settings"].erase("recent_projects");
    j["settings"].erase("recent_folders");
    j["settings"].erase("tour");
    json part;
    if (!read_json(files.plugin_state, part, error)) return false;
    j["plugin_state"] = part;
    if (!read_json(files.calibration_table, part, error)) return false;
    j["calibration"] = part;
    out = j.dump(1) + "\n";
    return true;
}

bool apply_profile(const std::string& text, const AppSettings& current, const SettingsFiles& files,
                   AppSettings& settings, std::string& error) {
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = tr("The file is not a Yantra profile (not JSON).");
        return false;
    }
    if (!j.contains("format") || j["format"] != kFormat) {
        error = tr("The file is not a Yantra profile (format yantra-profile/1).");
        return false;
    }
    if (!j.contains("settings") || !j["settings"].is_object()) {
        error = tr("The profile has no settings.");
        return false;
    }
    std::string why;
    AppSettings s = settings_from_json(j["settings"].dump(), why);
    if (!why.empty()) {
        error = tr("The profile's settings cannot be read: ") + why;
        return false;
    }
    if (const std::string bad = check_settings(s); !bad.empty()) {
        error = tr("The profile's settings are not valid: ") + bad;
        return false;
    }
    const json plugin_state = j.value("plugin_state", json());
    const json calibration = j.value("calibration", json());
    if (!plugin_state.is_null() && !plugin_state.is_object()) {
        error = tr("The profile's plugin state is not a JSON object.");
        return false;
    }
    std::string calibration_text;
    if (!calibration.is_null()) {
        yantra::calibration::Table table;
        std::string cal_error;
        if (!calibration.is_object() || !yantra::calibration::Table::parse(calibration.dump(), table, cal_error)) {
            error = tr("The profile's calibration table is not valid: ") + cal_error;
            return false;
        }
        calibration_text = table.to_json();  // as the calibration window writes it
    }
    s.recent_projects = current.recent_projects;
    s.recent_folders = current.recent_folders;
    s.tour = current.tour;
    if (!put(files.plugin_state, plugin_state, error)) return false;
    if (!files.calibration_table.empty()) {
        if (calibration.is_null()) {
            if (!put(files.calibration_table, calibration, error)) return false;
        } else if (!write_atomic(files.calibration_table, calibration_text, error)) {
            return false;
        }
    }
    if (!files.settings_file.empty() && !save_settings(files.settings_file, s, error)) return false;
    settings = s;
    return true;
}

}  // namespace yantra::app
