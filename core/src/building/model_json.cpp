// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JSON form of the building model, see model_json.h. Copied from yantra-v2
// 68ea20a: core/src/building/building_json.cpp, adapted (format name and
// version, unknown members ignored, per-band wall classes, cells, sources,
// trims, scale methods).

#include "yantra/core/building/model_json.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace yantra::building {

using Json = nlohmann::ordered_json;

namespace {

// --- writing ---------------------------------------------------------------------------

Json point(Point2 p) { return Json::array({p.x, p.y}); }

const char* antenna_type_name(AntennaType t) {
    return t == AntennaType::OutdoorDonor ? "outdoor_donor" : "indoor_das";
}

const char* trim_shape_name(Trim::Shape s) {
    switch (s) {
    case Trim::Shape::Rectangle: return "rectangle";
    case Trim::Shape::Polygon: return "polygon";
    case Trim::Shape::None: break;
    }
    return "none";
}

const char* scale_method_name(Scale::Method m) {
    switch (m) {
    case Scale::Method::TwoPoints: return "two_points";
    case Scale::Method::ScaleBar: return "scale_bar";
    case Scale::Method::Factor: return "factor";
    case Scale::Method::Shared: return "shared";
    case Scale::Method::None: break;
    }
    return "none";
}

Json pattern_json(const AntennaPattern& p) {
    switch (p.type) {
    case AntennaPattern::Type::Sector:
        return {{"type", "sector"},
                {"azimuth_deg", p.azimuth_deg},
                {"beamwidth_deg", p.beamwidth_deg},
                {"front_to_back_db", p.front_to_back_db}};
    case AntennaPattern::Type::Table:
        return {{"type", "table"}, {"azimuth_deg", p.azimuth_deg}, {"step_deg", p.step_deg}, {"gain_db", p.gain_db}};
    case AntennaPattern::Type::Omni: break;
    }
    return {{"type", "omni"}};
}

Json antenna_json(const Antenna& a) {
    Json j{{"id", a.id}};
    if (!a.name.empty()) j["name"] = a.name;
    if (!a.model.empty()) j["model"] = a.model;
    j["type"] = antenna_type_name(a.type);
    j["position"] = point(a.position);
    j["height_m"] = a.height_m;
    j["gain_dbi"] = a.gain_dbi;
    j["pattern"] = pattern_json(a.pattern);
    if (a.eirp_dbm) j["eirp_dbm"] = *a.eirp_dbm;
    if (a.feed_power_dbm) j["feed_power_dbm"] = *a.feed_power_dbm;
    if (a.cable_loss_db != 0.0) j["cable_loss_db"] = a.cable_loss_db;
    if (a.splitter_loss_db != 0.0) j["splitter_loss_db"] = a.splitter_loss_db;
    if (!a.cell.empty()) j["cell"] = a.cell;
    if (a.direction_deg) j["direction_deg"] = *a.direction_deg;
    if (a.elevation_deg) j["elevation_deg"] = *a.elevation_deg;
    return j;
}

Json scale_json(const Scale& s) {
    Json j{{"method", scale_method_name(s.method)}};
    switch (s.method) {
    case Scale::Method::TwoPoints:
    case Scale::Method::ScaleBar:
        j["a"] = point(s.a);
        j["b"] = point(s.b);
        j["distance_m"] = s.distance_m;
        break;
    case Scale::Method::Factor: j["metres_per_unit"] = s.metres_per_unit; break;
    case Scale::Method::Shared: j["floor"] = s.shared_floor; break;
    case Scale::Method::None: break;
    }
    return j;
}

Json floor_json(const Floor& f) {
    Json j{{"id", f.id}, {"name", f.name}, {"level", f.level}, {"height_m", f.height_m}};
    if (!f.source.empty()) {
        j["source"] = f.source;
        j["page"] = f.page;
    }
    if (!f.example_id.empty()) j["example_id"] = f.example_id;
    if (f.trim.shape != Trim::Shape::None || f.trim.quarter_turns != 0 || f.trim.deskew_deg != 0.0) {
        Json t{{"shape", trim_shape_name(f.trim.shape)}};
        Json outline = Json::array();
        for (const Point2& p : f.trim.outline) outline.push_back(point(p));
        t["outline"] = outline;
        t["quarter_turns"] = f.trim.quarter_turns;
        t["deskew_deg"] = f.trim.deskew_deg;
        j["trim"] = t;
    }
    j["plan_size"] = Json::array({f.plan_width, f.plan_height});
    j["scale"] = scale_json(f.scale);
    if (f.reference_point) j["reference_point"] = point(*f.reference_point);
    if (f.slab_attenuation_db) j["slab_attenuation_db"] = *f.slab_attenuation_db;
    Json walls = Json::array();
    for (const Wall& w : f.walls) {
        Json wj;
        if (!w.id.empty()) wj["id"] = w.id;
        wj["class"] = w.wall_class;
        Json pts = Json::array();
        for (const Point2& p : w.points) pts.push_back(point(p));
        wj["points"] = pts;
        if (w.attenuation_db) wj["attenuation_db"] = *w.attenuation_db;
        walls.push_back(wj);
    }
    j["walls"] = walls;
    Json ants = Json::array();
    for (const Antenna& a : f.antennas) ants.push_back(antenna_json(a));
    j["antennas"] = ants;
    return j;
}

// --- reading ---------------------------------------------------------------------------

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& path, const std::string& what) {
    throw FormatError(path.empty() ? what : path + ": " + what);
}

std::string member_path(const std::string& base, const std::string& key) {
    return base.empty() ? key : base + "." + key;
}

std::string index_path(const std::string& base, std::size_t i) { return base + "[" + std::to_string(i) + "]"; }

// An object with typed member access; unknown members are ignored.
class Obj {
public:
    Obj(const Json& j, std::string path) : j_(j), path_(std::move(path)) {
        if (!j_.is_object()) fail(path_, "expected an object");
    }
    bool has(const char* key) const { return j_.contains(key); }
    std::string at(const char* key) const { return member_path(path_, key); }
    const Json& value(const char* key) const {
        const auto it = j_.find(key);
        if (it == j_.end()) fail(at(key), "missing");
        return *it;
    }
    std::string str(const char* key) const {
        const Json& v = value(key);
        if (!v.is_string()) fail(at(key), "expected a string");
        return v.get<std::string>();
    }
    std::string str(const char* key, const std::string& fallback) const { return has(key) ? str(key) : fallback; }
    double num(const char* key, double lo, double hi) const {
        const Json& v = value(key);
        if (!v.is_number()) fail(at(key), "expected a number");
        const double d = v.get<double>();
        if (!std::isfinite(d) || d < lo || d > hi)
            fail(at(key), "out of range " + fmt(lo) + ".." + fmt(hi));
        return d;
    }
    double num(const char* key, double fallback, double lo, double hi) const {
        return has(key) ? num(key, lo, hi) : fallback;
    }
    std::optional<double> opt_num(const char* key, double lo, double hi) const {
        return has(key) ? std::optional<double>(num(key, lo, hi)) : std::nullopt;
    }
    std::int64_t integer(const char* key, std::int64_t lo, std::int64_t hi) const {
        const Json& v = value(key);
        if (!v.is_number_integer()) fail(at(key), "expected an integer");
        std::int64_t i = 0;
        if (v.is_number_unsigned()) {
            const auto u = v.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                fail(at(key), "out of range");
            i = static_cast<std::int64_t>(u);
        } else {
            i = v.get<std::int64_t>();
        }
        if (i < lo || i > hi) fail(at(key), "out of range " + std::to_string(lo) + ".." + std::to_string(hi));
        return i;
    }
    std::uint64_t uinteger(const char* key) const {
        const Json& v = value(key);
        if (!v.is_number_unsigned()) fail(at(key), "expected an unsigned integer");
        return v.get<std::uint64_t>();
    }
    bool boolean(const char* key, bool fallback) const {
        if (!has(key)) return fallback;
        const Json& v = value(key);
        if (!v.is_boolean()) fail(at(key), "expected true or false");
        return v.get<bool>();
    }
    const Json& array(const char* key) const {
        const Json& v = value(key);
        if (!v.is_array()) fail(at(key), "expected an array");
        return v;
    }

private:
    static std::string fmt(double d) {
        Json j = d;
        return j.dump();
    }
    const Json& j_;
    std::string path_;
};

Point2 read_point(const Json& v, const std::string& path) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number())
        fail(path, "expected [x, y]");
    const Point2 p{v[0].get<double>(), v[1].get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) fail(path, "must be finite");
    return p;
}

constexpr double kBig = 1e9;

AntennaPattern read_pattern(const Json& v, const std::string& path) {
    const Obj o(v, path);
    AntennaPattern p;
    const std::string type = o.str("type");
    if (type == "omni") {
        p.type = AntennaPattern::Type::Omni;
    } else if (type == "sector") {
        p.type = AntennaPattern::Type::Sector;
        p.azimuth_deg = o.num("azimuth_deg", 0.0, 0.0, 360.0);
        p.beamwidth_deg = o.num("beamwidth_deg", 0.0, 360.0);
        p.front_to_back_db = o.num("front_to_back_db", p.front_to_back_db, 0.0, 200.0);
    } else if (type == "table") {
        p.type = AntennaPattern::Type::Table;
        p.azimuth_deg = o.num("azimuth_deg", 0.0, 0.0, 360.0);
        p.step_deg = o.num("step_deg", 0.0, 360.0);
        const Json& g = o.array("gain_db");
        for (std::size_t i = 0; i < g.size(); ++i) {
            if (!g[i].is_number()) fail(index_path(o.at("gain_db"), i), "expected a number");
            p.gain_db.push_back(g[i].get<double>());
        }
    } else {
        fail(o.at("type"), "expected \"omni\", \"sector\" or \"table\"");
    }
    return p;
}

Antenna read_antenna(const Json& v, const std::string& path) {
    const Obj o(v, path);
    Antenna a;
    a.id = o.str("id");
    a.name = o.str("name", "");
    a.model = o.str("model", "");
    const std::string type = o.str("type", "indoor_das");
    if (type == "indoor_das") a.type = AntennaType::IndoorDas;
    else if (type == "outdoor_donor") a.type = AntennaType::OutdoorDonor;
    else fail(o.at("type"), "expected \"indoor_das\" or \"outdoor_donor\"");
    a.position = read_point(o.value("position"), o.at("position"));
    a.height_m = o.num("height_m", a.height_m, -kBig, kBig);
    a.gain_dbi = o.num("gain_dbi", a.gain_dbi, -100.0, 100.0);
    if (o.has("pattern")) a.pattern = read_pattern(o.value("pattern"), o.at("pattern"));
    a.eirp_dbm = o.opt_num("eirp_dbm", -200.0, 200.0);
    a.feed_power_dbm = o.opt_num("feed_power_dbm", -200.0, 200.0);
    a.cable_loss_db = o.num("cable_loss_db", 0.0, 0.0, 200.0);
    a.splitter_loss_db = o.num("splitter_loss_db", 0.0, 0.0, 200.0);
    a.cell = o.str("cell", "");
    a.direction_deg = o.opt_num("direction_deg", -360.0, 360.0);
    a.elevation_deg = o.opt_num("elevation_deg", -90.0, 90.0);
    return a;
}

Scale read_scale(const Json& v, const std::string& path) {
    const Obj o(v, path);
    Scale s;
    const std::string m = o.str("method");
    if (m == "none") {
        s.method = Scale::Method::None;
    } else if (m == "two_points" || m == "scale_bar") {
        s.method = m == "two_points" ? Scale::Method::TwoPoints : Scale::Method::ScaleBar;
        s.a = read_point(o.value("a"), o.at("a"));
        s.b = read_point(o.value("b"), o.at("b"));
        s.distance_m = o.num("distance_m", 0.0, kBig);
    } else if (m == "factor") {
        s.method = Scale::Method::Factor;
        s.metres_per_unit = o.num("metres_per_unit", 0.0, kBig);
    } else if (m == "shared") {
        s.method = Scale::Method::Shared;
        s.shared_floor = o.str("floor");
    } else {
        fail(o.at("method"), "expected \"none\", \"two_points\", \"scale_bar\", \"factor\" or \"shared\"");
    }
    return s;
}

Trim read_trim(const Json& v, const std::string& path) {
    const Obj o(v, path);
    Trim t;
    const std::string shape = o.str("shape", "none");
    if (shape == "none") t.shape = Trim::Shape::None;
    else if (shape == "rectangle") t.shape = Trim::Shape::Rectangle;
    else if (shape == "polygon") t.shape = Trim::Shape::Polygon;
    else fail(o.at("shape"), "expected \"none\", \"rectangle\" or \"polygon\"");
    if (o.has("outline")) {
        const Json& pts = o.array("outline");
        for (std::size_t i = 0; i < pts.size(); ++i) t.outline.push_back(read_point(pts[i], index_path(o.at("outline"), i)));
    }
    t.quarter_turns = static_cast<int>(o.has("quarter_turns") ? o.integer("quarter_turns", 0, 3) : 0);
    t.deskew_deg = o.num("deskew_deg", 0.0, -45.0, 45.0);
    return t;
}

Wall read_wall(const Json& v, const std::string& path) {
    const Obj o(v, path);
    Wall w;
    w.id = o.str("id", "");
    w.wall_class = o.str("class", "");
    const Json& pts = o.array("points");
    for (std::size_t i = 0; i < pts.size(); ++i) w.points.push_back(read_point(pts[i], index_path(o.at("points"), i)));
    w.attenuation_db = o.opt_num("attenuation_db", 0.0, 1000.0);
    return w;
}

Floor read_floor(const Json& v, const std::string& path) {
    const Obj o(v, path);
    Floor f;
    f.id = o.str("id");
    f.name = o.str("name", f.id);
    f.level = static_cast<int>(o.integer("level", -1000, 1000));
    f.height_m = o.num("height_m", f.height_m, 0.0, 1000.0);
    f.source = o.str("source", "");
    f.page = static_cast<int>(o.has("page") ? o.integer("page", 0, 1'000'000) : 0);
    f.example_id = o.str("example_id", "");
    if (o.has("trim")) f.trim = read_trim(o.value("trim"), o.at("trim"));
    if (o.has("plan_size")) {
        const Point2 s = read_point(o.value("plan_size"), o.at("plan_size"));
        if (s.x < 0.0 || s.y < 0.0) fail(o.at("plan_size"), "must be >= 0");
        f.plan_width = s.x;
        f.plan_height = s.y;
    }
    if (o.has("scale")) f.scale = read_scale(o.value("scale"), o.at("scale"));
    if (o.has("reference_point")) f.reference_point = read_point(o.value("reference_point"), o.at("reference_point"));
    f.slab_attenuation_db = o.opt_num("slab_attenuation_db", 0.0, 1000.0);
    if (o.has("walls")) {
        const Json& ws = o.array("walls");
        for (std::size_t i = 0; i < ws.size(); ++i) f.walls.push_back(read_wall(ws[i], index_path(o.at("walls"), i)));
    }
    if (o.has("antennas")) {
        const Json& as = o.array("antennas");
        for (std::size_t i = 0; i < as.size(); ++i)
            f.antennas.push_back(read_antenna(as[i], index_path(o.at("antennas"), i)));
    }
    return f;
}

Building read_building(const Json& j) {
    const Obj o(j, "");
    if (o.str("format") != kBuildingFormat) fail("format", std::string("expected \"") + kBuildingFormat + "\"");
    const std::uint64_t version = o.uinteger("version");
    if (version > kBuildingVersion)
        fail("version", "version " + std::to_string(version) + " is newer than this reader (" +
                            std::to_string(kBuildingVersion) + ")");
    if (version == 0) fail("version", "must be >= 1");
    Building b;
    b.name = o.str("name", "");
    b.floor_attenuation_db = o.num("floor_attenuation_db", b.floor_attenuation_db, 0.0, 1000.0);
    if (o.has("wall_classes")) {
        b.wall_classes.clear();  // a given table replaces the defaults (IND-11)
        const Json& cs = o.array("wall_classes");
        for (std::size_t i = 0; i < cs.size(); ++i) {
            const Obj c(cs[i], index_path("wall_classes", i));
            WallClass wc;
            wc.id = c.str("id");
            wc.name = c.str("name", wc.id);
            const Json& bands = c.array("bands");
            for (std::size_t k = 0; k < bands.size(); ++k) {
                const Obj bo(bands[k], index_path(c.at("bands"), k));
                wc.bands.push_back({bo.num("frequency_mhz", 0.0, 1e7), bo.num("attenuation_db", 0.0, 1000.0)});
            }
            wc.reflection_loss_db = c.opt_num("reflection_loss_db", 0.0, 1000.0);
            b.wall_classes.push_back(std::move(wc));
        }
    }
    if (o.has("cells")) {
        const Json& cs = o.array("cells");
        for (std::size_t i = 0; i < cs.size(); ++i) {
            const Obj c(cs[i], index_path("cells", i));
            Cell cell;
            cell.id = c.str("id");
            cell.label = c.str("label", "");
            cell.technology_id = c.str("technology", "");
            cell.schema_id = c.str("schema", "");
            cell.carrier_hz = c.has("carrier_hz") ? c.uinteger("carrier_hz") : 0;
            if (c.has("identity")) {
                const Json& id = c.value("identity");
                if (!id.is_object()) fail(c.at("identity"), "expected an object");
                for (auto it = id.begin(); it != id.end(); ++it) {
                    const std::string key = it.key();
                    if (!it.value().is_number_integer())
                        fail(member_path(c.at("identity"), key), "expected an integer");
                    if (it.value().is_number_unsigned() &&
                        it.value().get<std::uint64_t>() >
                            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                        fail(member_path(c.at("identity"), key), "out of range");
                    cell.identity[key] = it.value().get<std::int64_t>();
                }
            }
            b.cells.push_back(std::move(cell));
        }
    }
    if (o.has("sources")) {
        const Json& ss = o.array("sources");
        for (std::size_t i = 0; i < ss.size(); ++i) {
            const Obj s(ss[i], index_path("sources", i));
            PlanSource src;
            src.id = s.str("id");
            src.file_name = s.str("file_name", "");
            src.sha256 = s.str("sha256", "");
            src.embedded = s.boolean("embedded", true);
            b.sources.push_back(std::move(src));
        }
    }
    const Json& fs = o.array("floors");
    for (std::size_t i = 0; i < fs.size(); ++i) b.floors.push_back(read_floor(fs[i], index_path("floors", i)));
    return b;
}

}  // namespace

std::string building_to_json(const Building& b, int indent) {
    Json j{{"format", kBuildingFormat}, {"version", kBuildingVersion}};
    if (!b.name.empty()) j["name"] = b.name;
    j["floor_attenuation_db"] = b.floor_attenuation_db;
    Json classes = Json::array();
    for (const WallClass& c : b.wall_classes) {
        Json cj{{"id", c.id}, {"name", c.name}};
        Json bands = Json::array();
        for (const BandAttenuation& band : c.bands)
            bands.push_back({{"frequency_mhz", band.frequency_mhz}, {"attenuation_db", band.attenuation_db}});
        cj["bands"] = bands;
        if (c.reflection_loss_db) cj["reflection_loss_db"] = *c.reflection_loss_db;
        classes.push_back(cj);
    }
    j["wall_classes"] = classes;
    Json cells = Json::array();
    for (const Cell& c : b.cells) {
        Json cj{{"id", c.id}};
        if (!c.label.empty()) cj["label"] = c.label;
        if (!c.technology_id.empty()) cj["technology"] = c.technology_id;
        if (!c.schema_id.empty()) cj["schema"] = c.schema_id;
        if (c.carrier_hz != 0) cj["carrier_hz"] = c.carrier_hz;
        Json id = Json::object();
        for (const auto& [k, v] : c.identity) id[k] = v;
        cj["identity"] = id;
        cells.push_back(cj);
    }
    j["cells"] = cells;
    Json sources = Json::array();
    for (const PlanSource& s : b.sources) {
        Json sj{{"id", s.id}, {"file_name", s.file_name}};
        if (!s.sha256.empty()) sj["sha256"] = s.sha256;
        sj["embedded"] = s.embedded;
        sources.push_back(sj);
    }
    j["sources"] = sources;
    Json floors = Json::array();
    for (const Floor& f : b.floors) floors.push_back(floor_json(f));
    j["floors"] = floors;
    return j.dump(indent);
}

bool parse_building(std::string_view text, Building& out, std::string& error) {
    try {
        const Json j = Json::parse(text.begin(), text.end());
        Building b = read_building(j);
        const auto problems = b.validate();
        if (!problems.empty()) {
            error = problems.front();
            return false;
        }
        out = std::move(b);
        return true;
    } catch (const FormatError& e) {
        error = e.what();
    } catch (const nlohmann::json::exception& e) {
        error = std::string("not valid JSON: ") + e.what();
    }
    return false;
}

}  // namespace yantra::building
