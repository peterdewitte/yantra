// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// App settings, see settings.h.
// Copied from yantra-v2 68ea20a: app/model/src/settings.cpp, adapted.
#include "yantra/app/settings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "yantra/app/window_system.h"

namespace yantra::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

void add_recent_project(AppSettings& settings, const fs::path& project) {
    remove_recent_project(settings, project);
    auto& r = settings.recent_projects;
    r.insert(r.begin(), project.lexically_normal());
    if (r.size() > kMaxRecentProjects) r.resize(kMaxRecentProjects);
}

void remove_recent_project(AppSettings& settings, const fs::path& project) {
    auto& r = settings.recent_projects;
    const fs::path n = project.lexically_normal();
    r.erase(std::remove(r.begin(), r.end(), n), r.end());
}

void add_recent_folder(AppSettings& settings, const fs::path& folder) {
    if (folder.empty()) return;
    auto& r = settings.recent_folders;
    const fs::path n = folder.lexically_normal();
    r.erase(std::remove(r.begin(), r.end(), n), r.end());
    r.insert(r.begin(), n);
    if (r.size() > kMaxRecentFolders) r.resize(kMaxRecentFolders);
}

std::string settings_to_json(const AppSettings& s) {
    json j;
    j["format"] = "yantra-app-settings/1";
    j["recent_projects"] = json::array();
    for (const fs::path& p : s.recent_projects) j["recent_projects"].push_back(p.string());
    j["recent_folders"] = json::array();
    for (const fs::path& p : s.recent_folders) j["recent_folders"].push_back(p.string());
    j["window_system"] = s.window_system;
    j["tooltips"] = s.tooltips;
    j["tooltip_delay_s"] = s.tooltip_delay_s;
    return j.dump(1) + "\n";
}

AppSettings settings_from_json(const std::string& text, std::string& error) {
    AppSettings s;
    const json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        error = "the settings file is not a JSON object; defaults used";
        return s;
    }
    if (const auto it = j.find("recent_projects"); it != j.end() && it->is_array()) {
        for (auto e = it->rbegin(); e != it->rend(); ++e)
            if (e->is_string() && !e->get<std::string>().empty()) add_recent_project(s, e->get<std::string>());
    }
    if (const auto it = j.find("recent_folders"); it != j.end() && it->is_array()) {
        for (auto e = it->rbegin(); e != it->rend(); ++e)
            if (e->is_string() && !e->get<std::string>().empty()) add_recent_folder(s, e->get<std::string>());
    }
    if (const auto it = j.find("window_system"); it != j.end() && it->is_string() && parse_window_system(it->get<std::string>()))
        s.window_system = to_string(*parse_window_system(it->get<std::string>()));
    if (const auto it = j.find("tooltips"); it != j.end() && it->is_boolean()) s.tooltips = it->get<bool>();
    if (const auto it = j.find("tooltip_delay_s"); it != j.end() && it->is_number()) {
        const double d = it->get<double>();
        if (d >= 0.0 && d <= 5.0) s.tooltip_delay_s = d;
    }
    return s;
}

fs::path config_folder(const std::string& xdg_config_home, const std::string& home) {
    if (!xdg_config_home.empty() && fs::path(xdg_config_home).is_absolute()) return fs::path(xdg_config_home) / "yantra";
    if (!home.empty() && fs::path(home).is_absolute()) return fs::path(home) / ".config" / "yantra";
    return {};
}

fs::path config_folder() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const char* h = std::getenv("HOME");
    return config_folder(x != nullptr ? x : "", h != nullptr ? h : "");
}

AppSettings load_settings(const fs::path& file, std::string& error) {
    std::ifstream in(file);
    if (!in) return AppSettings{};
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return settings_from_json(text, error);
}

bool save_settings(const fs::path& file, const AppSettings& settings, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        error = "cannot create " + file.parent_path().string() + ": " + ec.message();
        return false;
    }
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << settings_to_json(settings);
        out.flush();
        if (!out) {
            error = "cannot write " + tmp.string();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        error = "cannot replace " + file.string() + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

}  // namespace yantra::app
