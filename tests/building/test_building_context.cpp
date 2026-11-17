// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Context snapshots from the building model (PLG-27, RAD-18, TST-50): the
// floor plan shown, the cursor and walk positions converted to plan-frame
// metres with the floor scale (IND-08) exactly, the scenes of every floor
// from a scene source (a test source here; in the app the fixed demo
// scenes by example id, DEMO-05: a user's own floor has none), and the
// change events.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include <yantra/core/context/building_context.h>
#include <yantra/core/demo/demo_project.h>

#include "example_building.h"

using namespace yantra::context;
using yantra::building::Building;
using yantra::building::Point2;
using yantra::building::Scale;
using yantra::tests::example_office;

namespace {

/// A test scene source: a scene text made of the floor's id, name and its
/// revision in `revisions` (a test changes a floor's scene by counting up).
SceneSource named_scenes(std::shared_ptr<std::map<std::string, int>> revisions = std::make_shared<std::map<std::string, int>>()) {
    return [revisions](const Building&, const yantra::building::Floor& f) -> std::optional<std::string> {
        return nlohmann::json{{"floor", {{"id", f.id}, {"name", f.name}, {"revision", (*revisions)[f.id]}}}}.dump();
    };
}

}  // namespace

TEST_CASE("floor plan of a floor (RAD-18 get_floor_plan)") {
    Building b = example_office();
    b.find_floor("f0")->example_id = "example-office-0";
    const FloorPlan p = floor_plan_of(b, *b.find_floor("f0"));
    CHECK(p.plan_name == "Example office, Ground floor");
    CHECK(p.example_id == "example-office-0");
    CHECK(p.floor_id == "f0");
    CHECK(p.floor_name == "Ground floor");
    CHECK(p.scale_set);
    CHECK(p.width_m == 400.0 * 0.05);
    CHECK(p.height_m == 200.0 * 0.05);
    b.find_floor("f0")->scale = Scale{};
    const FloorPlan n = floor_plan_of(b, *b.find_floor("f0"));
    CHECK_FALSE(n.scale_set);
    CHECK(n.width_m == 0.0);
    CHECK(floor_plan_of(b, *b.find_floor("f1")).example_id.empty());
}

TEST_CASE("the snapshot matches the model and the converted mouse position exactly (TST-50)") {
    auto pub = std::make_shared<ContextPublisher>();
    BuildingContext ctx(pub, named_scenes());
    const Building b = example_office();
    ctx.set_building(b);
    auto s = pub->snapshot();
    CHECK_FALSE(s->plan);  // nothing shown yet: NO_PLAN
    REQUIRE(s->scenes.size() == 2);
    CHECK(s->scenes.at("f0") == *named_scenes()(b, *b.find_floor("f0")));
    CHECK(s->scenes.at("f1") == *named_scenes()(b, *b.find_floor("f1")));

    CHECK(ctx.show_floor("f1") == kPlanChanged);
    s = pub->snapshot();
    REQUIRE(s->plan);
    CHECK(*s->plan == floor_plan_of(b, *b.find_floor("f1")));

    const Point2 mouse{123.25, 77.5};
    CHECK(ctx.set_cursor(mouse) == kCursorMoved);
    s = pub->snapshot();
    REQUIRE(s->cursor);
    CHECK(s->cursor->floor_id == "f1");
    CHECK(s->cursor->floor_name == "First floor");
    CHECK(s->cursor->x_m == mouse.x * 0.05);  // plan units x metres per unit, exactly
    CHECK(s->cursor->y_m == mouse.y * 0.05);
    CHECK(s->cursor->moved_monotonic_ns > 0);
    CHECK(ctx.set_cursor(std::nullopt) == kCursorMoved);
    CHECK_FALSE(pub->snapshot()->cursor);

    CHECK(ctx.set_walk(FloorPoint{"f0", {200, 100}}) == kWalkPosition);
    s = pub->snapshot();
    REQUIRE(s->walk);
    CHECK(s->walk->floor_id == "f0");
    CHECK(s->walk->x_m == 200.0 * 0.05);
    CHECK(s->walk->y_m == 100.0 * 0.05);
    CHECK(ctx.set_walk(std::nullopt) == kWalkPosition);
    CHECK(ctx.set_walk(FloorPoint{"zz", {1, 1}}) == 0);  // unknown floor: no position
}

TEST_CASE("edits: SCENE_CHANGED for the shown floor, positions follow a new scale") {
    auto pub = std::make_shared<ContextPublisher>();
    auto revisions = std::make_shared<std::map<std::string, int>>();
    BuildingContext ctx(pub, named_scenes(revisions));
    Building b = example_office();
    ctx.set_building(b);
    ctx.show_floor("f0");
    ctx.set_cursor(Point2{100, 100});
    ctx.set_walk(FloorPoint{"f0", {40, 40}});
    const auto before = pub->snapshot();

    // The same model again: nothing changes (also not the time stamps).
    CHECK(ctx.set_building(b) == 0);
    CHECK(pub->snapshot()->cursor == before->cursor);

    // The shown floor's scene changes.
    ++(*revisions)["f0"];
    CHECK(ctx.set_building(b) == kSceneChanged);
    CHECK(pub->snapshot()->scenes.at("f0") != before->scenes.at("f0"));

    // The other floor's scene changes, the shown one not: no event.
    ++(*revisions)["f1"];
    CHECK(ctx.set_building(b) == 0);

    // A new scale: plan size, cursor and walk metres follow.
    b.find_floor("f0")->scale = Scale{Scale::Method::Factor, {}, {}, 0.0, 0.1, ""};
    const std::uint32_t ev = ctx.set_building(b);
    CHECK((ev & kPlanChanged) != 0);
    CHECK((ev & kCursorMoved) != 0);
    CHECK((ev & kWalkPosition) != 0);
    const auto s = pub->snapshot();
    CHECK(s->plan->width_m == 400.0 * 0.1);
    CHECK(s->cursor->x_m == 100.0 * 0.1);
    CHECK(s->walk->x_m == 40.0 * 0.1);

    // Without a scale: no positions (NO_POSITION), scale_set false.
    b.find_floor("f0")->scale = Scale{};
    ctx.set_building(b);
    CHECK_FALSE(pub->snapshot()->plan->scale_set);
    CHECK_FALSE(pub->snapshot()->cursor);
    CHECK(ctx.set_cursor(Point2{1, 1}) == 0);
    CHECK_FALSE(pub->snapshot()->cursor);

    // Another floor shown: PLAN_CHANGED (its scene comes with it).
    CHECK(ctx.show_floor("f1") == kPlanChanged);
    // A removed floor is no longer shown.
    b.floors.pop_back();
    CHECK((ctx.set_building(b) & kPlanChanged) != 0);
    CHECK_FALSE(pub->snapshot()->plan);
    CHECK_FALSE(ctx.shown_floor());
    CHECK(ctx.show_floor("zz") == 0);
}

namespace {

namespace fs = std::filesystem;

fs::path scratch_dir(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path& f, const std::string& text) { std::ofstream(f, std::ios::binary) << text; }

}  // namespace

TEST_CASE("DEMO-05 fixed demo scenes: an example floor gets its scene file as it is, a user's own floor none (PLG-27)") {
    const fs::path dir = scratch_dir("demo_scenes");
    const std::string text = "{\n  \"format\": \"yantra-radio-scene\",\n  \"version\": 1,\n"
                             "  \"floor\": {\"id\": \"f0\", \"name\": \"Ground floor\", \"scale_set\": true,"
                             " \"width_m\": 20.0, \"height_m\": 10.0}\n}\n";
    write_file(yantra::demo::demo_scene_file(dir, "example-office-0"), text);

    Building b = example_office();  // walls and antennas on both floors
    b.find_floor("f0")->example_id = "example-office-0";
    auto pub = std::make_shared<ContextPublisher>();
    BuildingContext ctx(pub, demo_scenes({dir / "missing", dir}));
    ctx.set_building(b);
    auto s = pub->snapshot();
    REQUIRE(s->scenes.size() == 1);
    CHECK(s->scenes.at("f0") == text);  // byte for byte, not the model's walls and antennas
    CHECK_FALSE(s->scenes.count("f1"));  // the user's own floor: no scene (NOT_FOUND)

    // An example id without a scene file: no scene either.
    b.find_floor("f1")->example_id = "example-office-9";
    CHECK(ctx.set_building(b) == 0);
    CHECK(pub->snapshot()->scenes.size() == 1);

    // Walls and antennas of the model never change the scene.
    b.find_floor("f0")->walls.clear();
    b.find_floor("f0")->antennas.clear();
    ctx.show_floor("f0");
    CHECK(ctx.set_building(b) == 0);
    CHECK(pub->snapshot()->scenes.at("f0") == text);

    // Without a scene source no floor has a scene.
    auto pub2 = std::make_shared<ContextPublisher>();
    BuildingContext none(pub2);
    none.set_building(b);
    CHECK(pub2->snapshot()->scenes.empty());
}

TEST_CASE("DEMO-05 a floor with an example id but another floor id gets the scene with its own floor id") {
    const fs::path dir = scratch_dir("demo_scenes_id");
    const std::string text = "{\"format\": \"yantra-radio-scene\", \"version\": 1, \"floor\": {\"id\": \"first-floor\","
                             " \"name\": \"First floor\", \"scale_set\": true, \"width_m\": 20.0, \"height_m\": 10.0},"
                             " \"walls\": [{\"x1_m\": 0.1, \"y1_m\": 0.0, \"x2_m\": 5.0, \"y2_m\": 0.0,"
                             " \"attenuation_db\": 8.0}]}";
    write_file(yantra::demo::demo_scene_file(dir, "example-office-0"), text);
    Building b = example_office();
    b.find_floor("f0")->example_id = "example-office-0";  // a tour floor: the user's floor id
    auto pub = std::make_shared<ContextPublisher>();
    BuildingContext ctx(pub, demo_scenes({dir}));
    ctx.set_building(b);
    const auto got = nlohmann::json::parse(pub->snapshot()->scenes.at("f0"));
    auto want = nlohmann::json::parse(text);
    CHECK(got["floor"]["id"] == "f0");
    want["floor"]["id"] = "f0";
    CHECK(got == want);  // only the floor id differs
}
