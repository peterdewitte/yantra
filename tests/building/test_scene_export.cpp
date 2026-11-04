// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene export (RAD-36, PLG-27 get_scene): golden scene JSON of both
// floors of the example building (also checked by yantra-radios' own scene
// parser: ctests scene_radios_check_*), and the mapping rules of
// core/radio/scene_export.h.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>
#include <yantra/core/radio/scene_export.h>
#include <yantra/radio.h>

#include "example_building.h"
#include "golden.hpp"

using namespace yantra::building;
using yantra::radio::export_scene;
using yantra::radio::SceneExport;
using yantra::radio::SceneExportOptions;
using yantra::tests::example_office;
using nlohmann::json;

namespace {

const std::filesystem::path kDir = std::filesystem::path(YANTRA_TEST_DATA_DIR) / "building";

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

json scene_of(const Building& b, const std::string& floor, const SceneExportOptions& o = {}) {
    const SceneExport e = export_scene(b, floor, o);
    REQUIRE_MESSAGE(e.ok, e.error);
    return json::parse(e.json);
}

const json* antenna(const json& scene, const std::string& id) {
    for (const json& a : scene["antennas"])
        if (a["id"] == id) return &a;
    return nullptr;
}

bool warned(const SceneExport& e, const std::string& part) {
    for (const auto& w : e.warnings)
        if (w.find(part) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST_CASE("golden scenes of the example building (RAD-36)") {
    const Building b = example_office();
    for (const char* floor : {"f0", "f1"}) {
        INFO(floor);
        SceneExportOptions o;
        o.indent = 2;
        const SceneExport e = export_scene(b, floor, o);
        REQUIRE_MESSAGE(e.ok, e.error);
        const auto path = kDir / (std::string("example_office.") + floor + ".scene.json");
        if (yantra::tests::update_golden()) std::ofstream(path, std::ios::binary) << e.json << "\n";
        CHECK(e.json + "\n" == read_file(path));
        // The compact form (what get_scene answers) is the same JSON.
        CHECK(json::parse(export_scene(b, floor).json) == json::parse(e.json));
    }
}

TEST_CASE("scene of the ground floor: floor, classes, walls, cells, antennas") {
    const Building b = example_office();
    const SceneExport e = export_scene(b, "f0");
    REQUIRE(e.ok);
    CHECK(e.frequency_hz == 390e6);  // the carrier of T1
    const json s = json::parse(e.json);
    CHECK(s["format"] == YR_SCENE_FORMAT);
    CHECK(s["version"] == YR_SCENE_VERSION);
    CHECK(s["floor"]["id"] == "f0");
    CHECK(s["floor"]["name"] == "Ground floor");
    CHECK(s["floor"]["scale_set"] == true);
    CHECK(s["floor"]["width_m"] == 20.0);
    CHECK(s["floor"]["height_m"] == 10.0);
    CHECK(s["floor"]["slab_attenuation_db"] == 15.0);
    CHECK(s["floor"]["storey_height_m"] == 3.2);
    REQUIRE(s["wall_classes"].size() == 8);
    CHECK(s["wall_classes"][0]["id"] == "concrete");
    CHECK(s["wall_classes"][0]["attenuation_db"] == 12.0);  // 390 MHz: the 400 MHz band
    // Walls: one per polyline segment, in plan-frame metres.
    REQUIRE(s["walls"].size() == 4);
    CHECK(s["walls"][0] == json::parse(R"({"x1_m": 10.0, "y1_m": 0.0, "x2_m": 10.0, "y2_m": 10.0,
        "class": "concrete", "attenuation_db": 12.0})"));
    CHECK(s["walls"][2]["x1_m"] == 15.0);
    CHECK(s["walls"][2]["y2_m"] == 5.0);
    CHECK(s["walls"][3]["attenuation_db"] == 7.0);  // the wall's own loss
    // Cells: only those of exported antennas; identity by the radio API names.
    REQUIRE(s["cells"].size() == 1);
    CHECK(s["cells"][0] == json::parse(R"({"id": "T1", "label": "Indoor DAS", "mcc": 901, "mnc": 1,
        "la": 100, "colour_code": 1, "carrier_hz": 390000000})"));
    // Antennas: a1, a2 (EIRP from the feed), a3 from the floor above.
    REQUIRE(s["antennas"].size() == 3);
    const json* a1 = antenna(s, "a1");
    REQUIRE(a1);
    CHECK(*a1 == json::parse(R"({"id": "a1", "cell": "T1", "x_m": 3.0, "y_m": 5.0, "height_m": 2.8,
        "eirp_dbm": 10.0, "pattern": {"type": "omni"}})"));
    CHECK((*antenna(s, "a2"))["eirp_dbm"] == 13.0);
    CHECK((*antenna(s, "a2"))["pattern"]["type"] == "sector");
    const json* a3 = antenna(s, "a3");
    REQUIRE(a3);
    CHECK((*a3)["floor_offset"] == 1);
    CHECK((*a3)["x_m"] == 5.0);  // aligned by the reference points (IND-09)
    CHECK((*a3)["y_m"] == 5.0);
    // The f0|f1 slab is 20 dB, the scene's uniform slab 15: 5 dB in the EIRP.
    CHECK((*a3)["eirp_dbm"] == 12.0 - 5.0);
    CHECK((*a3)["pattern"]["type"] == "table");
    // What was left out, with the reason.
    CHECK(warned(e, "antenna \"donor\": outdoor donor directions are not part of the scene"));
    CHECK(warned(e, "antenna \"a4\": cell \"X9\" cannot be in a scene (it has no carrier)"));
    CHECK(e.warnings.size() == 2);
}

TEST_CASE("scene of the first floor: antennas from below and the shared scale") {
    const Building b = example_office();
    const json s = scene_of(b, "f1");
    CHECK(s["floor"]["scale_set"] == true);
    CHECK(s["floor"]["width_m"] == 20.0);  // f0's scale (one sheet, IND-08)
    REQUIRE(s["walls"].size() == 1);
    CHECK(s["walls"][0]["class"] == "plasterboard");
    CHECK(s["walls"][0]["x1_m"] == 5.0);
    const json* a1 = antenna(s, "a1");
    REQUIRE(a1);
    CHECK((*a1)["floor_offset"] == -1);
    CHECK((*a1)["x_m"] == 3.5);  // building (3, 5) m = f1 plan (70, 100) units
    CHECK((*a1)["eirp_dbm"] == 5.0);
    CHECK((*antenna(s, "a2"))["eirp_dbm"] == 8.0);
    CHECK(antenna(s, "a3"));
    CHECK(s["antennas"].size() == 3);
}

TEST_CASE("scene options: own floor only, frequency, default EIRP") {
    Building b = example_office();
    SceneExportOptions only;
    only.other_floors = false;
    CHECK(scene_of(b, "f0", only)["antennas"].size() == 2);
    SceneExportOptions f900;
    f900.frequency_hz = 900e6;
    const json s = scene_of(b, "f0", f900);
    CHECK(s["wall_classes"][0]["attenuation_db"] == 23.0);
    CHECK(s["walls"][0]["attenuation_db"] == 23.0);
    CHECK(s["walls"][3]["attenuation_db"] == 7.0);
    // An antenna without a known EIRP (IND-12) is left out unless a default is given.
    b.find_floor("f0")->antennas[0].eirp_dbm.reset();
    SceneExport e = export_scene(b, "f0", only);
    REQUIRE(e.ok);
    CHECK(warned(e, "antenna \"a1\": no EIRP, left out"));
    CHECK(json::parse(e.json)["antennas"].size() == 1);
    SceneExportOptions dflt = only;
    dflt.default_eirp_dbm = 0.0;
    const json d = scene_of(b, "f0", dflt);
    REQUIRE(antenna(d, "a1"));
    CHECK((*antenna(d, "a1"))["eirp_dbm"] == 0.0);
}

TEST_CASE("a floor without scale, without alignment, an unknown floor, an invalid model") {
    Building b = example_office();
    b.find_floor("f1")->scale = Scale{};
    SceneExport e = export_scene(b, "f1");
    REQUIRE(e.ok);
    const json s = json::parse(e.json);
    CHECK(s["floor"]["scale_set"] == false);
    CHECK(s["floor"]["width_m"] == 0.0);
    CHECK(s["walls"].empty());
    CHECK(s["antennas"].empty());
    CHECK(s["cells"].empty());
    CHECK(warned(e, "floor \"f1\" has no scale"));
    // The ground floor cannot place f1's antennas any more.
    e = export_scene(b, "f0");
    CHECK(warned(e, "floor \"f1\": not aligned with \"f0\""));
    CHECK(json::parse(e.json)["antennas"].size() == 2);
    // No reference point: not aligned either.
    b = example_office();
    b.find_floor("f1")->reference_point.reset();
    CHECK(warned(export_scene(b, "f0"), "floor \"f1\": not aligned"));
    // A cell without the radio identity.
    b = example_office();
    b.cells[0].identity.erase("colour_code");
    e = export_scene(b, "f0");
    CHECK(warned(e, "cannot be in a scene (its identity has no \"colour_code\")"));
    CHECK(json::parse(e.json)["antennas"].empty());
    CHECK(export_scene(b, "zz").error == "unknown floor \"zz\"");
    b.floors[0].walls[0].wall_class = "steel";
    e = export_scene(b, "f0");
    CHECK_FALSE(e.ok);
    CHECK(e.error == "the building model is not valid: floors[0].walls[0]: unknown wall class \"steel\"");
}
