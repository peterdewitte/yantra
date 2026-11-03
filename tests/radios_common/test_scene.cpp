// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene JSON v1 (RAD-36): valid scenes, invalid ones with the path of the
// problem in the message, and version handling.
#include <doctest/doctest.h>

#include <yantra/radios/scene.hpp>

#include <fstream>
#include <sstream>
#include <string>

using namespace yantra::radios;

namespace {

std::string fixture_text()
{
    std::ifstream in(std::string(YANTRA_TEST_DATA_DIR) + "/scenes/two_rooms_v1.json");
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
