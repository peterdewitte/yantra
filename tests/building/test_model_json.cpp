// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JSON form of the building model ("yantra-building" v1): golden file of
// the example building, round trip, versioning and error places.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <yantra/core/building/model_json.h>

#include "example_building.h"
#include "golden.hpp"

using namespace yantra::building;
using yantra::tests::example_office;

namespace {

const std::filesystem::path kDir = std::filesystem::path(YANTRA_TEST_DATA_DIR) / "building";

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string parse_error(const std::string& text) {
    Building b;
    std::string error;
    CHECK_FALSE(parse_building(text, b, error));
    return error;
}

std::string with(const std::string& body) {
    return R"({"format": "yantra-building", "version": 1, )" + body + "}";
}

}  // namespace

TEST_CASE("the example building matches its golden JSON and reads back equal") {
    const Building b = example_office();
    const std::string text = building_to_json(b, 2) + "\n";
    const auto path = kDir / "example_office.building.json";
    if (yantra::tests::update_golden()) {
        std::ofstream(path, std::ios::binary) << text;
    }
    CHECK(text == read_file(path));
    Building back;
    std::string error;
    REQUIRE_MESSAGE(parse_building(read_file(path), back, error), error);
    CHECK(back == b);
    // Compact form: the same model.
    Building compact;
    REQUIRE(parse_building(building_to_json(b), compact, error));
    CHECK(compact == b);
}

TEST_CASE("an empty building and the default wall classes") {
    Building b;
    std::string error;
    REQUIRE_MESSAGE(parse_building(with(R"("floors": [])"), b, error), error);
    CHECK(b.wall_classes == default_wall_classes());
    CHECK(b.floors.empty());
    // A given table replaces the defaults.
    REQUIRE(parse_building(with(R"("wall_classes": [{"id": "w", "bands": [{"frequency_mhz": 400,
        "attenuation_db": 5}]}], "floors": [])"),
                           b, error));
    REQUIRE(b.wall_classes.size() == 1);
    CHECK(b.wall_classes[0].name == "w");
}

TEST_CASE("versioning: another format or a newer version is refused, unknown members ignored") {
    CHECK(parse_error(R"({"format": "other", "version": 1, "floors": []})") ==
          "format: expected \"yantra-building\"");
    CHECK(parse_error(R"({"format": "yantra-building", "version": 2, "floors": []})") ==
          "version: version 2 is newer than this reader (1)");
    CHECK(parse_error(R"({"format": "yantra-building", "floors": []})") == "version: missing");
    Building b;
    std::string error;
    CHECK_MESSAGE(parse_building(with(R"("future": {"x": 1}, "floors": [{"id": "f", "level": 0,
        "colour": "red", "walls": [{"class": "brick", "points": [[0, 0], [1, 1]], "thickness": 2}]}])"),
                                 b, error),
                  error);
}

TEST_CASE("reading errors name their place") {
    CHECK(parse_error("not json").rfind("not valid JSON", 0) == 0);
    CHECK(parse_error(with(R"("floors": {})")) == "floors: expected an array");
    CHECK(parse_error(with(R"("floors": [{"level": 0}])")) == "floors[0].id: missing");
    CHECK(parse_error(with(R"("floors": [{"id": "f", "level": 0, "walls": [{"class": "brick",
        "points": [[0, 0], [1]]}]}])")) == "floors[0].walls[0].points[1]: expected [x, y]");
    CHECK(parse_error(with(R"("floors": [{"id": "f", "level": 0, "walls": [{"class": "steel",
        "points": [[0, 0], [1, 0]]}]}])")) == "floors[0].walls[0]: unknown wall class \"steel\"");
    CHECK(parse_error(with(R"("floors": [{"id": "f", "level": 0, "scale": {"method": "inch"}}])")) ==
          "floors[0].scale.method: expected \"none\", \"two_points\", \"scale_bar\", \"factor\" or \"shared\"");
    CHECK(parse_error(with(R"("floors": [{"id": "f", "level": 0, "antennas": [{"id": "a",
        "position": [1, 2], "pattern": {"type": "dish"}}]}])")) ==
          "floors[0].antennas[0].pattern.type: expected \"omni\", \"sector\" or \"table\"");
    CHECK(parse_error(with(R"("cells": [{"id": "c", "identity": {"la": 1.5}}], "floors": [])")) ==
          "cells[0].identity.la: expected an integer");
    CHECK(parse_error(with(R"("floors": [{"id": "f", "level": 0, "trim": {"deskew_deg": 60}}])")) ==
          "floors[0].trim.deskew_deg: out of range -45.0..45.0");
}
