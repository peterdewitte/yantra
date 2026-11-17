// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JSON form of the building model, see model_json.h. Copied from yantra-v2
// 68ea20a: core/src/building/building_json.cpp, adapted (format name and
// version, unknown members ignored, cells, sources, trims, scale methods).
// The members of 0.3 and earlier (walls, wall_classes, antennas,
// slab_attenuation_db, floor_attenuation_db; IND-46) are unknown members
// now: a reader ignores them.

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
