// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario files (RAD-74); see scenario.hpp and docs/SCENARIO_FORMAT.md.
// cell_config() copied from yantra 68ea20a
// (plugins/tetra/scenario/src/scenario.cpp), the rest written for the
// indoor format.

#include "yantra/gen/scenario.hpp"

#include "yantra/gen/cell_tx.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace yantra::gen {

namespace {

using json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& where, const std::string& what)
{
    throw ScenarioError(where + ": " + what);
}

std::string at(const std::string& base, const char* key) { return base.empty() ? key : base + "." + key; }
std::string at(const std::string& base, size_t i) { return base + "[" + std::to_string(i) + "]"; }

void object_check(const json& j, const std::string& where)
{
    if (!j.is_object())
        fail(where.empty() ? "scenario" : where, "must be an object");
}

const json* member(const json& j, const char* key)
{
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

const json& need(const json& j, const char* key, const std::string& where)
{
    const json* v = member(j, key);
    if (!v)
        fail(where.empty() ? "scenario" : where, std::string("member \"") + key + "\" missing");
    return *v;
}

double number(const json& v, const std::string& where)
{
    if (!v.is_number())
        fail(where, "must be a number");
    const double d = v.get<double>();
    if (!std::isfinite(d))
        fail(where, "must be finite");
    return d;
}

double num(const json& j, const char* key, const std::string& where, double fallback)
{
    const json* v = member(j, key);
    return v ? number(*v, at(where, key)) : fallback;
}

std::optional<double> opt_num(const json& j, const char* key, const std::string& where)
{
    const json* v = member(j, key);
    if (!v || v->is_null())
        return std::nullopt;
    return number(*v, at(where, key));
}

int64_t integer(const json& j, const char* key, const std::string& where, int64_t fallback, int64_t lo, int64_t hi)
{
    const json* v = member(j, key);
    if (!v)
        return fallback;
    if (!v->is_number_integer())
        fail(at(where, key), "must be an integer");
    const int64_t i = v->is_number_unsigned() && v->get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)
                          ? INT64_MAX
                          : v->get<int64_t>();
    if (i < lo || i > hi)
        fail(at(where, key), "must be from " + std::to_string(lo) + " to " + std::to_string(hi));
    return i;
}

uint64_t unsigned64(const json& j, const char* key, const std::string& where, uint64_t fallback)
{
    const json* v = member(j, key);
    if (!v)
        return fallback;
    if (!v->is_number_unsigned() && !(v->is_number_integer() && v->get<int64_t>() >= 0))
        fail(at(where, key), "must be an unsigned integer");
    return v->get<uint64_t>();
}

bool boolean(const json& j, const char* key, const std::string& where, bool fallback)
{
    const json* v = member(j, key);
    if (!v)
        return fallback;
    if (!v->is_boolean())
        fail(at(where, key), "must be true or false");
    return v->get<bool>();
}

std::string text(const json& j, const char* key, const std::string& where, bool required)
{
    const json* v = member(j, key);
    if (!v) {
        if (required)
            fail(where.empty() ? "scenario" : where, std::string("member \"") + key + "\" missing");
        return {};
    }
    if (!v->is_string())
        fail(at(where, key), "must be a string");
    return v->get<std::string>();
}

const json& array(const json& j, const char* key, const std::string& where)
{
    static const json empty = json::array();
    const json* v = member(j, key);
    if (!v)
        return empty;
    if (!v->is_array())
        fail(at(where, key), "must be an array");
    return *v;
}

const json& object(const json& j, const char* key, const std::string& where)
{
    static const json empty = json::object();
    const json* v = member(j, key);
    if (!v)
        return empty;
    if (!v->is_object())
        fail(at(where, key), "must be an object");
    return *v;
}

radios::ScenePattern site_pattern(const json& j, const std::string& where)
{
    object_check(j, where);
    radios::ScenePattern p;
    const std::string type = text(j, "type", where, true);
    if (type == "omni")
        return p;
    if (type != "sector")
        fail(at(where, "type"), "unknown pattern \"" + type + "\" (omni or sector)");
    p.type = radios::ScenePattern::Type::sector;
    p.azimuth_deg = num(j, "azimuth_deg", where, 0.0);
    p.beamwidth_deg = num(j, "beamwidth_deg", where, 65.0);
    p.front_to_back_db = num(j, "front_to_back_db", where, 25.0);
    if (!(p.azimuth_deg >= 0.0 && p.azimuth_deg < 360.0))
        fail(at(where, "azimuth_deg"), "must be in [0, 360)");
    if (!(p.beamwidth_deg > 0.0 && p.beamwidth_deg <= 360.0))
        fail(at(where, "beamwidth_deg"), "must be above 0 and at most 360");
    if (p.front_to_back_db < 0.0)
        fail(at(where, "front_to_back_db"), "must not be negative");
    return p;
}

ScenarioCell parse_cell(const json& j, const std::string& w)
{
    object_check(j, w);
    ScenarioCell c;
    c.id = text(j, "id", w, true);
    c.label = text(j, "label", w, false);
    c.cell_number = static_cast<uint8_t>(integer(j, "cell_number", w, 1, 0, 31));
    c.location_area = static_cast<uint16_t>(integer(j, "la", w, 1, 0, 16383));
    c.colour_code = static_cast<uint8_t>(integer(j, "colour_code", w, 1, 0, 63));
    c.main_carrier = static_cast<uint16_t>(integer(j, "main_carrier", w, 3600, 0, 4095));
    c.frequency_band = static_cast<uint8_t>(integer(j, "frequency_band", w, 3, 0, 15));
    c.offset = static_cast<uint8_t>(integer(j, "offset", w, 0, 0, 3));
    c.encryption = boolean(j, "encryption", w, false);
    c.rxlev_access_min = static_cast<uint8_t>(integer(j, "rxlev_access_min", w, 2, 0, 15));
    c.ms_txpwr_max_cell = static_cast<uint8_t>(integer(j, "ms_txpwr_max_cell", w, 5, 0, 7));
    const json& rs = object(j, "reselect", w);
    const std::string rw = at(w, "reselect");
    c.reselect.slow_threshold = static_cast<uint8_t>(integer(rs, "slow_threshold", rw, 2, 0, 15));
    c.reselect.slow_hysteresis = static_cast<uint8_t>(integer(rs, "slow_hysteresis", rw, 2, 0, 15));
    c.reselect.fast_threshold = static_cast<uint8_t>(integer(rs, "fast_threshold", rw, 4, 0, 15));
    c.reselect.fast_hysteresis = static_cast<uint8_t>(integer(rs, "fast_hysteresis", rw, 2, 0, 15));
    c.network_time = boolean(j, "network_time", w, true);
    const json& ns = array(j, "neighbours", w);
    for (size_t i = 0; i < ns.size(); ++i) {
        const std::string nw = at(at(w, "neighbours"), i);
        object_check(ns[i], nw);
        ScenarioNeighbour n;
        n.cell = text(ns[i], "cell", nw, true);
        if (member(ns[i], "main_carrier"))
            n.main_carrier = static_cast<uint16_t>(integer(ns[i], "main_carrier", nw, 0, 0, 4095));
        if (member(ns[i], "la"))
            n.location_area = static_cast<uint16_t>(integer(ns[i], "la", nw, 0, 0, 16383));
        c.neighbours.push_back(std::move(n));
    }
    c.level_dbm = num(j, "level_dbm", w, -70.0);
    if (const json* s = member(j, "site")) {
        const std::string sw = at(w, "site");
        object_check(*s, sw);
        ScenarioSite site;
        site.x_m = number(need(*s, "x_m", sw), at(sw, "x_m"));
        site.y_m = number(need(*s, "y_m", sw), at(sw, "y_m"));
        site.height_m = num(*s, "height_m", sw, 30.0);
        site.eirp_dbm = num(*s, "eirp_dbm", sw, 45.0);
        if (const json* p = member(*s, "pattern"))
            site.pattern = site_pattern(*p, at(sw, "pattern"));
        c.site = site;
    }
    c.ppm = num(j, "ppm", w, 0.0);
    c.time_offset_s = num(j, "time_offset_s", w, 0.0);
    c.on_air = boolean(j, "on_air", w, true);
    return c;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw ScenarioError("cannot read " + path);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

radios::Scene scene_from_text(const std::string& text, const std::string& where)
{
    radios::Scene scene;
    std::string error;
    if (!radios::parse_scene(text, scene, error))
        fail(where, error);
    return scene;
}

Scenario parse(const json& root, const ScenarioFileReader& read_scene)
{
    object_check(root, "");
    const json* f = member(root, "format");
    if (!f || !f->is_string() || f->get<std::string>() != kScenarioFormat)
        throw ScenarioError(std::string("not a scenario (format is not \"") + kScenarioFormat + "\")");
    const json v = need(root, "version", "scenario");
    if (!v.is_number_integer())
        fail("version", "must be an integer");
    const int64_t version = v.get<int64_t>();
    if (version < 1)
        fail("version", "invalid version " + std::to_string(version));
    if (version > static_cast<int64_t>(kScenarioVersion))
        throw ScenarioError("scenario version " + std::to_string(version) + " is newer than this program reads (" +
                            std::to_string(kScenarioVersion) + ")");
    Scenario s;
    s.version = static_cast<uint32_t>(version);
    s.id = text(root, "id", "", true);
    s.name = text(root, "name", "", false);
    s.description = text(root, "description", "", false);
    const json& ex = array(root, "example_ids", "");
    for (size_t i = 0; i < ex.size(); ++i) {
        if (!ex[i].is_string())
            fail(at("example_ids", i), "must be a string");
        s.example_ids.push_back(ex[i].get<std::string>());
    }
    s.is_default = boolean(root, "default", "", false);
    s.seed = unsigned64(root, "seed", "", 1);
    s.duration_s = num(root, "duration_s", "", 10.0);
    s.truth_interval_s = num(root, "truth_interval_s", "", 0.1);
    s.sample_rate_hz = num(root, "sample_rate_hz", "", 500e3);
    s.centre_frequency_hz = opt_num(root, "centre_frequency_hz", "");

    const json& net = object(root, "network", "");
    s.mcc = static_cast<uint16_t>(integer(net, "mcc", "network", 901, 0, 1023));
    s.mnc = static_cast<uint16_t>(integer(net, "mnc", "network", 1, 0, 16383));

    const json& cells = array(root, "cells", "");
    for (size_t i = 0; i < cells.size(); ++i)
        s.cells.push_back(parse_cell(cells[i], at("cells", i)));

    const json& floors = array(root, "floors", "");
    for (size_t i = 0; i < floors.size(); ++i) {
        const std::string w = at("floors", i);
        object_check(floors[i], w);
        ScenarioFloor fl;
        const json* inline_scene = member(floors[i], "scene");
        const json* file = member(floors[i], "scene_file");
        if ((inline_scene != nullptr) == (file != nullptr))
            fail(w, "needs either \"scene\" or \"scene_file\"");
        if (file) {
            if (!file->is_string() || file->get<std::string>().empty())
                fail(at(w, "scene_file"), "must be a file name");
            fl.file = file->get<std::string>();
            std::string body;
            try {
                body = read_scene(fl.file);
            } catch (const ScenarioError&) {
                fail(at(w, "scene_file"), "cannot read \"" + fl.file + "\"");
            }
            fl.scene = scene_from_text(body, at(w, "scene_file") + " \"" + fl.file + "\"");
        } else {
            fl.scene = scene_from_text(inline_scene->dump(), at(w, "scene"));
        }
        s.floors.push_back(std::move(fl));
    }

    const json& ants = array(root, "antennas", "");
    for (size_t i = 0; i < ants.size(); ++i) {
        const std::string w = at("antennas", i);
        object_check(ants[i], w);
        s.antennas.push_back({text(ants[i], "id", w, true), number(need(ants[i], "eirp_dbm", w), at(w, "eirp_dbm"))});
    }

    const json& ch = object(root, "channel", "");
    {
        const json& pl = object(ch, "path_loss", "channel");
        s.channel.path_loss.exponent = num(pl, "exponent", "channel.path_loss", 3.5);
        s.channel.path_loss.reference_distance_m = num(pl, "reference_distance_m", "channel.path_loss", 1.0);
        s.channel.path_loss.reference_loss_db = opt_num(pl, "reference_loss_db", "channel.path_loss");
        s.channel.penetration_loss_db = num(ch, "penetration_loss_db", "channel", 12.0);
        const json& sh = object(ch, "shadowing", "channel");
        s.channel.shadowing_sigma_db = num(sh, "sigma_db", "channel.shadowing", 0.0);
        s.channel.shadowing_decorrelation_m = num(sh, "decorrelation_m", "channel.shadowing", 20.0);
        const json& mw = object(ch, "multiwall", "channel");
        const std::string mww = "channel.multiwall";
        s.channel.multiwall.reflections = static_cast<int>(integer(mw, "reflections", mww, 0, 0, 2));
        s.channel.multiwall.reflection_loss_db = num(mw, "reflection_loss_db", mww, 6.0);
        s.channel.multiwall.slab_attenuation_db = num(mw, "slab_attenuation_db", mww, 15.0);
        s.channel.multiwall.storey_height_m = num(mw, "storey_height_m", mww, 3.0);
        s.channel.multiwall.shadowing_sigma_db = num(mw, "shadowing_sigma_db", mww, 0.0);
        s.channel.multiwall.shadowing_decorrelation_m = num(mw, "shadowing_decorrelation_m", mww, 4.0);
        const json& fd = object(ch, "fading", "channel");
        const std::string fw = "channel.fading";
        const std::string model = text(fd, "model", fw, false);
        if (!model.empty()) {
            try {
                s.channel.fading = fading_model_from_string(model);
            } catch (const std::invalid_argument& e) {
                fail(at(fw, "model"), e.what());
            }
        }
        s.channel.rician_k_db = num(fd, "k_db", fw, 6.0);
        s.channel.sinusoids = static_cast<int>(integer(fd, "sinusoids", fw, 32, 1, 4096));
        s.channel.doppler_hz = opt_num(fd, "doppler_hz", fw);
        s.channel.min_doppler_hz = num(fd, "min_doppler_hz", fw, 1.0);
        s.channel.doppler_shift_hz = num(ch, "doppler_shift_hz", "channel", 0.0);
    }

    const json& rx = object(root, "receiver", "");
    {
        const std::string w = "receiver";
        s.receiver.noise_figure_db = num(rx, "noise_figure_db", w, 7.0);
        s.receiver.noise = boolean(rx, "noise", w, true);
        s.receiver.reference_level_dbm = num(rx, "reference_level_dbm", w, -20.0);
        s.receiver.height_m = num(rx, "height_m", w, 1.5);
        s.receiver.ppm = num(rx, "ppm", w, 0.0);
        s.receiver.clock_ppm = num(rx, "clock_ppm", w, 0.0);
        s.receiver.gain_db = num(rx, "gain_db", w, 0.0);
        const json& iq = object(rx, "iq_imbalance", w);
        s.receiver.iq_gain_db = num(iq, "gain_db", "receiver.iq_imbalance", 0.0);
        s.receiver.iq_phase_deg = num(iq, "phase_deg", "receiver.iq_imbalance", 0.0);
        s.receiver.swap_iq = boolean(rx, "swap_iq", w, false);
        s.receiver.dc_dbfs = opt_num(rx, "dc_dbfs", w);
        s.receiver.full_scale_dbm = opt_num(rx, "full_scale_dbm", w);
        s.receiver.clip = boolean(rx, "clip", w, false);
        s.receiver.quantise_bits = static_cast<int>(integer(rx, "quantise_bits", w, 0, 0, 16));
        const json& dr = object(rx, "drops", w);
        s.receiver.drops_first_s = num(dr, "first_s", "receiver.drops", 0.0);
        s.receiver.drops_interval_s = num(dr, "interval_s", "receiver.drops", 0.0);
        s.receiver.drops_samples = unsigned64(dr, "samples", "receiver.drops", 0);
    }

    const json& path = object(root, "path", "");
    {
        const std::string mode = text(path, "mode", "path", false);
        if (mode.empty() || mode == "hold")
            s.path.linear = false;
        else if (mode == "linear")
            s.path.linear = true;
        else
            fail("path.mode", "unknown mode \"" + mode + "\" (hold or linear)");
        const json& pts = array(path, "points", "path");
        for (size_t i = 0; i < pts.size(); ++i) {
            const std::string w = at("path.points", i);
            object_check(pts[i], w);
            PathPoint p;
            p.t = num(pts[i], "t", w, 0.0);
            p.floor = text(pts[i], "floor", w, false);
            p.x_m = number(need(pts[i], "x_m", w), at(w, "x_m"));
            p.y_m = number(need(pts[i], "y_m", w), at(w, "y_m"));
            s.path.points.push_back(std::move(p));
        }
    }

    const json& events = array(root, "events", "");
    for (size_t i = 0; i < events.size(); ++i) {
        const std::string w = at("events", i);
        object_check(events[i], w);
        ScenarioEvent e;
        e.t = number(need(events[i], "t", w), at(w, "t"));
        const std::string type = text(events[i], "type", w, true);
        if (type == "cell_off")
            e.type = EventType::cell_off;
        else if (type == "cell_on")
            e.type = EventType::cell_on;
        else if (type == "cell_level")
            e.type = EventType::cell_level;
        else if (type == "rx_gain")
            e.type = EventType::rx_gain;
        else
            fail(at(w, "type"), "unknown event \"" + type + "\" (cell_off, cell_on, cell_level, rx_gain)");
        if (e.type != EventType::rx_gain)
            e.cell = text(events[i], "cell", w, true);
        if (e.type == EventType::cell_level || e.type == EventType::rx_gain)
            e.db = number(need(events[i], "db", w), at(w, "db"));
        s.events.push_back(std::move(e));
    }

    const json& planted = array(root, "planted", "");
    for (size_t i = 0; i < planted.size(); ++i) {
        if (!planted[i].is_string())
            fail(at("planted", i), "must be a string");
        s.planted.push_back(planted[i].get<std::string>());
    }

    validate_scenario(s);
    return s;
}

}  // namespace

FadingModel fading_model_from_string(const std::string& s)
{
    std::string n = s;
    for (char& c : n)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n == "none")
        return FadingModel::none;
    if (n == "rayleigh")
        return FadingModel::rayleigh;
    if (n == "rician")
        return FadingModel::rician;
    if (n == "static")
        return FadingModel::static_channel;
    if (n == "tu50")
        return FadingModel::tu50;
    if (n == "ht200")
        return FadingModel::ht200;
    throw std::invalid_argument("unknown fading model \"" + s + "\" (none, rayleigh, rician, static, TU50, HT200)");
}

const char* to_string(FadingModel m)
{
    switch (m) {
    case FadingModel::none:
        return "none";
    case FadingModel::rayleigh:
        return "rayleigh";
    case FadingModel::rician:
        return "rician";
    case FadingModel::static_channel:
        return "static";
    case FadingModel::tu50:
        return "TU50";
    case FadingModel::ht200:
        return "HT200";
    }
    return "none";
}

const ScenarioCell* Scenario::find_cell(std::string_view cell_id) const
{
    for (const ScenarioCell& c : cells)
        if (c.id == cell_id)
            return &c;
    return nullptr;
}

const radios::Scene* Scenario::find_scene(std::string_view floor_id) const
{
    for (const ScenarioFloor& f : floors)
        if (f.scene.floor.id == floor_id)
            return &f.scene;
    return nullptr;
}

Scenario parse_scenario(std::string_view json_text, const std::string& base_dir)
{
    json root;
    try {
        root = json::parse(json_text.begin(), json_text.end());
    } catch (const json::exception& e) {
        throw ScenarioError(std::string("not valid JSON: ") + e.what());
    }
    return parse(root, [&base_dir](const std::string& file) {
        std::filesystem::path p(file);
        if (p.is_relative() && !base_dir.empty())
            p = std::filesystem::path(base_dir) / p;
        return read_file(p.string());
    });
}

Scenario parse_scenario(std::string_view json_text, const ScenarioFileReader& read_scene)
{
    json root;
    try {
        root = json::parse(json_text.begin(), json_text.end());
    } catch (const json::exception& e) {
        throw ScenarioError(std::string("not valid JSON: ") + e.what());
    }
    return parse(root, read_scene);
}

Scenario load_scenario_file(const std::string& path)
{
    const std::string body = read_file(path);
    return parse_scenario(body, std::filesystem::path(path).parent_path().string());
}

double cell_frequency_hz(const ScenarioCell& c)
{
    return downlink_frequency_hz(c.frequency_band, c.main_carrier, c.offset);
}

double centre_frequency_hz(const Scenario& s)
{
    if (s.centre_frequency_hz)
        return *s.centre_frequency_hz;
    if (s.cells.empty())
        return 0.0;
    double lo = cell_frequency_hz(s.cells.front()), hi = lo;
    for (const ScenarioCell& c : s.cells) {
        lo = std::min(lo, cell_frequency_hz(c));
        hi = std::max(hi, cell_frequency_hz(c));
    }
    return (lo + hi) / 2.0;
}

void validate_scenario(const Scenario& s)
{
    if (s.id.empty())
        fail("id", "must not be empty");
    if (!(s.duration_s >= 0.0) || s.duration_s > 24 * 3600.0)
        fail("duration_s", "must be from 0 (endless) to 86400");
    if (!(s.truth_interval_s >= 0.001) || s.truth_interval_s > 3600.0)
        fail("truth_interval_s", "must be from 0.001 to 3600");
    if (!(s.sample_rate_hz >= 36e3) || s.sample_rate_hz > 20e6)
        fail("sample_rate_hz", "must be from 36000 to 20000000");
    if (s.cells.empty())
        fail("cells", "needs at least one cell");

    const double centre = centre_frequency_hz(s);
    const double fs_true = s.sample_rate_hz * (1.0 + s.receiver.clock_ppm * 1e-6);
    std::set<std::string> ids;
    for (size_t i = 0; i < s.cells.size(); ++i) {
        const ScenarioCell& c = s.cells[i];
        const std::string w = at("cells", i);
        if (c.id.empty())
            fail(at(w, "id"), "must not be empty");
        if (!ids.insert(c.id).second)
            fail(at(w, "id"), "\"" + c.id + "\" repeated");
        if (c.cell_number > 31)
            fail(at(w, "cell_number"), "must be from 0 to 31");
        if (!(c.time_offset_s >= 0.0) || c.time_offset_s >= 61.2)
            fail(at(w, "time_offset_s"), "must be from 0 to below 61.2 (one hyperframe)");
        if (std::fabs(c.ppm) > 1000.0)
            fail(at(w, "ppm"), "must be within +/-1000");
        if (c.neighbours.size() > 31)
            fail(at(w, "neighbours"), "at most 31 neighbours");
        if (c.site && !(c.site->height_m >= 0.0))
            fail(at(at(w, "site"), "height_m"), "must not be negative");
        double f = 0.0;
        try {
            f = cell_frequency_hz(c);
        } catch (const std::exception& e) {
            fail(w, e.what());
        }
        const double offset = f - centre + c.ppm * 1e-6 * f - s.receiver.ppm * 1e-6 * centre +
                              s.channel.doppler_shift_hz;
        if (!(std::fabs(offset) + kHalfOccupiedBandwidthHz < fs_true / 2.0))
            fail(w, "carrier at " + std::to_string(f) + " Hz does not fit the span of " +
                        std::to_string(s.sample_rate_hz) + " sps around " + std::to_string(centre) + " Hz");
    }
    for (size_t i = 0; i < s.cells.size(); ++i)
        for (size_t k = 0; k < s.cells[i].neighbours.size(); ++k) {
            const std::string& n = s.cells[i].neighbours[k].cell;
            if (!s.find_cell(n))
                fail(at(at(at("cells", i), "neighbours"), k) + ".cell", "unknown cell \"" + n + "\"");
            if (n == s.cells[i].id)
                fail(at(at(at("cells", i), "neighbours"), k) + ".cell", "a cell is not its own neighbour");
        }

    std::set<std::string> floors;
    for (size_t i = 0; i < s.floors.size(); ++i) {
        const radios::Scene& sc = s.floors[i].scene;
        const std::string w = at("floors", i);
        if (!floors.insert(sc.floor.id).second)
            fail(w, "floor \"" + sc.floor.id + "\" repeated");
        if (!sc.floor.scale_set)
            fail(w, "floor \"" + sc.floor.id + "\" has no scale");
        // A scene cell that the scenario has must be the same cell.
        for (const radios::SceneCell& sc_cell : sc.cells) {
            const ScenarioCell* c = s.find_cell(sc_cell.id);
            if (!c)
                fail(w, "scene cell \"" + sc_cell.id + "\" is not a scenario cell");
            if (sc_cell.mcc != s.mcc || sc_cell.mnc != s.mnc || sc_cell.la != c->location_area ||
                sc_cell.colour_code != c->colour_code ||
                std::llround(cell_frequency_hz(*c)) != static_cast<long long>(sc_cell.carrier_hz))
                fail(w, "scene cell \"" + sc_cell.id + "\" differs from the scenario cell (MCC, MNC, LA, colour code "
                        "or carrier)");
        }
    }
    for (size_t i = 0; i < s.antennas.size(); ++i)
        if (s.antennas[i].id.empty())
            fail(at(at("antennas", i), "id"), "must not be empty");

    const ChannelSpec& ch = s.channel;
    if (!(ch.path_loss.exponent > 0.0) || ch.path_loss.exponent > 10.0)
        fail("channel.path_loss.exponent", "must be above 0 and at most 10");
    if (!(ch.path_loss.reference_distance_m > 0.0))
        fail("channel.path_loss.reference_distance_m", "must be above 0");
    if (!(ch.penetration_loss_db >= 0.0))
        fail("channel.penetration_loss_db", "must not be negative");
    if (!(ch.shadowing_sigma_db >= 0.0) || !(ch.shadowing_decorrelation_m > 0.0))
        fail("channel.shadowing", "needs sigma_db >= 0 and decorrelation_m > 0");
    try {
        MultiWallModel check(ch.multiwall);
    } catch (const std::invalid_argument&) {
        fail("channel.multiwall", "parameter out of range");
    }
    if (ch.sinusoids < 1 || ch.sinusoids > 4096)
        fail("channel.fading.sinusoids", "must be from 1 to 4096");
    if (ch.doppler_hz && !(*ch.doppler_hz >= 0.0))
        fail("channel.fading.doppler_hz", "must not be negative");
    if (!(ch.min_doppler_hz >= 0.0))
        fail("channel.fading.min_doppler_hz", "must not be negative");

    const ReceiverSpec& rx = s.receiver;
    if (!(rx.noise_figure_db >= 0.0) || rx.noise_figure_db > 60.0)
        fail("receiver.noise_figure_db", "must be from 0 to 60");
    if (std::fabs(rx.ppm) > 1000.0)
        fail("receiver.ppm", "must be within +/-1000");
    if (std::fabs(rx.clock_ppm) > 1000.0)
        fail("receiver.clock_ppm", "must be within +/-1000");
    if (rx.quantise_bits != 0 && (rx.quantise_bits < 2 || rx.quantise_bits > 16))
        fail("receiver.quantise_bits", "must be 0 or from 2 to 16");
    if (!(rx.drops_interval_s >= 0.0) || !(rx.drops_first_s >= 0.0))
        fail("receiver.drops", "first_s and interval_s must not be negative");
    if (rx.drops_interval_s > 0.0 &&
        static_cast<double>(rx.drops_samples) >= rx.drops_interval_s * s.sample_rate_hz)
        fail("receiver.drops.samples", "must be fewer than the samples of one interval");

    for (size_t i = 0; i < s.path.points.size(); ++i) {
        const PathPoint& p = s.path.points[i];
        const std::string w = at("path.points", i);
        if (!(p.t >= 0.0))
            fail(at(w, "t"), "must not be negative");
        if (i > 0 && !(p.t > s.path.points[i - 1].t))
            fail(at(w, "t"), "times must increase");
        if (!p.floor.empty() && !s.find_scene(p.floor) && !s.floors.empty())
            fail(at(w, "floor"), "unknown floor \"" + p.floor + "\"");
    }
    for (size_t i = 0; i < s.events.size(); ++i) {
        const ScenarioEvent& e = s.events[i];
        const std::string w = at("events", i);
        if (!(e.t >= 0.0))
            fail(at(w, "t"), "must not be negative");
        if (i > 0 && e.t < s.events[i - 1].t)
            fail(at(w, "t"), "events must be in time order");
        if (e.type != EventType::rx_gain && !s.find_cell(e.cell))
            fail(at(w, "cell"), "unknown cell \"" + e.cell + "\"");
    }

    // The broadcast configuration of every cell must encode.
    for (size_t i = 0; i < s.cells.size(); ++i) {
        try {
            validate(cell_config(s, s.cells[i]));
        } catch (const ScenarioError&) {
            throw;
        } catch (const std::exception& e) {
            fail(at("cells", i), std::string("cannot be encoded: ") + e.what());
        }
    }
}

CellConfig cell_config(const Scenario& s, const ScenarioCell& c)
{
    CellConfig cfg = CellConfig::defaults();
    cfg.sync.colour_code = c.colour_code;
    cfg.sync.mle.mcc = s.mcc;
    cfg.sync.mle.mnc = s.mnc;
    cfg.sysinfo.main_carrier = c.main_carrier;
    cfg.sysinfo.frequency_band = c.frequency_band;
    cfg.sysinfo.offset = c.offset;
    cfg.sysinfo.rxlev_access_min = c.rxlev_access_min;
    cfg.sysinfo.ms_txpwr_max_cell = c.ms_txpwr_max_cell;
    cfg.sysinfo.mle.location_area = c.location_area;
    cfg.sysinfo.mle.bs_service_details.air_interface_encryption = c.encryption;
    if (!c.neighbours.empty() || c.network_time) {
        NwrkBroadcast nb;
        nb.reselect = c.reselect;
        if (c.network_time)
            nb.network_time = NetworkTime{};
        if (!c.neighbours.empty()) {
            std::vector<NeighbourCell> list;
            for (const auto& ref : c.neighbours) {
                const ScenarioCell* n = s.find_cell(ref.cell);
                if (!n)
                    throw ScenarioError("cell \"" + c.id + "\": unknown neighbour cell \"" + ref.cell + "\"");
                NeighbourCell nc;
                nc.cell_id = n->cell_number;
                nc.main_carrier = ref.main_carrier.value_or(n->main_carrier);
                if (n->frequency_band != c.frequency_band || n->offset != 0)
                    nc.carrier_extension = CarrierExtension{n->frequency_band, n->offset, 0, false};
                // The LA is sent only when planted: with it a neighbour
                // takes 48 instead of 23 bits.
                if (ref.location_area)
                    nc.location_area = ref.location_area;
                list.push_back(nc);
            }
            nb.neighbours = list;
        }
        // A list that does not fit one SCH/F block is spread over several
        // D-NWRK-BROADCAST PDUs sent in turn.
        std::vector<NwrkBroadcast> pdus = split_nwrk_broadcast(nb);
        cfg.nwrk_broadcast = pdus.front();
        cfg.nwrk_more.assign(pdus.begin() + 1, pdus.end());
    }
    return cfg;
}

radios::Scene apply_overrides(const Scenario& s, radios::Scene scene)
{
    for (radios::SceneAntenna& a : scene.antennas)
        for (const AntennaOverride& o : s.antennas)
            if (o.id == a.id)
                a.eirp_dbm = o.eirp_dbm;
    return scene;
}

namespace {

// Puts `c` in the place of the scenario cell `old` (same carrier): it keeps
// the old cell's neighbour cell identifier, the other cells' neighbour
// entries name it instead, and the old cell's events and its antennas in
// the scenario's own floors are dropped.
void replace_cell(Scenario& s, std::vector<ScenarioCell>::iterator old, ScenarioCell c)
{
    const std::string gone = old->id;
    c.cell_number = old->cell_number;
    *old = std::move(c);
    const std::string& now = old->id;
    for (ScenarioCell& o : s.cells) {
        bool listed = false;
        std::vector<ScenarioNeighbour> kept;
        for (ScenarioNeighbour n : o.neighbours) {
            if (n.cell == gone)
                n.cell = now;
            if (n.cell == now) {
                if (listed || o.id == now)
                    continue;
                listed = true;
            }
            kept.push_back(std::move(n));
        }
        o.neighbours = std::move(kept);
    }
    std::erase_if(s.events, [&](const ScenarioEvent& e) { return e.cell == gone; });
    for (ScenarioFloor& f : s.floors) {
        std::erase_if(f.scene.cells, [&](const radios::SceneCell& k) { return k.id == gone; });
        std::erase_if(f.scene.antennas, [&](const radios::SceneAntenna& a) { return a.cell == gone; });
    }
}

} // namespace

void add_scene_cells(Scenario& s, const radios::Scene& scene, const ScenarioCell& defaults)
{
    for (const radios::SceneCell& sc : scene.cells) {
        if (s.find_cell(sc.id))
            continue;
        ScenarioCell c = defaults;
        c.id = sc.id;
        c.label = sc.label;
        c.location_area = static_cast<uint16_t>(sc.la);
        c.colour_code = static_cast<uint8_t>(sc.colour_code);
        c.neighbours.clear();
        c.site.reset();
        // carrier_hz = band x 100 MHz + carrier x 25 kHz + offset.
        static constexpr int64_t kOffset[4] = {0, 6250, -6250, 12500};
        bool found = false;
        for (int code = 0; code < 4 && !found; ++code) {
            const int64_t rest = static_cast<int64_t>(sc.carrier_hz) - kOffset[code];
            if (rest < 0)
                continue;
            const int64_t band = rest / 100000000;
            const int64_t within = rest - band * 100000000;
            if (band <= 15 && within % 25000 == 0 && within / 25000 <= 4095) {
                c.frequency_band = static_cast<uint8_t>(band);
                c.main_carrier = static_cast<uint16_t>(within / 25000);
                c.offset = static_cast<uint8_t>(code);
                found = true;
            }
        }
        if (!found)
            throw ScenarioError("scene cell \"" + sc.id + "\": carrier " + std::to_string(sc.carrier_hz) +
                                " Hz is not on the TETRA raster");
        // A scenario cell on the same carrier gives way to the plan's cell
        // (RAD-61: the floor's cells are calculated from its scene).
        const auto same = std::find_if(s.cells.begin(), s.cells.end(), [&](const ScenarioCell& o) {
            return std::abs(cell_frequency_hz(o) - static_cast<double>(sc.carrier_hz)) < 1.0 &&
                   std::none_of(scene.cells.begin(), scene.cells.end(),
                                [&](const radios::SceneCell& k) { return k.id == o.id; });
        });
        if (same != s.cells.end()) {
            replace_cell(s, same, std::move(c));
            continue;
        }
        uint8_t number = 1;
        while (number < 31 && std::any_of(s.cells.begin(), s.cells.end(),
                                          [&](const ScenarioCell& o) { return o.cell_number == number; }))
            ++number;
        c.cell_number = number;
        s.cells.push_back(std::move(c));
    }
}

std::vector<ScenarioInfo> list_scenarios(const std::string& dir)
{
    std::vector<ScenarioInfo> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        try {
            const Scenario s = load_scenario_file(entry.path().string());
            out.push_back({entry.path().string(), s.id, s.name, s.example_ids, s.is_default});
        } catch (const std::exception&) {
            // not a (valid) scenario: skipped
        }
    }
    std::sort(out.begin(), out.end(), [](const ScenarioInfo& a, const ScenarioInfo& b) { return a.path < b.path; });
    return out;
}

std::optional<ScenarioInfo> select_scenario(const std::vector<ScenarioInfo>& list, const std::string& example_id)
{
    if (!example_id.empty())
        for (const ScenarioInfo& i : list)
            for (const std::string& e : i.example_ids) {
                if (e == example_id)
                    return i;
                if (!e.empty() && e.back() == '*' && example_id.rfind(e.substr(0, e.size() - 1), 0) == 0)
                    return i;
            }
    for (const ScenarioInfo& i : list)
        if (i.is_default)
            return i;
    return std::nullopt;
}

}  // namespace yantra::gen
