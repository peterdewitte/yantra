// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Context snapshots from the building model (PLG-27, RAD-18, TST-50): the
// floor plan shown, the cursor and walk positions converted to plan-frame
// metres with the floor scale (IND-08) exactly, the scenes of every floor,
// and the change events.

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include <yantra/core/context/building_context.h>
#include <yantra/core/radio/scene_export.h>

#include "example_building.h"

using namespace yantra::context;
using yantra::building::Building;
using yantra::building::Point2;
using yantra::building::Scale;
using yantra::tests::example_office;

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
    BuildingContext ctx(pub);
    const Building b = example_office();
    ctx.set_building(b);
    auto s = pub->snapshot();
    CHECK_FALSE(s->plan);  // nothing shown yet: NO_PLAN
    REQUIRE(s->scenes.size() == 2);
    CHECK(s->scenes.at("f0") == yantra::radio::export_scene(b, "f0").json);
    CHECK(s->scenes.at("f1") == yantra::radio::export_scene(b, "f1").json);
    CHECK(ctx.scene_warnings().at("f0").size() == 2);

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
    BuildingContext ctx(pub);
    Building b = example_office();
    ctx.set_building(b);
    ctx.show_floor("f0");
    ctx.set_cursor(Point2{100, 100});
    ctx.set_walk(FloorPoint{"f0", {40, 40}});
    const auto before = pub->snapshot();

    // The same model again: nothing changes (also not the time stamps).
    CHECK(ctx.set_building(b) == 0);
    CHECK(pub->snapshot()->cursor == before->cursor);

    // A new wall on the shown floor.
    b.find_floor("f0")->walls.push_back({"w9", "door", {{0, 50}, {50, 50}}, std::nullopt});
    CHECK(ctx.set_building(b) == kSceneChanged);
    CHECK(pub->snapshot()->scenes.at("f0") != before->scenes.at("f0"));

    // A wall on the other floor: f1's scene changes, the shown one not.
    b.find_floor("f1")->walls.clear();
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
