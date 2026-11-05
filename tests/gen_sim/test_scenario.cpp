// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario files (RAD-74, RAD-93): every file in data/scenarios parses and
// validates, the selection by example id, invalid variants (each refused
// with the place of the problem), a newer version refused, unknown members
// ignored, the broadcast configuration of a cell and cells added from a
// scene.
#include <doctest/doctest.h>

#include "yantra/gen/scenario.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace yantra::gen;
namespace fs = std::filesystem;
namespace radios = yantra::radios;
using json = nlohmann::ordered_json;

namespace {

const std::string kDir = YANTRA_SCENARIO_DIR;

json minimal()
{
    return json::parse(R"({
      "format": "yantra-radios-scenario", "version": 1, "id": "t",
      "cells": [
        { "id": "A", "cell_number": 1, "la": 10, "colour_code": 1, "main_carrier": 3600,
          "frequency_band": 3, "neighbours": [ { "cell": "B" } ] },
        { "id": "B", "cell_number": 2, "la": 11, "colour_code": 2, "main_carrier": 3604,
          "frequency_band": 3 } ],
      "floors": [ { "scene": {
        "format": "yantra-radio-scene", "version": 1,
        "floor": { "id": "f", "name": "F", "scale_set": true, "width_m": 10, "height_m": 10 },
        "cells": [ { "id": "A", "mcc": 901, "mnc": 1, "la": 10, "colour_code": 1,
                     "carrier_hz": 390000000 } ],
        "antennas": [ { "id": "x", "cell": "A", "x_m": 1, "y_m": 1, "height_m": 2,
                        "eirp_dbm": 10, "pattern": { "type": "omni" } } ] } } ],
      "path": { "points": [ { "t": 0, "floor": "f", "x_m": 5, "y_m": 5 } ] },
      "events": [ { "t": 1, "type": "cell_off", "cell": "B" } ]
    })");
}

/// The error message of parsing `j`, "" when it parses.
std::string error_of(const json& j)
{
    try {
        parse_scenario(j.dump());
    } catch (const ScenarioError& e) {
        return e.what();
    }
    return {};
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("scenario: every bundled scenario file parses and validates (RAD-74)")
{
    size_t files = 0;
    for (const auto& e : fs::directory_iterator(kDir)) {
        if (e.path().extension() != ".json")
            continue;
        ++files;
        CAPTURE(e.path().string());
        Scenario s;
        REQUIRE_NOTHROW(s = load_scenario_file(e.path().string()));
        CHECK(s.id == e.path().stem().string());
        CHECK_FALSE(s.name.empty());
        CHECK_FALSE(s.description.empty());
        for (const ScenarioCell& c : s.cells)
            CHECK_NOTHROW(validate(cell_config(s, c)));
    }
    const auto list = list_scenarios(kDir);
    CHECK(list.size() == files);
    CHECK(files >= 9);
    size_t defaults = 0;
    for (const auto& i : list)
        defaults += i.is_default ? 1 : 0;
    CHECK(defaults == 1);

    // DEMO-09: good and weak DAS, floor change, failed site, weak signal.
    for (const char* id : {"indoor_good_das", "indoor_weak_das", "floor_change", "failed_site", "weak_signal"})
        CHECK(std::any_of(list.begin(), list.end(), [&](const ScenarioInfo& i) { return i.id == id; }));
}

TEST_CASE("scenario: selection by example id (RAD-18, RAD-74)")
{
    const auto list = list_scenarios(kDir);
    REQUIRE_FALSE(list.empty());
    auto pick = [&](const std::string& ex) { return select_scenario(list, ex).value().id; };
    CHECK(pick("fraunces-1") == "fraunces");
    CHECK(pick("mak-beer-house-1") == "mak_beer_house");
    CHECK(pick("mak-haus-claeson-2") == "mak_haus_claeson");
    CHECK(pick("mak-villa-bunzl-1") == "mak_villa_bunzl");
    CHECK(pick("") == "indoor_good_das");
    CHECK(pick("someone-elses-plan") == "indoor_good_das");

    std::vector<ScenarioInfo> own = {{"a.json", "a", "A", {"x-1"}, false}, {"b.json", "b", "B", {"x-*"}, false}};
    CHECK(select_scenario(own, "x-1")->id == "a");  // exact before a wildcard of an earlier file
    CHECK(select_scenario(own, "x-2")->id == "b");
    CHECK_FALSE(select_scenario(own, "y").has_value());  // no default
}

TEST_CASE("scenario: the DEMO-09 scenarios plant what they say")
{
    const Scenario fail = load_scenario_file(kDir + "/failed_site.json");
    const ScenarioCell* n3 = fail.find_cell("N3");
    REQUIRE(n3);
    CHECK_FALSE(n3->on_air);
    CHECK(fail.find_cell("N1")->neighbours.size() == 2);

    const Scenario fc = load_scenario_file(kDir + "/floor_change.json");
    REQUIRE(fc.floors.size() == 2);
    CHECK(fc.floors[0].file == "scenes/demo_office_f0.json");
    CHECK(fc.find_scene("f1") != nullptr);
    CHECK(fc.path.points.front().floor == "f0");
    CHECK(fc.path.points.back().floor == "f1");

    const Scenario weak = load_scenario_file(kDir + "/indoor_weak_das.json");
    const radios::Scene sc = apply_overrides(weak, *weak.find_scene("f0"));
    for (const radios::SceneAntenna& a : sc.antennas)
        CHECK(a.eirp_dbm == -35.0);
}

TEST_CASE("scenario: parse of the minimal document and defaults")
{
    const Scenario s = parse_scenario(minimal().dump());
    CHECK(s.version == 1);
    CHECK(s.mcc == 901);
    CHECK(s.mnc == 1);
    CHECK(s.seed == 1);
    CHECK(s.duration_s == 10.0);
    CHECK(s.sample_rate_hz == 500e3);
    CHECK(s.cells.size() == 2);
    CHECK(s.cells[0].level_dbm == -70.0);
    CHECK(s.channel.fading == FadingModel::none);
    CHECK(s.receiver.noise_figure_db == 7.0);
    CHECK_FALSE(s.path.linear);
    CHECK(cell_frequency_hz(s.cells[1]) == 390.1e6);
    CHECK(centre_frequency_hz(s) == 390.05e6);

    // Unknown members are ignored at every level (compatible additions).
    json j = minimal();
    j["future_member"] = {{"a", 1}};
    j["cells"][0]["future"] = true;
    j["receiver"] = {{"future", "x"}};
    CHECK(error_of(j).empty());
}

TEST_CASE("scenario: version and format checks")
{
    json j = minimal();
    j["version"] = 2;
    CHECK(contains(error_of(j), "version 2 is newer"));
    j["version"] = 0;
    CHECK(contains(error_of(j), "version"));
    j["version"] = "1";
    CHECK(contains(error_of(j), "version: must be an integer"));
    j = minimal();
    j["format"] = "yantra-radio-scene";
    CHECK(contains(error_of(j), "not a scenario"));
    j.erase("format");
    CHECK(contains(error_of(j), "not a scenario"));
    CHECK_THROWS_AS(parse_scenario("{ not json"), ScenarioError);
    CHECK_THROWS_AS(parse_scenario("[1, 2]"), ScenarioError);
}

TEST_CASE("scenario: invalid documents name the place of the problem")
{
    struct Case {
        const char* what;
        void (*change)(json&);
        const char* expect;
    };
    const Case cases[] = {
        {"missing id", [](json& j) { j.erase("id"); }, "member \"id\" missing"},
        {"no cells", [](json& j) { j["cells"] = json::array(); }, "cells: needs at least one cell"},
        {"repeated cell", [](json& j) { j["cells"][1]["id"] = "A"; }, "cells[1].id: \"A\" repeated"},
        {"unknown neighbour", [](json& j) { j["cells"][0]["neighbours"][0]["cell"] = "X9"; },
         "cells[0].neighbours[0].cell: unknown cell \"X9\""},
        {"own neighbour", [](json& j) { j["cells"][0]["neighbours"][0]["cell"] = "A"; }, "not its own neighbour"},
        {"carrier range", [](json& j) { j["cells"][0]["main_carrier"] = 5000; },
         "cells[0].main_carrier: must be from 0 to 4095"},
        {"carrier outside span", [](json& j) { j["cells"][1]["main_carrier"] = 3700; }, "does not fit the span"},
        {"string level", [](json& j) { j["cells"][0]["level_dbm"] = "-70"; }, "cells[0].level_dbm: must be a number"},
        {"time offset", [](json& j) { j["cells"][0]["time_offset_s"] = 61.2; }, "cells[0].time_offset_s"},
        {"cell number", [](json& j) { j["cells"][0]["cell_number"] = 32; }, "cells[0].cell_number"},
        {"scene cell differs", [](json& j) { j["floors"][0]["scene"]["cells"][0]["la"] = 99; },
         "scene cell \"A\" differs"},
        {"scene cell unknown",
         [](json& j) {
             j["floors"][0]["scene"]["cells"][0]["id"] = "Z";
             j["floors"][0]["scene"]["antennas"][0]["cell"] = "Z";
         },
         "scene cell \"Z\" is not a scenario cell"},
        {"bad scene", [](json& j) { j["floors"][0]["scene"]["format"] = "x"; }, "floors[0].scene"},
        {"both scene kinds", [](json& j) { j["floors"][0]["scene_file"] = "x.json"; }, "either \"scene\" or"},
        {"missing scene file", [](json& j) { j["floors"][0] = {{"scene_file", "nowhere.json"}}; },
         "floors[0].scene_file: cannot read"},
        {"fading model", [](json& j) { j["channel"] = {{"fading", {{"model", "jakes"}}}}; },
         "channel.fading.model: unknown fading model"},
        {"reflections", [](json& j) { j["channel"] = {{"multiwall", {{"reflections", 3}}}}; },
         "channel.multiwall.reflections"},
        {"negative penetration", [](json& j) { j["channel"] = {{"penetration_loss_db", -1}}; },
         "channel.penetration_loss_db"},
        {"quantise bits", [](json& j) { j["receiver"] = {{"quantise_bits", 1}}; }, "receiver.quantise_bits"},
        {"drops", [](json& j) { j["receiver"] = {{"drops", {{"interval_s", 0.001}, {"samples", 1000}}}}; },
         "receiver.drops.samples"},
        {"path mode", [](json& j) { j["path"]["mode"] = "spline"; }, "path.mode"},
        {"path floor", [](json& j) { j["path"]["points"][0]["floor"] = "g"; },
         "path.points[0].floor: unknown floor \"g\""},
        {"path time", [](json& j) { j["path"]["points"][1] = {{"t", 0}, {"floor", "f"}, {"x_m", 1}, {"y_m", 1}}; },
         "path.points[1].t: times must increase"},
        {"event type", [](json& j) { j["events"][0]["type"] = "explode"; }, "events[0].type: unknown event"},
        {"event cell", [](json& j) { j["events"][0]["cell"] = "Q"; }, "events[0].cell: unknown cell \"Q\""},
        {"event order",
         [](json& j) { j["events"].push_back({{"t", 0.5}, {"type", "cell_on"}, {"cell", "B"}}); },
         "events[1].t: events must be in time order"},
        {"level event without db", [](json& j) { j["events"][0]["type"] = "cell_level"; }, "events[0]: member \"db\""},
        {"duration", [](json& j) { j["duration_s"] = -1; }, "duration_s"},
        {"sample rate", [](json& j) { j["sample_rate_hz"] = 1000; }, "sample_rate_hz"},
        {"mcc", [](json& j) { j["network"] = {{"mcc", 2000}}; }, "network.mcc"},
        {"seed", [](json& j) { j["seed"] = -3; }, "seed: must be an unsigned integer"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.what);
        json j = minimal();
        c.change(j);
        const std::string e = error_of(j);
        CAPTURE(e);
        CHECK(contains(e, c.expect));
    }
}

TEST_CASE("scenario: broadcast configuration of a cell")
{
    json j = minimal();
    j["network"] = {{"mcc", 206}, {"mnc", 16}};
    j["floors"] = json::array();
    j["path"] = json::object();
    j["cells"][0]["encryption"] = true;
    j["cells"][0]["rxlev_access_min"] = 5;
    j["cells"][0]["neighbours"][0]["main_carrier"] = 3610;  // planted: announced carrier differs
    j["cells"][0]["neighbours"][0]["la"] = 77;
    j["cells"][1]["frequency_band"] = 4;
    j["cells"][1]["main_carrier"] = 0;
    j["centre_frequency_hz"] = 400e6;
    j["cells"][0]["main_carrier"] = 3990;  // 399.75 MHz
    j["sample_rate_hz"] = 1e6;
    const Scenario s = parse_scenario(j.dump());
    const CellConfig a = cell_config(s, s.cells[0]);
    CHECK(a.sync.colour_code == 1);
    CHECK(a.sync.mle.mcc == 206);
    CHECK(a.sync.mle.mnc == 16);
    CHECK(a.sysinfo.main_carrier == 3990);
    CHECK(a.sysinfo.mle.location_area == 10);
    CHECK(a.sysinfo.rxlev_access_min == 5);
    CHECK(a.sysinfo.mle.bs_service_details.air_interface_encryption);
    REQUIRE(a.nwrk_broadcast.has_value());
    REQUIRE(a.nwrk_broadcast->neighbours.has_value());
    REQUIRE(a.nwrk_broadcast->neighbours->size() == 1);
    const NeighbourCell& n = a.nwrk_broadcast->neighbours->front();
    CHECK(n.cell_id == 2);
    CHECK(n.main_carrier == 3610);
    CHECK(n.location_area == std::optional<uint16_t>(77));
    REQUIRE(n.carrier_extension.has_value());  // other band
    CHECK(n.carrier_extension->frequency_band == 4);
    CHECK(a.nwrk_broadcast->network_time.has_value());

    const CellConfig b = cell_config(s, s.cells[1]);
    REQUIRE(b.nwrk_broadcast.has_value());
    CHECK_FALSE(b.nwrk_broadcast->neighbours.has_value());
}

TEST_CASE("scenario: cells added from a scene")
{
    Scenario s = parse_scenario(minimal().dump());
    radios::Scene scene = s.floors[0].scene;
    scene.cells.push_back({"own", "Own cell", 901, 1, 12, 3, 390006250});
    scene.cells.push_back({"own2", "Second", 901, 1, 13, 4, 412512500});
    add_scene_cells(s, scene);
    REQUIRE(s.cells.size() == 4);
    const ScenarioCell* own = s.find_cell("own");
    REQUIRE(own);
    CHECK(own->main_carrier == 3600);
    CHECK(own->offset == 1);
    CHECK(own->frequency_band == 3);
    CHECK(own->location_area == 12);
    CHECK(own->colour_code == 3);
    CHECK(own->cell_number == 3);
    CHECK(cell_frequency_hz(*own) == 390006250.0);
    const ScenarioCell* own2 = s.find_cell("own2");
    CHECK(own2->frequency_band == 4);
    CHECK(own2->main_carrier == 500);
    CHECK(own2->offset == 3);
    CHECK(own2->cell_number == 4);

    radios::Scene bad = scene;
    bad.cells = {{"off", "Off raster", 901, 1, 1, 1, 390001000}};
    CHECK_THROWS_AS(add_scene_cells(s, bad), ScenarioError);
}

TEST_CASE("scenario: a scene cell on a scenario cell's carrier replaces that cell (RAD-61)")
{
    // B (390.1 MHz) is the carrier of the plan's cell "own": the plan's
    // cell takes B's place and cell identifier, A announces it instead of
    // B, and B's event goes. A (named by the scene) stays.
    Scenario s = parse_scenario(minimal().dump());
    radios::Scene scene;
    scene.cells.push_back({"own", "Own cell", 901, 1, 77, 3, 390100000});
    scene.cells.push_back({"A", "A", 901, 1, 10, 1, 390000000});
    add_scene_cells(s, scene);
    REQUIRE(s.cells.size() == 2);
    CHECK(s.find_cell("B") == nullptr);
    const ScenarioCell* own = s.find_cell("own");
    REQUIRE(own);
    CHECK(own == &s.cells[1]);
    CHECK(own->cell_number == 2);
    CHECK(own->location_area == 77);
    CHECK(cell_frequency_hz(*own) == 390100000.0);
    REQUIRE(s.find_cell("A")->neighbours.size() == 1);
    CHECK(s.find_cell("A")->neighbours[0].cell == "own");
    CHECK(s.events.empty());
    validate_scenario(s);

    // The scenario's floor loses the antennas of a replaced cell; a carrier
    // 6.25 kHz away is another carrier (added, nothing replaced).
    Scenario t = parse_scenario(minimal().dump());
    radios::Scene other;
    other.cells.push_back({"own", "Own cell", 901, 1, 77, 3, 390000000});
    other.cells.push_back({"near", "Near", 901, 1, 78, 3, 390106250});
    add_scene_cells(t, other);
    REQUIRE(t.cells.size() == 3);
    CHECK(t.find_cell("A") == nullptr);
    CHECK(t.find_cell("B") != nullptr);
    CHECK(t.find_cell("own")->cell_number == 1);
    CHECK(t.find_cell("near")->cell_number == 3);
    CHECK(t.floors[0].scene.cells.empty());
    CHECK(t.floors[0].scene.antennas.empty());
    validate_scenario(t);
}

TEST_CASE("scenario: scene files from a reader instead of the file system (RAD-61)")
{
    std::ifstream in(kDir + "/indoor_good_das.json");
    std::stringstream text;
    text << in.rdbuf();
    std::vector<std::string> asked;
    const Scenario from_reader = parse_scenario(text.str(), [&](const std::string& file) {
        asked.push_back(file);
        std::ifstream f(kDir + "/" + file);
        std::stringstream body;
        body << f.rdbuf();
        return body.str();
    });
    CHECK(asked == std::vector<std::string>{"scenes/demo_office_f0.json", "scenes/demo_office_f1.json"});
    const Scenario from_file = load_scenario_file(kDir + "/indoor_good_das.json");
    REQUIRE(from_reader.floors.size() == from_file.floors.size());
    for (size_t i = 0; i < from_file.floors.size(); ++i)
        CHECK(from_reader.floors[i].scene == from_file.floors[i].scene);
    // A reader without the file: the error names the floor.
    CHECK_THROWS_AS(parse_scenario(text.str(), [](const std::string& file) -> std::string {
                        throw ScenarioError("no " + file);
                    }),
                    ScenarioError);
}
