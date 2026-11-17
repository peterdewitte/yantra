// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Weak areas of a floor (survey/weak_areas.h; EXP-06, DEMO-16).

#include "yantra/core/survey/weak_areas.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

namespace ys = yantra::survey;

namespace {

ys::SurveyPoint point(double x_m, double y_m, std::optional<double> value, const std::string& unit = "dBm",
                      const std::string& label = "") {
    ys::SurveyPoint p;
    p.plan = {x_m * 10.0, y_m * 10.0};
    p.metres = ys::Point2{x_m, y_m};
    p.value = value;
    p.unit = unit;
    p.label = label;
    return p;
}

}  // namespace

TEST_CASE("DEMO-16 weak areas: points below the threshold grouped by distance, largest first") {
    std::vector<ys::SurveyPoint> pts{
        point(0, 0, -80),  point(1, 0, -96), point(3.5, 0, -100), point(6, 0, -97),  // a chain of three, 2.5 m apart
        point(20, 20, -99), point(21, 21, -105),                                       // a pair
        point(40, 0, -95),                                                             // at the threshold: not weak
        point(40, 1, -120, "dBFS"),                                                    // other unit
        point(40, 2, std::nullopt),                                                    // no value
    };
    ys::SurveyPoint unscaled = point(41, 2, -110);
    unscaled.metres.reset();
    pts.push_back(unscaled);

    ys::WeakAreaOptions o;
    o.threshold = -95.0;
    const auto areas = ys::weak_areas(pts, o);
    REQUIRE(areas.size() == 2);
    CHECK(areas[0].points == 3);
    CHECK(areas[0].lowest == doctest::Approx(-100.0));
    CHECK(areas[0].min_m.x == doctest::Approx(1.0));
    CHECK(areas[0].max_m.x == doctest::Approx(6.0));
    CHECK(areas[0].label.empty());
    CHECK(areas[1].points == 2);
    CHECK(areas[1].lowest == doctest::Approx(-105.0));
    CHECK(areas[1].min_m.y == doctest::Approx(20.0));
    CHECK(areas[1].max_m.y == doctest::Approx(21.0));

    // A shorter link splits the chain; no weak point gives no area.
    o.link_m = 2.0;
    CHECK(ys::weak_areas(pts, o).size() == 4);
    o.threshold = -130.0;
    CHECK(ys::weak_areas(pts, o).empty());
}

TEST_CASE("DEMO-16 a weak area takes the most frequent label of its points") {
    const std::vector<ys::SurveyPoint> pts{point(0, 0, -100, "dBm", "Stairs"), point(1, 0, -100, "dBm", "Hall"),
                                           point(2, 0, -100, "dBm", "Hall"), point(3, 0, -100)};
    const auto areas = ys::weak_areas(pts, {});
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].label == "Hall");
}

TEST_CASE("DEMO-16 plan_part: the third of the plan of the area's centre") {
    const ys::Point2 size{30.0, 21.0};
    CHECK(ys::plan_part({1, 1}, {3, 3}, size) == "top-left");
    CHECK(ys::plan_part({14, 1}, {16, 2}, size) == "top");
    CHECK(ys::plan_part({25, 0}, {29, 2}, size) == "top-right");
    CHECK(ys::plan_part({0, 9}, {2, 11}, size) == "left");
    CHECK(ys::plan_part({11, 9}, {15, 13}, size) == "centre");
    CHECK(ys::plan_part({28, 10}, {29, 11}, size) == "right");
    CHECK(ys::plan_part({1, 18}, {2, 20}, size) == "bottom-left");
    CHECK(ys::plan_part({15, 20}, {15, 20}, size) == "bottom");
    CHECK(ys::plan_part({27, 19}, {29, 20}, size) == "bottom-right");
    CHECK(ys::plan_part({5, 5}, {5, 5}, {0.0, 0.0}) == "centre");  // no size
}
