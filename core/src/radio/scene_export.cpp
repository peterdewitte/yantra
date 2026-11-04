// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene export (RAD-36, PLG-27), see scene_export.h.

#include "yantra/core/radio/scene_export.h"

#include <yantra/radio.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

#include <nlohmann/json.hpp>

namespace yantra::radio {

using building::Antenna;
using building::AntennaPattern;
using building::AntennaType;
using building::Building;
using building::Cell;
using building::Floor;
using building::Point2;
using Json = nlohmann::ordered_json;

namespace {

// 1e-6 rounding: stable text; -0 becomes 0.
double r6(double v) {
    const double r = std::round(v * 1e6) / 1e6;
    return r == 0.0 ? 0.0 : r;
}

// An azimuth for the scene: rounded, in [0, 360).
double azimuth(double deg) {
    const double a = r6(deg);
    return a >= 360.0 ? 0.0 : a;
}

Json pattern_json(const AntennaPattern& p) {
    switch (p.type) {
    case AntennaPattern::Type::Sector:
        return {{"type", "sector"},
                {"azimuth_deg", azimuth(p.azimuth_deg)},
                {"beamwidth_deg", r6(p.beamwidth_deg)},
                {"front_to_back_db", r6(p.front_to_back_db)}};
    case AntennaPattern::Type::Table: {
        Json g = Json::array();
        for (double v : p.gain_db) g.push_back(r6(v));
        return {{"type", "table"}, {"azimuth_deg", azimuth(p.azimuth_deg)}, {"step_deg", r6(p.step_deg)}, {"gain_db", g}};
    }
    case AntennaPattern::Type::Omni: break;
    }
    return {{"type", "omni"}};
}

// The scene identity of a cell, or the reason it has none.
std::optional<Json> cell_json(const Cell& c, std::string& why) {
    if (c.carrier_hz == 0) {
        why = "it has no carrier";
        return std::nullopt;
    }
    Json j{{"id", c.id}};
    if (!c.label.empty()) j["label"] = c.label;
    for (const char* key : {YR_FIELD_MCC, YR_FIELD_MNC, YR_FIELD_LA, YR_FIELD_COLOUR_CODE}) {
        const auto it = c.identity.find(key);
        if (it == c.identity.end()) {
            why = std::string("its identity has no \"") + key + "\"";
            return std::nullopt;
        }
        if (it->second < 0 || it->second > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            why = std::string("its \"") + key + "\" is out of range";
            return std::nullopt;
        }
        j[key] = static_cast<std::uint64_t>(it->second);
    }
    j["carrier_hz"] = c.carrier_hz;
    return j;
}

struct Placed {
    const Floor* floor = nullptr;
    const Antenna* antenna = nullptr;
    Point2 at_m;          // plan-frame metres of the scene's floor
    int floor_offset = 0;
    double eirp_dbm = 0.0;
};

}  // namespace

SceneExport export_scene(const Building& b, const std::string& floor_id, const SceneExportOptions& options) {
    SceneExport out;
    const Floor* f = b.find_floor(floor_id);
    if (f == nullptr) {
        out.error = "unknown floor \"" + floor_id + "\"";
        return out;
    }
    if (const auto problems = b.validate(); !problems.empty()) {
        out.error = "the building model is not valid: " + problems.front();
        return out;
    }
    auto warn = [&](const std::string& w) { out.warnings.push_back(w); };

    const auto mpu = b.metres_per_unit(*f);
    const bool scale_set = mpu.has_value() && f->plan_width > 0.0 && f->plan_height > 0.0;
    if (mpu && !scale_set) warn("floor \"" + f->id + "\": the plan size is 0, so the scene has no scale");
    if (!scale_set && (!f->walls.empty() || !f->antennas.empty()))
        warn("floor \"" + f->id + "\" has no scale: its walls and antennas are not in the scene");
    const double slab_db = b.floor_attenuation_db;

    // Antennas (and the cells they need).
    std::vector<Placed> placed;
    std::set<std::string> cell_ids;
    std::vector<std::string> bad_cells;
    auto consider = [&](const Floor& on, const Antenna& a, Point2 at_m, int offset) {
        const std::string what = "antenna \"" + a.id + "\"";
        if (a.type == AntennaType::OutdoorDonor) {
            warn(what + ": outdoor donor directions are not part of the scene");
            return;
        }
        if (a.cell.empty()) {
            warn(what + ": no cell, left out");
            return;
        }
        const Cell* c = b.find_cell(a.cell);
        std::string why;
        if (c == nullptr || !cell_json(*c, why)) {
            warn(what + ": cell \"" + a.cell + "\" cannot be in a scene (" + why + "), left out");
            return;
        }
        auto eirp = a.effective_eirp_dbm();
        if (!eirp) eirp = options.default_eirp_dbm;
        if (!eirp) {
            warn(what + ": no EIRP, left out");
            return;
        }
        double e = *eirp;
        if (offset != 0) {
            // The scene applies |offset| x slab_db; the model the per-floor slabs.
            e -= b.inter_floor_attenuation_db(f->level, on.level) - std::abs(offset) * slab_db;
        }
        placed.push_back({&on, &a, at_m, offset, e});
        cell_ids.insert(a.cell);
    };
    if (scale_set) {
        for (const Antenna& a : f->antennas) consider(*f, a, *b.to_plan_metres(*f, a.position), 0);
        if (options.other_floors) {
            for (std::size_t i : b.floors_by_level()) {
                const Floor& o = b.floors[i];
                if (&o == f || o.antennas.empty()) continue;
                const int offset = o.level - f->level;
                if (!f->reference_point || !o.reference_point || !b.has_scale(o)) {
                    warn("floor \"" + o.id + "\": not aligned with \"" + f->id +
                         "\" (scale and reference point on both), its antennas are not in the scene");
                    continue;
                }
                if (offset < -200 || offset > 200) {
                    warn("floor \"" + o.id + "\": more than 200 storeys away, its antennas are not in the scene");
                    continue;
                }
                for (const Antenna& a : o.antennas) {
                    const Point2 bm = *b.to_building_metres(o, a.position);
                    const Point2 here = *b.from_building_metres(*f, bm);
                    consider(o, a, *b.to_plan_metres(*f, here), offset);
                }
            }
        }
    }

    // The scene frequency for the wall attenuations.
    double hz = 0.0;
    if (options.frequency_hz && *options.frequency_hz > 0.0) {
        hz = *options.frequency_hz;
    } else if (!cell_ids.empty()) {
        double sum = 0.0;
        for (const std::string& id : cell_ids) sum += static_cast<double>(b.find_cell(id)->carrier_hz);
        hz = sum / static_cast<double>(cell_ids.size());
    }
    out.frequency_hz = hz;
    const double mhz = hz / 1e6;

    Json floor{{"id", f->id}};
    if (!f->name.empty()) floor["name"] = f->name;
    floor["scale_set"] = scale_set;
    const auto size = scale_set ? b.plan_size_m(*f) : std::nullopt;
    floor["width_m"] = size ? r6(size->x) : 0.0;
    floor["height_m"] = size ? r6(size->y) : 0.0;
    floor["slab_attenuation_db"] = r6(slab_db);
    floor["storey_height_m"] = r6(f->height_m);

    Json classes = Json::array();
    for (const auto& c : b.wall_classes) {
        Json cj{{"id", c.id}};
        if (!c.name.empty()) cj["label"] = c.name;
        cj["attenuation_db"] = r6(c.attenuation_at_db(mhz));
        if (c.reflection_loss_db) cj["reflection_loss_db"] = r6(*c.reflection_loss_db);
        classes.push_back(cj);
    }

    Json walls = Json::array();
    if (scale_set) {
        for (const auto& w : f->walls) {
            const double att = *b.wall_attenuation_db(w, mhz);
            for (std::size_t i = 0; i + 1 < w.points.size(); ++i) {
                const Point2 p = *b.to_plan_metres(*f, w.points[i]);
                const Point2 q = *b.to_plan_metres(*f, w.points[i + 1]);
                walls.push_back({{"x1_m", r6(p.x)},
                                 {"y1_m", r6(p.y)},
                                 {"x2_m", r6(q.x)},
                                 {"y2_m", r6(q.y)},
                                 {"class", w.wall_class},
                                 {"attenuation_db", r6(att)}});
            }
        }
    }

    Json cells = Json::array();
    for (const Cell& c : b.cells) {
        if (cell_ids.count(c.id) == 0) continue;
        std::string why;
        cells.push_back(*cell_json(c, why));
    }

    Json antennas = Json::array();
    for (const Placed& p : placed) {
        const Antenna& a = *p.antenna;
        Json aj{{"id", a.id},
                {"cell", a.cell},
                {"x_m", r6(p.at_m.x)},
                {"y_m", r6(p.at_m.y)},
                {"height_m", r6(a.height_m)},
                {"eirp_dbm", r6(p.eirp_dbm)},
                {"pattern", pattern_json(a.pattern)}};
        if (p.floor_offset != 0) aj["floor_offset"] = p.floor_offset;
        antennas.push_back(aj);
    }

    const Json scene{{"format", YR_SCENE_FORMAT}, {"version", YR_SCENE_VERSION}, {"floor", floor},
                     {"wall_classes", classes},   {"walls", walls},             {"cells", cells},
                     {"antennas", antennas}};
    out.json = scene.dump(options.indent);
    out.ok = true;
    return out;
}

}  // namespace yantra::radio
