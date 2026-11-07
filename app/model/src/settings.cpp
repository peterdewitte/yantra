// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// App settings, see settings.h.
// Copied from yantra-v2 68ea20a: app/model/src/settings.cpp, adapted.
#include "yantra/app/settings.h"

#include "yantra/app/i18n.h"

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

const char* theme_key(Theme theme) {
    switch (theme) {
    case Theme::Light: return "light";
    case Theme::HighContrast: return "high_contrast";
    case Theme::Dark: break;
    }
    return "dark";
}

std::optional<Theme> parse_theme(std::string_view key) {
    for (Theme t : kThemes)
        if (key == theme_key(t)) return t;
    return std::nullopt;
}

namespace {

bool within(double v, double lo, double hi) { return v >= lo && v <= hi; }

}  // namespace

std::string check_settings(const AppSettings& s) {
    if (!parse_window_system(s.window_system)) return "The window system is auto, x11 or wayland.";
    if (!within(s.tooltip_delay_s, 0.0, kMaxTooltipDelayS)) return "The tooltip delay is 0 to 5 seconds.";
    if (!within(s.layers.bin_size_m, 0.5, 50.0)) return "The binned view's squares are 0.5 to 50 m.";
    if (!within(s.survey.threshold_dbm, kMinThresholdDbm, kMaxThresholdDbm))
        return "The coverage threshold is -150 to -20 dBm.";
    if (!within(s.survey.graph_span_min, kMinGraphSpanMin, kMaxGraphSpanMin))
        return "The graphs show 1 to 60 minutes.";
    if (!within(s.survey.ring_minutes, kMinRingMinutes, kMaxRingMinutes)) return "The ring buffer keeps 2 to 60 minutes.";
    if (!parse_language(s.language)) return "The language is en, nl, fr or de.";
    if (!within(s.ui_scale, kMinUiScale, kMaxUiScale)) return "The UI scale is 75 to 250 %.";
    if (!within(s.sound.volume, 0.0, 1.0)) return "The volume is 0 to 100 %.";
    if (!within(s.sound.level_interval_s, kMinLevelIntervalS, kMaxLevelIntervalS))
        return "The level tone comes every 2 to 120 seconds.";
    if (s.sound.alerts.size() > kMaxLevelAlerts) return "At most 8 level alerts.";
    for (const LevelAlert& a : s.sound.alerts) {
        if (a.cell.empty()) return "Every level alert needs a cell.";
        if (!within(a.threshold_dbm, kMinThresholdDbm, kMaxThresholdDbm))
            return "A level alert's threshold is -150 to -20 dBm.";
    }
    return {};
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
    j["theme"] = theme_key(s.theme);
    j["language"] = s.language;
    j["touch"] = s.touch;
    j["ui_scale"] = s.ui_scale;
    j["sound"] = json::object();
    j["sound"]["enabled"] = s.sound.enabled;
    j["sound"]["volume"] = s.sound.volume;
    j["sound"]["level_tone"] = s.sound.level_tone;
    j["sound"]["level_interval_s"] = s.sound.level_interval_s;
    j["sound"]["sync_loss"] = s.sound.sync_loss;
    j["sound"]["reselection"] = s.sound.reselection;
    j["sound"]["low_battery"] = s.sound.low_battery;
    j["sound"]["alert_popup"] = s.sound.alert_popup;
    j["sound"]["alerts"] = json::array();
    for (const LevelAlert& a : s.sound.alerts)
        j["sound"]["alerts"].push_back({{"cell", a.cell}, {"threshold_dbm", a.threshold_dbm}});
    j["survey"] = json::object();
    j["survey"]["threshold_dbm"] = s.survey.threshold_dbm;
    j["survey"]["graph_span_min"] = s.survey.graph_span_min;
    j["survey"]["ring"] = s.survey.ring;
    j["survey"]["ring_minutes"] = s.survey.ring_minutes;
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
        if (d >= 0.0 && d <= kMaxTooltipDelayS) s.tooltip_delay_s = d;
    }
    if (const auto it = j.find("theme"); it != j.end() && it->is_string())
        if (const auto t = parse_theme(it->get<std::string>())) s.theme = *t;
    if (const auto it = j.find("language"); it != j.end() && it->is_string() && parse_language(it->get<std::string>()))
        s.language = it->get<std::string>();
    if (const auto it = j.find("touch"); it != j.end() && it->is_boolean()) s.touch = it->get<bool>();
    if (const auto it = j.find("ui_scale"); it != j.end() && it->is_number() &&
                                            within(it->get<double>(), kMinUiScale, kMaxUiScale))
        s.ui_scale = it->get<double>();
    if (const auto it = j.find("sound"); it != j.end() && it->is_object()) {
        auto flag = [&](const char* key, bool& out) {
            if (const auto v = it->find(key); v != it->end() && v->is_boolean()) out = v->get<bool>();
        };
        auto number = [&](const char* key, double lo, double hi, double& out) {
            if (const auto v = it->find(key); v != it->end() && v->is_number() && within(v->get<double>(), lo, hi))
                out = v->get<double>();
        };
        flag("enabled", s.sound.enabled);
        number("volume", 0.0, 1.0, s.sound.volume);
        flag("level_tone", s.sound.level_tone);
        number("level_interval_s", kMinLevelIntervalS, kMaxLevelIntervalS, s.sound.level_interval_s);
        flag("sync_loss", s.sound.sync_loss);
        flag("reselection", s.sound.reselection);
        flag("low_battery", s.sound.low_battery);
        flag("alert_popup", s.sound.alert_popup);
        if (const auto a = it->find("alerts"); a != it->end() && a->is_array()) {
            for (const auto& e : *a) {
                if (!e.is_object() || s.sound.alerts.size() >= kMaxLevelAlerts) continue;
                LevelAlert x;
                if (const auto c = e.find("cell"); c != e.end() && c->is_string()) x.cell = c->get<std::string>();
                if (const auto t = e.find("threshold_dbm"); t != e.end() && t->is_number()) x.threshold_dbm = t->get<double>();
                if (!x.cell.empty() && within(x.threshold_dbm, kMinThresholdDbm, kMaxThresholdDbm))
                    s.sound.alerts.push_back(x);
            }
        }
    }
    if (const auto it = j.find("survey"); it != j.end() && it->is_object()) {
        auto number = [&](const char* key, double lo, double hi, double& out) {
            if (const auto v = it->find(key); v != it->end() && v->is_number() && within(v->get<double>(), lo, hi))
                out = v->get<double>();
        };
        number("threshold_dbm", kMinThresholdDbm, kMaxThresholdDbm, s.survey.threshold_dbm);
        number("graph_span_min", kMinGraphSpanMin, kMaxGraphSpanMin, s.survey.graph_span_min);
        number("ring_minutes", kMinRingMinutes, kMaxRingMinutes, s.survey.ring_minutes);
        if (const auto v = it->find("ring"); v != it->end() && v->is_boolean()) s.survey.ring = v->get<bool>();
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
