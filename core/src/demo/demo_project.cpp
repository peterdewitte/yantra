// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The demo project builder (demo/demo_project.h, docs/DEMO.md). The walk
// replay follows the record-file replay of the TST-10 tests
// (tests/measure_host/e2e_helpers.h FileRun) and the recorded walk test of
// tests/measure_host/test_measure_survey.cpp. The description's walls,
// antennas, trims and walk routes are copied from yantra-v2 68ea20a:
// data/demo/fraunces/fraunces_demo.json and
// plugins/tetra/demo/scenarios/fraunces_indoor.json (building part).
#include "yantra/core/demo/demo_project.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <sstream>

#include <nlohmann/json.hpp>

#include "yantra/core/building/edit.h"
#include "yantra/core/celldb/csv.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/plan/import.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/recording/writer.h"
#include "yantra/core/survey/recordings.h"
#include "yantra/core/survey/walk_test.h"
#include "yantra/core/util/sha256.h"

namespace yantra::demo {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

[[noreturn]] void fail(const fs::path& file, const std::string& what) {
    throw DemoError(file.filename().string() + ": " + what);
}

Point2 point(const json& j, const fs::path& file, const std::string& where) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number())
        fail(file, where + " is not a point [x, y]");
    const Point2 p{j[0].get<double>(), j[1].get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) fail(file, where + " is not finite");
    return p;
}

std::vector<Point2> points(const json& j, const fs::path& file, const std::string& where) {
    if (!j.is_array()) fail(file, where + " is not a list of points");
    std::vector<Point2> out;
    for (std::size_t i = 0; i < j.size(); ++i) out.push_back(point(j[i], file, where + "[" + std::to_string(i) + "]"));
    return out;
}

// "YYYY-MM-DDTHH:MM:SSZ" -> ns since 1970.
std::optional<std::int64_t> utc_ns(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    if (text.size() != 20 || std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 ||
        z != 'Z')
        return std::nullopt;
    using namespace std::chrono;
    const year_month_day ymd{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (!ymd.ok() || h > 23 || mi > 59 || s > 59 || h < 0 || mi < 0 || s < 0) return std::nullopt;
    const auto t = sys_days{ymd} + hours{h} + minutes{mi} + seconds{s};
    return duration_cast<nanoseconds>(t.time_since_epoch()).count();
}

building::Antenna read_antenna(const json& j, const fs::path& file, const std::string& where) {
    building::Antenna a;
    a.id = j.value("id", "");
    a.name = j.value("name", "");
    a.model = j.value("model", "");
    a.position = point(j.at("position"), file, where + ".position");
    a.height_m = j.value("height_m", a.height_m);
    a.gain_dbi = j.value("gain_dbi", a.gain_dbi);
    if (j.contains("eirp_dbm")) a.eirp_dbm = j["eirp_dbm"].get<double>();
    if (j.contains("feed_power_dbm")) a.feed_power_dbm = j["feed_power_dbm"].get<double>();
    a.cable_loss_db = j.value("cable_loss_db", 0.0);
    a.splitter_loss_db = j.value("splitter_loss_db", 0.0);
    a.cell = j.value("cell", "");
    if (a.id.empty()) fail(file, where + " has no id");
    return a;
}

DemoFloor read_floor(const json& j, const fs::path& file, const std::string& where) {
    DemoFloor f;
    f.id = j.at("id").get<std::string>();
    f.example_id = j.value("example_id", "");
    f.name = j.at("name").get<std::string>();
    f.level = j.at("level").get<int>();
    f.height_m = j.value("height_m", f.height_m);
    f.sheet = j.at("sheet").get<std::string>();
    const json& trim = j.at("trim");
    if (!trim.is_array() || trim.size() != 2) fail(file, where + ".trim is not two corners");
    f.trim_a = point(trim[0], file, where + ".trim[0]");
    f.trim_b = point(trim[1], file, where + ".trim[1]");
    if (j.contains("scale_bar") == j.contains("scale")) fail(file, where + " needs either scale_bar or scale");
    if (j.contains("scale_bar")) {
        const json& bar = j.at("scale_bar");
        f.scale_a = point(bar.at("a"), file, where + ".scale_bar.a");
        f.scale_b = point(bar.at("b"), file, where + ".scale_bar.b");
        f.scale_length_m = bar.at("length_m").get<double>();
        if (!(f.scale_length_m > 0.0)) fail(file, where + ".scale_bar.length_m must be > 0");
    } else {
        const json& scale = j.at("scale");
        f.metres_per_unit = scale.at("metres_per_unit").get<double>();
        f.scale_approximate = scale.value("approximate", false);
        f.scale_note = scale.value("note", "");
        if (!(f.metres_per_unit > 0.0 && std::isfinite(f.metres_per_unit)))
            fail(file, where + ".scale.metres_per_unit must be > 0");
    }
    f.reference_point = point(j.at("reference_point"), file, where + ".reference_point");
    for (std::size_t i = 0; i < j.value("walls", json::array()).size(); ++i) {
        const json& w = j["walls"][i];
        const std::string at = where + ".walls[" + std::to_string(i) + "]";
        building::Wall wall;
        wall.id = w.value("id", "");
        wall.wall_class = w.value("class", "");
        wall.points = points(w.at("points"), file, at + ".points");
        if (w.contains("attenuation_db")) wall.attenuation_db = w["attenuation_db"].get<double>();
        f.walls.push_back(std::move(wall));
    }
    for (std::size_t i = 0; i < j.value("antennas", json::array()).size(); ++i)
        f.antennas.push_back(read_antenna(j["antennas"][i], file, where + ".antennas[" + std::to_string(i) + "]"));
    const json& walk = j.at("walk");
    f.walk.name = walk.at("name").get<std::string>();
    f.walk.speed_mps = walk.value("speed_mps", 1.0);
    f.walk.waypoints = points(walk.at("waypoints"), file, where + ".walk.waypoints");
    if (f.id.empty() || f.name.empty()) fail(file, where + " needs an id and a name");
    if (!(f.walk.speed_mps > 0.0 && f.walk.speed_mps <= 10.0)) fail(file, where + ".walk.speed_mps must be in (0, 10]");
    if (f.walk.waypoints.size() < 2) fail(file, where + ".walk needs at least 2 waypoints");
    return f;
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw DemoError("cannot read " + file.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool is_sha256(const std::string& h) {
    return h.size() == 64 &&
           std::all_of(h.begin(), h.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string where_to_get(const DemoSheet& s) {
    return s.source_url.empty() ? std::string() : "; get the unaltered file from " + s.source_url;
}

}  // namespace

const DemoFloor* DemoDescription::find_floor(const std::string& floor_id) const {
    for (const DemoFloor& f : floors)
        if (f.id == floor_id) return &f;
    return nullptr;
}

const DemoSheet* DemoDescription::find_sheet(const std::string& sheet_id) const {
    for (const DemoSheet& s : sheets)
        if (s.id == sheet_id) return &s;
    return nullptr;
}

DemoDescription read_description(const fs::path& file) {
    json j;
    try {
        j = json::parse(read_text(file));
    } catch (const json::exception& e) {
        fail(file, std::string("not JSON: ") + e.what());
    }
    if (!j.is_object() || j.value("format", "") != kFormat) fail(file, std::string("not a ") + kFormat + " file");
    if (j.value("version", 0) != kFormatVersion)
        fail(file, "version " + std::to_string(j.value("version", 0)) + " is not supported (1)");
    DemoDescription d;
    d.folder = file.parent_path();
    try {
        d.id = j.at("id").get<std::string>();
        d.label = j.at("label").get<std::string>();
        const json& p = j.at("project");
        d.project.name = p.at("name").get<std::string>();
        d.project.site = p.value("site", "");
        d.project.operator_name = p.value("operator_name", "");
        d.project.notes = p.value("notes", "");
        const json& b = j.at("building");
        d.building_name = b.at("name").get<std::string>();
        d.floor_attenuation_db = b.value("floor_attenuation_db", d.floor_attenuation_db);
        d.start_utc = j.at("start_utc").get<std::string>();
        d.cell_database = j.value("cell_database", "");
        for (const json& s : j.at("sheets")) {
            DemoSheet sheet;
            sheet.id = s.at("id").get<std::string>();
            sheet.file = s.at("file").get<std::string>();
            sheet.sha256 = s.at("sha256").get<std::string>();
            sheet.source_url = s.value("source_url", "");
            sheet.item_url = s.value("item_url", "");
            sheet.title = s.value("title", "");
            sheet.restricted = s.value("licence_restricted", false);
            sheet.size_bytes = s.value("size_bytes", std::uint64_t{0});
            sheet.media_type = s.value("media_type", "");
            sheet.width = s.value("width", 0);
            sheet.height = s.value("height", 0);
            sheet.units_per_inch = s.value("units_per_inch", 0.0);
            sheet.licence = s.value("licence", "");
            sheet.licence_url = s.value("licence_url", "");
            if (s.contains("credit")) {
                const json& c = s["credit"];
                sheet.credit.holder = c.value("holder", "");
                sheet.credit.designer = c.value("designer", "");
                sheet.credit.year = c.value("year", "");
                sheet.credit.inventory_number = c.value("inventory_number", "");
            }
            d.sheets.push_back(std::move(sheet));
        }
        const json& floors = j.at("floors");
        for (std::size_t i = 0; i < floors.size(); ++i)
            d.floors.push_back(read_floor(floors[i], file, "floors[" + std::to_string(i) + "]"));
        d.interpolation_cell = j.at("interpolation").at("cell").get<std::string>();
    } catch (const json::exception& e) {
        fail(file, e.what());
    }
    const auto t = utc_ns(d.start_utc);
    if (!t) fail(file, "start_utc is not YYYY-MM-DDTHH:MM:SSZ");
    d.start_utc_ns = *t;
    if (d.id.empty() || d.floors.empty()) fail(file, "needs an id and at least one floor");
    std::set<std::string> ids;
    for (const DemoFloor& f : d.floors) {
        if (!ids.insert(f.id).second) fail(file, "floor id " + f.id + " is used twice");
        if (!d.find_sheet(f.sheet)) fail(file, "floor " + f.id + ": unknown sheet " + f.sheet);
    }
    for (const DemoSheet& s : d.sheets) {
        if (!is_sha256(s.sha256) || s.file.empty() || s.file.find_first_of("/\\") != std::string::npos)
            fail(file, "sheet " + s.id + ": needs a file name and a SHA-256 (64 lower-case hex digits)");
        if (s.width < 0 || s.height < 0 || (s.width == 0) != (s.height == 0))
            fail(file, "sheet " + s.id + ": width and height must both be given and > 0");
        // DEMO-12: a restricted sheet is never shipped, so the description
        // holds what the project and the download need.
        if (s.restricted &&
            (s.size_bytes == 0 || s.media_type.empty() || s.width == 0 || s.source_url.rfind("https://", 0) != 0 ||
             s.licence.empty() || s.credit.holder.empty() || s.credit.inventory_number.empty()))
            fail(file, "sheet " + s.id +
                           ": a licence-restricted sheet needs size_bytes, media_type, width, height, an https "
                           "source_url, the licence and the credit (holder, inventory number)");
    }
    // Everything drawn lies on its floor's trimmed plan, and each trim on
    // its sheet.
    for (const DemoFloor& f : d.floors) {
        const DemoSheet* s = d.find_sheet(f.sheet);
        const double w = f.trim_b.x - f.trim_a.x;
        const double h = f.trim_b.y - f.trim_a.y;
        if (!(w > 0.0 && h > 0.0)) fail(file, "floor " + f.id + ": the trim's second corner must be right of and below the first");
        if (s->width > 0 && (f.trim_a.x < 0.0 || f.trim_a.y < 0.0 || f.trim_b.x > s->width || f.trim_b.y > s->height))
            fail(file, "floor " + f.id + ": the trim is not inside sheet " + s->id);
        const auto inside = [&](Point2 p, const std::string& what) {
            if (p.x < 0.0 || p.y < 0.0 || p.x > w || p.y > h)
                fail(file, "floor " + f.id + ": " + what + " (" + std::to_string(p.x) + ", " + std::to_string(p.y) +
                               ") is outside the trimmed plan");
        };
        for (const building::Wall& wall : f.walls)
            for (const Point2& p : wall.points) inside(p, "wall " + wall.id);
        for (const building::Antenna& a : f.antennas) inside(a.position, "antenna " + a.id);
        for (const Point2& p : f.walk.waypoints) inside(p, "walk waypoint");
        inside(f.reference_point, "the reference point");
    }
    return d;
}

bool example_id_matches(const std::string& pattern, const std::string& example_id) {
    if (example_id.empty() || pattern.empty()) return false;
    if (pattern.back() == '*') return example_id.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return pattern == example_id;
}

std::optional<fs::path> scenario_for_example(const fs::path& scenario_dir, const std::string& example_id) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(scenario_dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
    if (ec) throw DemoError("cannot list " + scenario_dir.string() + ": " + ec.message());
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        json j;
        try {
            j = json::parse(read_text(f));
        } catch (const json::exception&) {
            continue;
        }
        if (!j.is_object() || j.value("format", "") != "yantra-radios-scenario") continue;
        for (const json& id : j.value("example_ids", json::array()))
            if (id.is_string() && example_id_matches(id.get<std::string>(), example_id)) return f;
    }
    return std::nullopt;
}

std::optional<fs::path> check_sheet_file(const DemoSheet& sheet, const fs::path& folder) {
    const fs::path path = folder / sheet.file;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return std::nullopt;
    const std::string bytes = read_text(path);
    const std::string got = util::Sha256::of(bytes);
    if (got != sheet.sha256)
        throw DemoError("sheet " + sheet.id + ": " + path.string() + " is not the described file (SHA-256 " + got +
                        ", expected " + sheet.sha256 + ")" + where_to_get(sheet));
    if (sheet.restricted && bytes.size() != sheet.size_bytes)
        throw DemoError("sheet " + sheet.id + ": " + path.string() + " has " + std::to_string(bytes.size()) +
                        " bytes, the description " + std::to_string(sheet.size_bytes));
    if (sheet.width > 0) {
        std::unique_ptr<plan::PlanSource> src;
        try {
            src = plan::open_plan_source(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), sheet.file);
        } catch (const std::exception& e) {
            throw DemoError("sheet " + sheet.id + ": " + path.string() + " cannot be read: " + e.what());
        }
        const plan::PageSize size = src->page_size(0);
        if (size.width != sheet.width || size.height != sheet.height)
            throw DemoError("sheet " + sheet.id + ": " + path.string() + " is " + std::to_string(int(size.width)) + " x " +
                            std::to_string(int(size.height)) + " pixels, the description " + std::to_string(sheet.width) +
                            " x " + std::to_string(sheet.height));
    }
    return path;
}

std::shared_ptr<project::Project> create_project(const DemoDescription& d, const fs::path& file,
                                                 const CreateOptions& options) {
    // The sheets first: nothing is made from a sheet that is not the
    // described file.
    const fs::path restricted_folder = options.sheet_folder.empty() ? d.folder : options.sheet_folder;
    std::map<std::string, std::optional<fs::path>> sheet_file;
    for (const DemoSheet& s : d.sheets) {
        auto found = check_sheet_file(s, s.restricted ? restricted_folder : d.folder);
        if (!found && (!s.restricted || options.require_restricted_sheets))
            throw DemoError("sheet " + s.id + ": " + ((s.restricted ? restricted_folder : d.folder) / s.file).string() +
                            " is missing" + where_to_get(s));
        sheet_file[s.id] = std::move(found);
    }
    std::error_code ec;
    fs::remove(file, ec);
    fs::remove(fs::path(file.string() + "-wal"), ec);
    fs::remove(fs::path(file.string() + "-shm"), ec);
    std::shared_ptr<project::Project> p = project::Project::create(file, d.project);
    p->set_building_name(d.building_name);
    p->set_floor_attenuation_db(d.floor_attenuation_db);
    if (!d.cell_database.empty()) {
        std::vector<std::string> problems;
        const celldb::CellDatabase db = celldb::read_csv_file((d.folder / d.cell_database).string(), {}, &problems);
        if (!problems.empty()) throw DemoError(d.cell_database + ": " + problems.front());
        p->set_cell_database(db);  // also the building's cells (IND-12), which the antennas serve
    }
    std::map<std::string, std::string> source_of;  // sheet id -> project source id
    for (const DemoSheet& s : d.sheets) {
        const std::optional<fs::path>& path = sheet_file.at(s.id);
        if (!s.restricted) {
            auto imported = plan::PlanImport::add_file(*p, *path);  // records the page size
            source_of[s.id] = imported->source_id();
            continue;
        }
        // IND-03, DEMO-12: a reference by SHA-256, never a copy; the same
        // source whether or not the file was there (its path is a hint).
        project::SourceReference ref;
        ref.file_name = s.file;
        ref.media_type = s.media_type;
        ref.size = s.size_bytes;
        ref.sha256 = s.sha256;
        ref.reference = path ? fs::absolute(*path).lexically_normal().string() : s.file;
        const std::string id = p->add_source_reference(ref).id;
        p->set_source_pages(id, {project::SourcePage{double(s.width), double(s.height), s.units_per_inch}});
        source_of[s.id] = id;
    }
    for (const DemoFloor& df : d.floors) {
        building::Floor f;
        f.id = df.id;
        f.name = df.name;
        f.level = df.level;
        f.height_m = df.height_m;
        f.source = source_of.at(df.sheet);
        f.example_id = df.example_id;
        f.page = 0;
        f.trim = plan::rectangle_trim(df.trim_a, df.trim_b);
        const plan::PageSize size = plan::TrimTransform(f.trim).floor_size();
        f.plan_width = size.width;
        f.plan_height = size.height;
        const auto scale = df.metres_per_unit > 0.0 ? building::factor_scale(df.metres_per_unit)
                                                    : building::scale_bar(df.scale_a, df.scale_b, df.scale_length_m);
        if (!scale) throw DemoError("floor " + df.id + ": the scale is not usable");
        f.scale = *scale;
        f.reference_point = df.reference_point;
        f.walls = df.walls;
        f.antennas = df.antennas;
        p->add_floor(std::move(f));
    }
    return p;
}

std::vector<double> waypoint_times(const building::Building& b, const DemoFloor& f) {
    const building::Floor* bf = b.find_floor(f.id);
    if (!bf) throw DemoError("floor " + f.id + " is not in the building");
    const auto mpu = b.metres_per_unit(*bf);
    if (!mpu) throw DemoError("floor " + f.id + " has no scale");
    std::vector<double> t{0.0};
    const auto& w = f.walk.waypoints;
    for (std::size_t i = 1; i < w.size(); ++i)
        t.push_back(t.back() + std::hypot(w[i].x - w[i - 1].x, w[i].y - w[i - 1].y) * *mpu / f.walk.speed_mps);
    return t;
}

std::string walk_scenario(const std::string& base_scenario, const building::Building& b, const DemoFloor& f,
                          const std::string& scene, double truth_interval_s) {
    json s;
    try {
        s = json::parse(base_scenario);
    } catch (const json::exception& e) {
        throw DemoError(std::string("the base scenario is not JSON: ") + e.what());
    }
    if (!s.is_object() || s.value("format", "") != "yantra-radios-scenario")
        throw DemoError("the base scenario is not a yantra-radios scenario");
    json scene_json;
    try {
        scene_json = json::parse(scene);
    } catch (const json::exception& e) {
        throw DemoError("floor " + f.id + ": the demo scene is not JSON: " + e.what());
    }
    const building::Floor* bf = b.find_floor(f.id);
    const std::vector<double> times = waypoint_times(b, f);
    json path = json::array();
    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto m = b.to_plan_metres(*bf, f.walk.waypoints[i]);
        path.push_back({{"t", times[i]}, {"floor", f.id}, {"x_m", m->x}, {"y_m", m->y}});
    }
    s["floors"] = json::array({json{{"scene", std::move(scene_json)}}});
    s["path"] = {{"mode", "linear"}, {"points", path}};
    s["duration_s"] = std::ceil(times.back()) + 1.0;
    s["truth_interval_s"] = truth_interval_s;
    s.erase("example_ids");
    s["description"] = s.value("description", "") + " Walk \"" + f.walk.name + "\" on " + f.name +
                       " (generated by the Yantra demo builder).";
    return s.dump(1);
}

fs::path demo_scene_file(const fs::path& scene_dir, const std::string& example_id) {
    return scene_dir / (example_id + ".json");
}

std::optional<std::string> read_demo_scene(const fs::path& scene_dir, const std::string& example_id) {
    if (example_id.empty() || example_id.find_first_of("/\\") != std::string::npos) return std::nullopt;
    std::ifstream in(demo_scene_file(scene_dir, example_id), std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string walk_session_id(const DemoDescription& d, const DemoFloor& f) {
    return util::Sha256::of(d.id + "/" + f.id).substr(0, 16);
}

WalkRecording record_walk(const std::shared_ptr<project::Project>& project, const DemoDescription& d,
                          const DemoFloor& f, const measure::RecordFile& truth,
                          std::shared_ptr<measure::TechnologyCatalog> catalog, const fs::path& recordings) {
    const building::Building b = project->building();
    const std::vector<double> times = waypoint_times(b, f);
    const auto clock = std::make_shared<platform::ManualClock>(0, d.start_utc_ns);
    measure::MeasurementPipeline::Options po;
    po.catalog = std::move(catalog);
    po.clock = clock;
    auto pipeline = std::make_shared<measure::MeasurementPipeline>(std::move(po));

    recording::RecorderConfig rc;
    rc.folder = recordings;
    rc.flush_thread = false;
    rc.session_id = walk_session_id(d, f);
    recording::RadioInfo radio;
    radio.plugin_id = truth.plugin_id;
    radio.plugin_version = truth.plugin_version;
    radio.label = truth.plugin_name + " (simulated walk, " + d.label + ")";
    rc.info.radios.push_back(radio);
    rc.info.cell_db = d.cell_database;
    rc.info.notes = d.label + ": " + f.walk.name + " on " + f.name + ", simulated (" + d.project.name + ")";
    rc.info.project = recording::ProjectLink{project->path().filename().string(), f.id};
    const std::string name = f.id + "_walk";
    std::error_code ec;
    fs::create_directories(recordings, ec);
    for (const auto& e : fs::directory_iterator(recordings, ec))
        if (e.path().filename().string().rfind(name + "_", 0) == 0) fs::remove(e.path(), ec);
    auto recorder = recording::Recorder::create(pipeline, rc);

    survey::WalkTestOptions wo;
    wo.project = project;
    wo.floor_id = f.id;
    wo.mode = survey::WalkMode::Continuous;
    wo.name = f.walk.name;
    wo.radio = radio.label;
    wo.notes = d.label;
    wo.recorder = recorder;
    wo.clock = clock;
    auto walk = std::make_shared<survey::WalkTest>(wo);
    pipeline->add_observer(walk);

    // The pipeline clock follows the recorded times (record files start
    // at 0); every cycle that ends by `t` is emitted first.
    const auto advance_to = [&](std::int64_t t) {
        while (pipeline->next_cycle_ns() <= t) {
            clock->set(pipeline->next_cycle_ns());
            pipeline->tick();
        }
        if (t > clock->monotonic_ns()) clock->set(t);
    };
    const auto at_ns = [](double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); };
    std::string error;
    if (!recorder->start(name, error)) throw DemoError("recording " + name + ": " + error);
    const std::string recording_session = recorder->status().session_id;
    walk->start(0);
    std::size_t next = 0;
    // The walker clicks each waypoint when reaching it.
    const auto clicks_until = [&](std::int64_t t) {
        while (next < times.size() && at_ns(times[next]) <= t) {
            const std::int64_t tc = at_ns(times[next]);
            advance_to(tc);
            walk->click(tc, f.walk.waypoints[next]);
            ++next;
        }
    };
    measure::ReplayOptions ro;
    ro.key_from_device = false;
    ro.advance = [&](std::int64_t t) {
        clicks_until(t);
        advance_to(t);
    };
    measure::replay(truth, *pipeline, ro);
    std::int64_t last = 0;
    for (const measure::RecordFileLine& l : truth.lines) {
        if (const auto* r = std::get_if<measure::RadioRecord>(&l)) last = std::max(last, r->record.time.monotonic_ns);
        else last = std::max(last, std::get<measure::RadioEvent>(l).monotonic_ns);
    }
    clicks_until(std::max(last, at_ns(times.back())));
    if (next < times.size()) throw DemoError("floor " + f.id + ": the record file ends before the walk");
    advance_to(last + 1'000'000'000);  // close the last period
    walk->finish(clock->monotonic_ns());
    if (!recorder->flush(error)) throw DemoError("recording " + name + ": " + error);
    WalkRecording out;
    out.session_id = walk->session_id();
    out.recording_session = recording_session;
    out.points = walk->stored();
    out.files = recorder->status().files;
    recorder->stop();
    recorder->detach();
    pipeline->remove_observer(walk);
    if (out.files.empty())
        for (const auto& e : fs::directory_iterator(recordings, ec))
            if (e.path().filename().string().rfind(name + "_", 0) == 0) out.files.push_back(e.path());
    std::sort(out.files.begin(), out.files.end());
    for (const fs::path& file : out.files)
        survey::add_recording_file(*project, file, (recordings.filename() / file.filename()).generic_string());
    return out;
}

survey::Metric demo_metric(const project::Project& project, const DemoDescription& d) {
    const building::Building b = project.building();
    const building::Cell* c = b.find_cell(d.interpolation_cell);
    if (!c || c->carrier_hz == 0) throw DemoError("interpolation cell " + d.interpolation_cell + " has no carrier");
    survey::Metric m;
    m.carrier = survey::Metric::Carrier::Frequency;
    m.value = survey::Metric::Value::Level;
    m.frequency_hz = c->carrier_hz;
    return m;
}

survey::InterpolationRequest interpolation_request(const project::Project& project, const DemoDescription& d,
                                                   const DemoFloor& f) {
    survey::InterpolationRequest r;
    r.floor_id = f.id;
    r.metric = demo_metric(project, d);
    return r;
}

std::vector<DemoScenario> read_demo_scenarios(const fs::path& file) {
    json j;
    try {
        j = json::parse(read_text(file));
    } catch (const json::exception& e) {
        fail(file, std::string("not JSON: ") + e.what());
    }
    if (!j.is_object() || j.value("format", "") != "yantra-demo-scenarios" || j.value("version", 0) != 1)
        fail(file, "not a yantra-demo-scenarios file of version 1");
    std::vector<DemoScenario> out;
    try {
        for (const json& s : j.at("scenarios"))
            out.push_back({s.at("id").get<std::string>(), s.at("title").get<std::string>(), s.value("shows", "")});
    } catch (const json::exception& e) {
        fail(file, e.what());
    }
    return out;
}

}  // namespace yantra::demo
