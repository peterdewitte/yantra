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

const char* layer_key(Layer layer) {
    switch (layer) {
    case Layer::Plan: return "plan";
    case Layer::Interpolation: return "interpolation";
    case Layer::Binned: return "binned";
    case Layer::Walls: return "walls";
    case Layer::Antennas: return "antennas";
    case Layer::Points: return "points";
    }
    return "?";
}

std::optional<Layer> parse_layer(std::string_view key) {
    for (Layer l : kLayers)
        if (key == layer_key(l)) return l;
    return std::nullopt;
}

bool LayerSettings::visible(Layer layer) const {
    return std::find(hidden.begin(), hidden.end(), layer) == hidden.end();
}

void LayerSettings::set_visible(Layer layer, bool on) {
    hidden.erase(std::remove(hidden.begin(), hidden.end(), layer), hidden.end());
    if (!on) hidden.push_back(layer);
}

std::size_t LayerSettings::position(Layer layer) const {
    return static_cast<std::size_t>(std::find(order.begin(), order.end(), layer) - order.begin());
}

bool LayerSettings::move(Layer layer, int steps) {
    const std::size_t i = position(layer);
    if (i >= order.size() || steps == 0) return false;
    const long to = static_cast<long>(i) + (steps > 0 ? 1 : -1);
    if (to < 0 || to >= static_cast<long>(order.size())) return false;
    std::swap(order[i], order[static_cast<std::size_t>(to)]);
    return true;
}

std::string settings_to_json(const AppSettings& s) {
    json j;
    j["format"] = "yantra-app-settings/1";
    j["recent_projects"] = json::array();
    for (const fs::path& p : s.recent_projects) j["recent_projects"].push_back(p.string());
    j["recent_folders"] = json::array();
    for (const fs::path& p : s.recent_folders) j["recent_folders"].push_back(p.string());
    j["layers"] = json::object();
    j["layers"]["order"] = json::array();
    for (Layer l : s.layers.order) j["layers"]["order"].push_back(layer_key(l));
    j["layers"]["hidden"] = json::array();
    for (Layer l : s.layers.hidden) j["layers"]["hidden"].push_back(layer_key(l));
    j["layers"]["bin_size_m"] = s.layers.bin_size_m;
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
    if (const auto it = j.find("layers"); it != j.end() && it->is_object()) {
        // The order keeps every layer once: known keys in the stored
        // order, then the layers it does not name in their default place.
        if (const auto o = it->find("order"); o != it->end() && o->is_array()) {
            std::vector<Layer> order;
            for (const json& e : *o) {
                const auto l = e.is_string() ? parse_layer(e.get<std::string>()) : std::nullopt;
                if (l && std::find(order.begin(), order.end(), *l) == order.end()) order.push_back(*l);
            }
            for (std::size_t k = 0; k < std::size(kLayers); ++k)
                if (std::find(order.begin(), order.end(), kLayers[k]) == order.end())
                    order.insert(order.begin() + static_cast<long>(std::min(k, order.size())), kLayers[k]);
            s.layers.order = order;
        }
        if (const auto h = it->find("hidden"); h != it->end() && h->is_array()) {
            s.layers.hidden.clear();
            for (const json& e : *h) {
                const auto l = e.is_string() ? parse_layer(e.get<std::string>()) : std::nullopt;
                if (l) s.layers.set_visible(*l, false);
            }
        }
        if (const auto b = it->find("bin_size_m"); b != it->end() && b->is_number()) {
            const double v = b->get<double>();
            if (v >= 0.5 && v <= 50.0) s.layers.bin_size_m = v;
        }
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
