// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene JSON v1.1 (RAD-36): valid scenes, invalid ones with the path of
// the problem in the message, version handling (1.0 and 1.1 read, 2 refused)
// and the 1.1 members (storeys, polyline, bands).
#include <doctest/doctest.h>

#include <yantra/radios/scene.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace yantra::radios;

namespace {

std::string fixture_text(const char* name = "two_rooms_v1.json")
{
    std::ifstream in(std::string(YANTRA_TEST_DATA_DIR) + "/scenes/" + name);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// Replaces the first occurrence of `from` in the fixture with `to`.
std::string variant(const std::string& from, const std::string& to)
{
    std::string text = fixture_text();
    const auto pos = text.find(from);
    REQUIRE_MESSAGE(pos != std::string::npos, from);
    text.replace(pos, from.size(), to);
    return text;
}

std::string error_of(const std::string& text)
{
    Scene scene;
    std::string error;
    CHECK_FALSE(parse_scene(text, scene, error));
    return error;
}

} // namespace

TEST_CASE("scene: the example scene parses completely")
{
    Scene s;
    std::string error;
    REQUIRE_MESSAGE(parse_scene(fixture_text(), s, error), error);
    CHECK(s.version == 1);
    CHECK(s.floor.id == "f1");
    CHECK(s.floor.name == "Ground floor");
    CHECK(s.floor.scale_set);
    CHECK(s.floor.width_m == 20.0);
    REQUIRE(s.wall_classes.size() == 2);
    REQUIRE(s.walls.size() == 2);
    CHECK(s.walls[1].wall_class == "drywall");
    CHECK(s.walls[1].attenuation_db == 4.5); // overrides the class value
    REQUIRE(s.cells.size() == 2);
    CHECK(s.cells[1].la == 101);
    CHECK(s.cells[1].carrier_hz == 392'037'500u);
    REQUIRE(s.antennas.size() == 3);
    CHECK(s.antennas[0].pattern.type == ScenePattern::Type::omni);
    CHECK(s.antennas[1].pattern.type == ScenePattern::Type::sector);
    CHECK(s.antennas[1].pattern.beamwidth_deg == 90.0);
    CHECK(s.antennas[2].pattern.type == ScenePattern::Type::table);
    CHECK(s.antennas[2].pattern.gain_db.size() == 4);
    REQUIRE(s.find_cell("c2") != nullptr);
    CHECK(s.find_cell("c2")->colour_code == 2);
    CHECK(s.find_cell("c9") == nullptr);
}

TEST_CASE("scene: writing and parsing again gives the same scene")
{
    Scene s;
    std::string error;
    REQUIRE(parse_scene(fixture_text(), s, error));
    const std::string text = scene_to_json(s);
    Scene again;
    REQUIRE_MESSAGE(parse_scene(text, again, error), error);
    CHECK(again == s);
    CHECK(text.rfind("{\"format\":\"yantra-radio-scene\",\"version\":1,", 0) == 0);
}

TEST_CASE("scene: minimal scene without walls, cells or antennas")
{
    Scene s;
    std::string error;
    REQUIRE_MESSAGE(parse_scene(R"({"format":"yantra-radio-scene","version":1,
        "floor":{"id":"b","name":"","scale_set":false,"width_m":0,"height_m":0}})",
                                s, error),
                    error);
    CHECK(s.walls.empty());
    CHECK(s.antennas.empty());
    CHECK_FALSE(s.floor.scale_set);
}

TEST_CASE("scene: version and format")
{
    CHECK(error_of(variant("\"version\": 1", "\"version\": 2")) ==
          "scene version 2 is newer than this program reads (1)");
    CHECK(error_of(variant("\"version\": 1", "\"version\": 0")).find("invalid version") !=
          std::string::npos);
    CHECK(error_of(variant("\"version\": 1", "\"version\": \"1\"")) ==
          "version: must be an integer");
    CHECK(error_of(variant("yantra-radio-scene", "yantra-scene")).find("not a scene") !=
          std::string::npos);
    CHECK(error_of("{").find("not valid JSON") != std::string::npos);
    CHECK(error_of("[]").find("scene: must be an object") != std::string::npos);
}

TEST_CASE("scene: invalid content names where the problem is")
{
    CHECK(error_of(variant("\"cell\": \"c2\"", "\"cell\": \"c9\"")) ==
          "antennas[1].cell: unknown cell \"c9\"");
    CHECK(error_of(variant("\"class\": \"drywall\"", "\"class\": \"glass\"")) ==
          "walls[1].class: unknown wall class \"glass\"");
    CHECK(error_of(variant("\"id\": \"c2\"", "\"id\": \"c1\"")) == "cells[1].id: \"c1\" repeated");
    CHECK(error_of(variant("\"id\": \"a3\"", "\"id\": \"a1\"")) ==
          "antennas[2].id: \"a1\" repeated");
    CHECK(error_of(variant("\"mcc\": 901", "\"mcc\": -1")) ==
          "cells[0].mcc: must be an unsigned integer");
    CHECK(error_of(variant("\"carrier_hz\": 392012500", "\"carrier_hz\": 0")) ==
          "cells[0].carrier_hz: must be above 0");
    CHECK(error_of(variant("\"x1_m\": 10.0", "\"x1_m\": \"ten\"")) ==
          "walls[0].x1_m: must be a number");
    CHECK(error_of(variant("\"attenuation_db\": 4.5", "\"attenuation_db\": -1")) ==
          "walls[1].attenuation_db: must not be negative");
    CHECK(error_of(variant("\"type\": \"omni\"", "\"type\": \"dipole\"")) ==
          "antennas[0].pattern.type: unknown pattern \"dipole\" (omni, sector or table)");
    CHECK(error_of(variant("\"beamwidth_deg\": 90.0", "\"beamwidth_deg\": 0")) ==
          "antennas[1].pattern.beamwidth_deg: must be above 0 and at most 360");
    CHECK(error_of(variant("\"azimuth_deg\": 270.0", "\"azimuth_deg\": 360.0")) ==
          "antennas[1].pattern.azimuth_deg: must be in [0, 360)");
    CHECK(error_of(variant("\"step_deg\": 90.0", "\"step_deg\": 100.0")) ==
          "antennas[2].pattern.gain_db: covers more than 360 degrees");
    CHECK(error_of(variant("\"gain_db\": [0.0, -6.0, -15.0, -6.0]", "\"gain_db\": []")) ==
          "antennas[2].pattern.gain_db: must be a non-empty array of numbers");
    CHECK(error_of(variant("\"width_m\": 20.0", "\"width_m\": 0")) ==
          "floor: a floor with a scale needs width_m and height_m above 0");
    CHECK(error_of(variant("\"scale_set\": true", "\"scale_set\": 1")) ==
          "floor.scale_set: must be true or false");
    CHECK(error_of(variant("\"walls\": [", "\"walls\": {}, \"old_walls\": [")).find("walls: must be an array") !=
          std::string::npos);
    CHECK(error_of(variant("\"eirp_dbm\": 10.0, ", "")) ==
          "antennas[0]: member \"eirp_dbm\" missing");
    CHECK(error_of(R"({"format":"yantra-radio-scene","version":1})") ==
          "scene: member \"floor\" missing");
    CHECK(error_of(variant("\"id\": \"f1\"", "\"id\": \"\"")) == "floor.id: must not be empty");
}

TEST_CASE("scene: optional members for the multi-wall model (RAD-73)")
{
    // Absent: the reader's defaults apply, and the writer leaves them out.
    Scene plain;
    std::string error;
    REQUIRE(parse_scene(fixture_text(), plain, error));
    CHECK_FALSE(plain.floor.slab_attenuation_db.has_value());
    CHECK_FALSE(plain.floor.storey_height_m.has_value());
    CHECK_FALSE(plain.wall_classes[0].reflection_loss_db.has_value());
    CHECK(plain.antennas[0].floor_offset == 0);
    const std::string plain_text = scene_to_json(plain);
    CHECK(plain_text.find("floor_offset") == std::string::npos);
    CHECK(plain_text.find("reflection_loss_db") == std::string::npos);
    CHECK(plain_text.find("slab_attenuation_db") == std::string::npos);

    std::string text = variant("\"width_m\": 20.0,",
                               "\"width_m\": 20.0, \"slab_attenuation_db\": 18.5, \"storey_height_m\": 3.2,");
    auto replace = [&](const std::string& from, const std::string& to) {
        const auto pos = text.find(from);
        REQUIRE(pos != std::string::npos);
        text.replace(pos, from.size(), to);
    };
    replace("\"attenuation_db\": 12.0 },", "\"attenuation_db\": 12.0, \"reflection_loss_db\": 5.5 },");
    replace("\"eirp_dbm\": 13.0,", "\"eirp_dbm\": 13.0, \"floor_offset\": -1,");
    Scene s;
    REQUIRE_MESSAGE(parse_scene(text, s, error), error);
    CHECK(s.version == 1);
    CHECK(s.floor.slab_attenuation_db == 18.5);
    CHECK(s.floor.storey_height_m == 3.2);
    REQUIRE(s.find_wall_class("concrete") != nullptr);
    CHECK(s.find_wall_class("concrete")->reflection_loss_db == 5.5);
    CHECK_FALSE(s.find_wall_class("drywall")->reflection_loss_db.has_value());
    CHECK(s.find_wall_class("glass") == nullptr);
    CHECK(s.antennas[1].floor_offset == -1);
    Scene again;
    REQUIRE_MESSAGE(parse_scene(scene_to_json(s), again, error), error);
    CHECK(again == s);

    CHECK(error_of(variant("\"width_m\": 20.0,", "\"width_m\": 20.0, \"slab_attenuation_db\": -1,")) ==
          "floor.slab_attenuation_db: must not be negative");
    CHECK(error_of(variant("\"width_m\": 20.0,", "\"width_m\": 20.0, \"storey_height_m\": 0,")) ==
          "floor.storey_height_m: must be above 0");
    CHECK(error_of(variant("\"attenuation_db\": 12.0 },",
                           "\"attenuation_db\": 12.0, \"reflection_loss_db\": \"x\" },")) ==
          "wall_classes[0].reflection_loss_db: must be a number");
    CHECK(error_of(variant("\"eirp_dbm\": 13.0,", "\"eirp_dbm\": 13.0, \"floor_offset\": 0.5,")) ==
          "antennas[1].floor_offset: must be an integer from -200 to 200");
}

TEST_CASE("scene: minor version 1.1 is read, a 1.0 scene stays 1.0")
{
    Scene v10;
    std::string error;
    REQUIRE(parse_scene(fixture_text(), v10, error));
    CHECK(v10.minor_version == 0);
    CHECK_FALSE(v10.uses_minor_1());
    const std::string text10 = scene_to_json(v10);
    CHECK(text10.find("minor_version") == std::string::npos);
    CHECK(text10.find("polyline") == std::string::npos);
    CHECK(text10.find("bands") == std::string::npos);
    CHECK(text10.find("storeys") == std::string::npos);

    // A newer minor of major 1 is read (its unknown members ignored); the
    // writer keeps the minor it read.
    Scene later;
    REQUIRE_MESSAGE(parse_scene(variant("\"version\": 1,", "\"version\": 1, \"minor_version\": 7, \"future\": {},"),
                                later, error),
                    error);
    CHECK(later.minor_version == 7);
    CHECK(scene_to_json(later).find("\"minor_version\":7") != std::string::npos);
    CHECK(error_of(variant("\"version\": 1,", "\"version\": 1, \"minor_version\": -1,")) ==
          "scene.minor_version: must be an unsigned integer");
    CHECK(error_of(variant("\"version\": 1,", "\"version\": 2, \"minor_version\": 0,")) ==
          "scene version 2 is newer than this program reads (1)");

    // A 1.1 member raises the written minor version to 1.
    Scene raised = v10;
    raised.walls[0].polyline = "p";
    CHECK(raised.uses_minor_1());
    CHECK(scene_to_json(raised).find("\"version\":1,\"minor_version\":1,") != std::string::npos);
}

TEST_CASE("scene: the 1.1 members storeys, polyline and bands (RAD-36, RAD-73)")
{
    Scene s;
    std::string error;
    REQUIRE_MESSAGE(parse_scene(fixture_text("multiwall_v1_1.json"), s, error), error);
    CHECK(s.version == 1);
    CHECK(s.minor_version == 1);
    REQUIRE(s.floor.storeys.size() == 2);
    CHECK(s.floor.storeys[0] == SceneStorey{1, 4.5, 12.0});
    CHECK(s.floor.storeys[1] == SceneStorey{-1, -3.5, 20.0});
    CHECK(s.floor.find_storey(-1) == &s.floor.storeys[1]);
    REQUIRE(s.walls.size() == 3);
    CHECK(s.walls[0].polyline == "v");
    CHECK(s.walls[1].polyline == "v");
    CHECK(s.walls[2].polyline.empty());
    CHECK(s.walls[0].bands.empty());
    REQUIRE(s.walls[2].bands.size() == 2);
    CHECK(s.walls[2].bands[1] == SceneBand{450000000, 9.0});
    Scene again;
    const std::string text = scene_to_json(s);
    REQUIRE_MESSAGE(parse_scene(text, again, error), error);
    CHECK(again == s);
    CHECK(text.rfind("{\"format\":\"yantra-radio-scene\",\"version\":1,\"minor_version\":1,", 0) == 0);

    const auto bad = [](const std::string& from, const std::string& to) {
        std::string t = fixture_text("multiwall_v1_1.json");
        const auto pos = t.find(from);
        REQUIRE_MESSAGE(pos != std::string::npos, from);
        t.replace(pos, from.size(), to);
        return error_of(t);
    };
    CHECK(bad("\"floor_offset\": 1, \"level_m\"", "\"floor_offset\": 0, \"level_m\"") ==
          "floor.storeys[0].floor_offset: must be an integer from -200 to 200, not 0");
    CHECK(bad("\"floor_offset\": -1, \"level_m\"", "\"floor_offset\": 1, \"level_m\"") ==
          "floor.storeys[1].floor_offset: 1 repeated");
    CHECK(bad("\"level_m\": 4.5", "\"level_m\": -4.5") ==
          "floor.storeys[0].level_m: must be above 0 for a floor above");
    CHECK(bad("\"level_m\": -3.5", "\"level_m\": 0") ==
          "floor.storeys[1].level_m: must be below 0 for a floor below");
    CHECK(bad("\"slab_attenuation_db\": 12.0", "\"slab_attenuation_db\": -1") ==
          "floor.storeys[0].slab_attenuation_db: must not be negative");
    CHECK(bad("\"storeys\": [", "\"storeys\": {}, \"x\": [") == "floor.storeys: must be an array");
    CHECK(bad("\"polyline\": \"v\"", "\"polyline\": 3") == "walls[0].polyline: must be a string");
    CHECK(bad("\"frequency_hz\": 450000000", "\"frequency_hz\": 380000000") ==
          "walls[2].bands[1].frequency_hz: must be above the previous band's");
    CHECK(bad("\"frequency_hz\": 380000000", "\"frequency_hz\": 0") ==
          "walls[2].bands[0].frequency_hz: must be above 0");
    CHECK(bad("\"attenuation_db\": 9.0", "\"attenuation_db\": -9.0") ==
          "walls[2].bands[1].attenuation_db: must not be negative");
    CHECK(bad("\"bands\": [", "\"bands\": [], \"old\": [") == "walls[2].bands: must be a non-empty array");
}
