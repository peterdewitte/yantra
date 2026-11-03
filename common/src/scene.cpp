// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene JSON v1 (RAD-36): parser and writer. The format is defined in
// include/yantra/radio.h (YR_SCENE_FORMAT).
#include <yantra/radios/scene.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
#include <stdexcept>

namespace yantra::radios {

namespace {

using json = nlohmann::ordered_json;

struct Bad : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& where, const std::string& what)
{
    throw Bad(where + ": " + what);
}

const json& need(const json& j, const char* key, const std::string& where)
{
    auto it = j.find(key);
    if (it == j.end())
        fail(where, std::string("member \"") + key + "\" missing");
    return *it;
}

std::string str(const json& j, const char* key, const std::string& where, bool required)
{
    auto it = j.find(key);
    if (it == j.end()) {
        if (required)
            fail(where, std::string("member \"") + key + "\" missing");
        return {};
    }
    if (!it->is_string())
        fail(where + "." + key, "must be a string");
    return it->get<std::string>();
}

std::string id_of(const json& j, const std::string& where)
{
    std::string id = str(j, "id", where, true);
    if (id.empty())
        fail(where + ".id", "must not be empty");
    return id;
}

double num(const json& j, const char* key, const std::string& where)
{
    const json& v = need(j, key, where);
    if (!v.is_number())
        fail(where + "." + key, "must be a number");
    const double d = v.get<double>();
    if (!std::isfinite(d))
        fail(where + "." + key, "must be finite");
    return d;
}

uint64_t unsigned_num(const json& j, const char* key, const std::string& where, uint64_t max)
{
    const json& v = need(j, key, where);
    if (!v.is_number_unsigned() && !(v.is_number_integer() && v.get<int64_t>() >= 0))
        fail(where + "." + key, "must be an unsigned integer");
    const uint64_t u = v.get<uint64_t>();
    if (u > max)
        fail(where + "." + key, "is out of range");
    return u;
}

std::optional<double> opt_num(const json& j, const char* key, const std::string& where)
{
    if (j.find(key) == j.end())
        return std::nullopt;
    return num(j, key, where);
}

const json& array_of(const json& j, const char* key)
{
    static const json empty = json::array();
    auto it = j.find(key);
    if (it == j.end())
        return empty;
    if (!it->is_array())
        fail(key, "must be an array");
    return *it;
}

void object_check(const json& j, const std::string& where)
{
    if (!j.is_object())
        fail(where, "must be an object");
}

ScenePattern pattern_from(const json& j, const std::string& where)
{
    object_check(j, where);
    ScenePattern p;
    const std::string type = str(j, "type", where, true);
    if (type == "omni") {
        p.type = ScenePattern::Type::omni;
    } else if (type == "sector") {
        p.type = ScenePattern::Type::sector;
        p.azimuth_deg = num(j, "azimuth_deg", where);
        p.beamwidth_deg = num(j, "beamwidth_deg", where);
        p.front_to_back_db = num(j, "front_to_back_db", where);
        if (!(p.beamwidth_deg > 0.0 && p.beamwidth_deg <= 360.0))
            fail(where + ".beamwidth_deg", "must be above 0 and at most 360");
        if (p.front_to_back_db < 0.0)
            fail(where + ".front_to_back_db", "must not be negative");
    } else if (type == "table") {
        p.type = ScenePattern::Type::table;
        p.azimuth_deg = num(j, "azimuth_deg", where);
        p.step_deg = num(j, "step_deg", where);
        if (!(p.step_deg > 0.0))
            fail(where + ".step_deg", "must be above 0");
        const json& g = need(j, "gain_db", where);
        if (!g.is_array() || g.empty())
            fail(where + ".gain_db", "must be a non-empty array of numbers");
        for (const json& e : g) {
            if (!e.is_number() || !std::isfinite(e.get<double>()))
                fail(where + ".gain_db", "must be a non-empty array of numbers");
            p.gain_db.push_back(e.get<double>());
        }
        if (p.step_deg * static_cast<double>(p.gain_db.size()) > 360.0 + 1e-9)
            fail(where + ".gain_db", "covers more than 360 degrees");
    } else {
        fail(where + ".type", "unknown pattern \"" + type + "\" (omni, sector or table)");
    }
    if (!(p.azimuth_deg >= 0.0 && p.azimuth_deg < 360.0))
        fail(where + ".azimuth_deg", "must be in [0, 360)");
    return p;
}

Scene parse(const json& root)
{
    object_check(root, "scene");
    auto f = root.find("format");
    if (f == root.end() || !f->is_string() || f->get<std::string>() != YR_SCENE_FORMAT)
        throw Bad(std::string("not a scene (format is not \"") + YR_SCENE_FORMAT + "\")");
    const json v = need(root, "version", "scene");
    if (!v.is_number_integer())
        fail("version", "must be an integer");
    const int64_t version = v.get<int64_t>();
    if (version < 1)
        fail("version", "invalid version " + std::to_string(version));
    if (version > static_cast<int64_t>(YR_SCENE_VERSION))
        throw Bad("scene version " + std::to_string(version) +
                  " is newer than this program reads (" + std::to_string(YR_SCENE_VERSION) + ")");
    Scene s;
    s.version = static_cast<uint32_t>(version);

    const json fl = need(root, "floor", "scene");
    object_check(fl, "floor");
    s.floor.id = id_of(fl, "floor");
    s.floor.name = str(fl, "name", "floor", false);
    const json scale = need(fl, "scale_set", "floor");
    if (!scale.is_boolean())
        fail("floor.scale_set", "must be true or false");
    s.floor.scale_set = scale.get<bool>();
    s.floor.width_m = num(fl, "width_m", "floor");
    s.floor.height_m = num(fl, "height_m", "floor");
    if (s.floor.width_m < 0.0 || s.floor.height_m < 0.0)
        fail("floor", "width_m and height_m must not be negative");
    if (s.floor.scale_set && (s.floor.width_m <= 0.0 || s.floor.height_m <= 0.0))
        fail("floor", "a floor with a scale needs width_m and height_m above 0");
    s.floor.slab_attenuation_db = opt_num(fl, "slab_attenuation_db", "floor");
    if (s.floor.slab_attenuation_db && *s.floor.slab_attenuation_db < 0.0)
        fail("floor.slab_attenuation_db", "must not be negative");
    s.floor.storey_height_m = opt_num(fl, "storey_height_m", "floor");
    if (s.floor.storey_height_m && !(*s.floor.storey_height_m > 0.0))
        fail("floor.storey_height_m", "must be above 0");

    std::set<std::string> ids;
    const json& classes = array_of(root, "wall_classes");
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const std::string w = "wall_classes[" + std::to_string(i) + "]";
        object_check(classes[i], w);
        SceneWallClass c;
        c.id = id_of(classes[i], w);
        if (!ids.insert(c.id).second)
            fail(w + ".id", "\"" + c.id + "\" repeated");
        c.label = str(classes[i], "label", w, false);
        c.attenuation_db = num(classes[i], "attenuation_db", w);
        if (c.attenuation_db < 0.0)
            fail(w + ".attenuation_db", "must not be negative");
        c.reflection_loss_db = opt_num(classes[i], "reflection_loss_db", w);
        if (c.reflection_loss_db && *c.reflection_loss_db < 0.0)
            fail(w + ".reflection_loss_db", "must not be negative");
        s.wall_classes.push_back(std::move(c));
    }

    const json& walls = array_of(root, "walls");
    for (std::size_t i = 0; i < walls.size(); ++i) {
        const std::string w = "walls[" + std::to_string(i) + "]";
        object_check(walls[i], w);
        SceneWall wall;
        wall.x1_m = num(walls[i], "x1_m", w);
        wall.y1_m = num(walls[i], "y1_m", w);
        wall.x2_m = num(walls[i], "x2_m", w);
        wall.y2_m = num(walls[i], "y2_m", w);
        wall.wall_class = str(walls[i], "class", w, false);
        if (!wall.wall_class.empty() && ids.count(wall.wall_class) == 0)
            fail(w + ".class", "unknown wall class \"" + wall.wall_class + "\"");
        wall.attenuation_db = num(walls[i], "attenuation_db", w);
        if (wall.attenuation_db < 0.0)
            fail(w + ".attenuation_db", "must not be negative");
        s.walls.push_back(std::move(wall));
    }

    ids.clear();
    const json& cells = array_of(root, "cells");
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const std::string w = "cells[" + std::to_string(i) + "]";
        object_check(cells[i], w);
        SceneCell c;
        c.id = id_of(cells[i], w);
        if (!ids.insert(c.id).second)
            fail(w + ".id", "\"" + c.id + "\" repeated");
        c.label = str(cells[i], "label", w, false);
        c.mcc = static_cast<uint32_t>(unsigned_num(cells[i], "mcc", w, UINT32_MAX));
        c.mnc = static_cast<uint32_t>(unsigned_num(cells[i], "mnc", w, UINT32_MAX));
        c.la = static_cast<uint32_t>(unsigned_num(cells[i], "la", w, UINT32_MAX));
        c.colour_code = static_cast<uint32_t>(unsigned_num(cells[i], "colour_code", w, UINT32_MAX));
        c.carrier_hz = unsigned_num(cells[i], "carrier_hz", w, UINT64_MAX);
        if (c.carrier_hz == 0)
            fail(w + ".carrier_hz", "must be above 0");
        s.cells.push_back(std::move(c));
    }

    std::set<std::string> antenna_ids;
    const json& antennas = array_of(root, "antennas");
    for (std::size_t i = 0; i < antennas.size(); ++i) {
        const std::string w = "antennas[" + std::to_string(i) + "]";
        object_check(antennas[i], w);
        SceneAntenna a;
        a.id = id_of(antennas[i], w);
        if (!antenna_ids.insert(a.id).second)
            fail(w + ".id", "\"" + a.id + "\" repeated");
        a.cell = str(antennas[i], "cell", w, true);
        if (ids.count(a.cell) == 0)
            fail(w + ".cell", "unknown cell \"" + a.cell + "\"");
        a.x_m = num(antennas[i], "x_m", w);
        a.y_m = num(antennas[i], "y_m", w);
        a.height_m = num(antennas[i], "height_m", w);
        a.eirp_dbm = num(antennas[i], "eirp_dbm", w);
        a.pattern = pattern_from(need(antennas[i], "pattern", w), w + ".pattern");
        if (antennas[i].find("floor_offset") != antennas[i].end()) {
            const json& o = antennas[i]["floor_offset"];
            if (!o.is_number_integer() || o.get<int64_t>() < -200 || o.get<int64_t>() > 200)
                fail(w + ".floor_offset", "must be an integer from -200 to 200");
            a.floor_offset = static_cast<int32_t>(o.get<int64_t>());
        }
        s.antennas.push_back(std::move(a));
    }
    return s;
}

} // namespace

const SceneCell* Scene::find_cell(std::string_view id) const
{
    for (const SceneCell& c : cells)
        if (c.id == id)
            return &c;
    return nullptr;
}

const SceneWallClass* Scene::find_wall_class(std::string_view id) const
{
    for (const SceneWallClass& c : wall_classes)
        if (c.id == id)
            return &c;
    return nullptr;
}

bool parse_scene(std::string_view json_text, Scene& out, std::string& error)
{
    try {
        out = parse(json::parse(json_text.begin(), json_text.end()));
        error.clear();
        return true;
    } catch (const json::exception& e) {
        error = std::string("not valid JSON: ") + e.what();
    } catch (const Bad& e) {
        error = e.what();
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

std::string scene_to_json(const Scene& s)
{
    json root = {{"format", YR_SCENE_FORMAT},
                 {"version", s.version},
                 {"floor",
                  {{"id", s.floor.id},
                   {"name", s.floor.name},
                   {"scale_set", s.floor.scale_set},
                   {"width_m", s.floor.width_m},
                   {"height_m", s.floor.height_m}}}};
    if (s.floor.slab_attenuation_db)
        root["floor"]["slab_attenuation_db"] = *s.floor.slab_attenuation_db;
    if (s.floor.storey_height_m)
        root["floor"]["storey_height_m"] = *s.floor.storey_height_m;
    json classes = json::array();
    for (const SceneWallClass& c : s.wall_classes) {
        json jc = {{"id", c.id}, {"label", c.label}, {"attenuation_db", c.attenuation_db}};
        if (c.reflection_loss_db)
            jc["reflection_loss_db"] = *c.reflection_loss_db;
        classes.push_back(std::move(jc));
    }
    root["wall_classes"] = std::move(classes);
    json walls = json::array();
    for (const SceneWall& w : s.walls)
        walls.push_back({{"x1_m", w.x1_m},
                         {"y1_m", w.y1_m},
                         {"x2_m", w.x2_m},
                         {"y2_m", w.y2_m},
                         {"class", w.wall_class},
                         {"attenuation_db", w.attenuation_db}});
    root["walls"] = std::move(walls);
    json cells = json::array();
    for (const SceneCell& c : s.cells)
        cells.push_back({{"id", c.id},
                         {"label", c.label},
                         {"mcc", c.mcc},
                         {"mnc", c.mnc},
                         {"la", c.la},
                         {"colour_code", c.colour_code},
                         {"carrier_hz", c.carrier_hz}});
    root["cells"] = std::move(cells);
    json antennas = json::array();
    for (const SceneAntenna& a : s.antennas) {
        json p;
        switch (a.pattern.type) {
        case ScenePattern::Type::omni:
            p = {{"type", "omni"}};
            break;
        case ScenePattern::Type::sector:
            p = {{"type", "sector"},
                 {"azimuth_deg", a.pattern.azimuth_deg},
                 {"beamwidth_deg", a.pattern.beamwidth_deg},
                 {"front_to_back_db", a.pattern.front_to_back_db}};
            break;
        case ScenePattern::Type::table:
            p = {{"type", "table"},
                 {"azimuth_deg", a.pattern.azimuth_deg},
                 {"step_deg", a.pattern.step_deg},
                 {"gain_db", a.pattern.gain_db}};
            break;
        }
        json ja = {{"id", a.id},
                   {"cell", a.cell},
                   {"x_m", a.x_m},
                   {"y_m", a.y_m},
                   {"height_m", a.height_m},
                   {"eirp_dbm", a.eirp_dbm},
                   {"pattern", std::move(p)}};
        if (a.floor_offset != 0)
            ja["floor_offset"] = a.floor_offset;
        antennas.push_back(std::move(ja));
    }
    root["antennas"] = std::move(antennas);
    return root.dump();
}

} // namespace yantra::radios
