// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-11 S: walls proposed from the plan image. Synthetic rasters with known
// walls: a found wall lies within kTolPx of the drawn one (each end, across
// and along) and within kTolDeg of its direction.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "plan_fixtures.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/plan/wall_detect.h"

namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;
using pl::Point2;

namespace {

constexpr double kTolPx = 3.0;
constexpr double kTolDeg = 1.0;

pl::Raster white(int w, int h) { return pl::Raster(w, h, 255); }

void set_dark(pl::Raster& r, int x, int y, std::uint8_t v = 0) {
    if (x < 0 || y < 0 || x >= r.width || y >= r.height) return;
    std::uint8_t* p = r.pixel(x, y);
    p[0] = p[1] = p[2] = v;
    p[3] = 255;
}

// A line of `width` pixels between two points; pixel centres within width / 2.
void draw(pl::Raster& r, Point2 a, Point2 b, double width) {
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    const int x0 = static_cast<int>(std::min(a.x, b.x) - width), x1 = static_cast<int>(std::max(a.x, b.x) + width);
    const int y0 = static_cast<int>(std::min(a.y, b.y) - width), y1 = static_cast<int>(std::max(a.y, b.y) + width);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5 - a.x, py = y + 0.5 - a.y;
            const double t = (px * dx + py * dy) / len2;
            if (t < 0.0 || t > 1.0) continue;
            if (std::fabs(px * dy - py * dx) / std::sqrt(len2) <= width / 2.0) set_dark(r, x, y);
        }
}

double dist(Point2 a, Point2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The found line matching a -> b within the tolerance, in either direction.
template <class Seg>
bool has_line(const std::vector<Seg>& lines, Point2 a, Point2 b, double tol_units = kTolPx) {
    const double want = std::atan2(b.y - a.y, b.x - a.x);
    for (const Seg& l : lines) {
        const bool same = dist(l.a, a) <= tol_units && dist(l.b, b) <= tol_units;
        const bool flip = dist(l.a, b) <= tol_units && dist(l.b, a) <= tol_units;
        if (!same && !flip) continue;
        double turn = std::fabs(std::atan2(l.b.y - l.a.y, l.b.x - l.a.x) - want);
        turn = std::fmod(turn, std::numbers::pi);
        turn = std::min(turn, std::numbers::pi - turn);
        if (turn * 180.0 / std::numbers::pi <= kTolDeg) return true;
    }
    return false;
}

std::string listing(const std::vector<pl::DetectedLine>& v) {
    std::string s;
    for (const auto& l : v)
        s += std::to_string(l.a.x) + "," + std::to_string(l.a.y) + " -> " + std::to_string(l.b.x) + "," +
             std::to_string(l.b.y) + " w " + std::to_string(l.thickness) + "\n";
    return s;
}

}  // namespace

TEST_CASE("IND-11: axis-aligned walls are found within the tolerance") {
    pl::Raster r = white(500, 400);
    draw(r, {50, 60}, {450, 60}, 4);    // top, joins the two sides
    draw(r, {50, 60}, {50, 340}, 4);
    draw(r, {450, 60}, {450, 340}, 4);
    draw(r, {50, 340}, {450, 340}, 4);
    draw(r, {250, 60}, {250, 340}, 2);  // a thin one, crossing nothing
    const auto v = pl::detect_dark_lines(r);
    INFO(listing(v));
    CHECK(v.size() == 5);
    CHECK(has_line(v, {50, 60}, {450, 60}));
    CHECK(has_line(v, {50, 60}, {50, 340}));
    CHECK(has_line(v, {450, 60}, {450, 340}));
    CHECK(has_line(v, {50, 340}, {450, 340}));
    CHECK(has_line(v, {250, 60}, {250, 340}));
}

TEST_CASE("IND-11: walls at an angle are found within the tolerance") {
    for (const double deg : {7.0, 30.0, 45.0, 62.5, 123.0}) {
        CAPTURE(deg);
        pl::Raster r = white(600, 600);
        const double a = deg * std::numbers::pi / 180.0;
        const Point2 c{300, 300};
        const Point2 d{std::cos(a) * 220, std::sin(a) * 220};
        const Point2 n{-std::sin(a) * 120, std::cos(a) * 120};
        const Point2 a1{c.x - d.x + n.x, c.y - d.y + n.y}, b1{c.x + d.x + n.x, c.y + d.y + n.y};
        const Point2 a2{c.x - d.x - n.x, c.y - d.y - n.y}, b2{c.x + d.x - n.x, c.y + d.y - n.y};
        draw(r, a1, b1, 3);
        draw(r, a2, b2, 5);
        const auto v = pl::detect_dark_lines(r);
        INFO(listing(v));
        CHECK(v.size() == 2);
        CHECK(has_line(v, a1, b1));
        CHECK(has_line(v, a2, b2));
    }
}

TEST_CASE("IND-11: a crossing does not lose a wall; a small break is bridged, a door is not") {
    pl::Raster r = white(500, 400);
    draw(r, {50, 200}, {450, 200}, 4);
    draw(r, {250, 40}, {250, 360}, 4);
    SUBCASE("crossing") {
        const auto v = pl::detect_dark_lines(r);
        INFO(listing(v));
        CHECK(v.size() == 2);
        CHECK(has_line(v, {50, 200}, {450, 200}));
        CHECK(has_line(v, {250, 40}, {250, 360}));
    }
    SUBCASE("breaks") {
        pl::Raster b = white(500, 200);
        draw(b, {20, 50}, {200, 50}, 3);
        draw(b, {206, 50}, {400, 50}, 3);  // 6 pixels: bridged at merge_gap_px 10
        draw(b, {20, 150}, {200, 150}, 3);
        draw(b, {240, 150}, {400, 150}, 3);  // 40 pixels: two walls
        const auto v = pl::detect_dark_lines(b);
        INFO(listing(v));
        CHECK(v.size() == 3);
        CHECK(has_line(v, {20, 50}, {400, 50}));
        CHECK(has_line(v, {20, 150}, {200, 150}));
        CHECK(has_line(v, {240, 150}, {400, 150}));
    }
}

TEST_CASE("IND-11: a line running off the picture ends on its edge") {
    pl::Raster r = white(400, 300);
    draw(r, {50, 280}, {398, 310}, 4);  // slanting past the bottom
    draw(r, {-20, 40}, {420, 44}, 3);   // past both sides
    const auto v = pl::detect_dark_lines(r);
    INFO(listing(v));
    CHECK(v.size() == 2);
    for (const auto& l : v)
        for (const Point2 p : {l.a, l.b}) {
            CHECK(p.x >= 0.0);
            CHECK(p.y >= 0.0);
            CHECK(p.x <= 400.0);
            CHECK(p.y <= 300.0);
        }
}

TEST_CASE("IND-11: noise and text-like small marks give no proposals") {
    pl::Raster r = white(600, 400);
    std::mt19937 rng(11);
    std::uniform_int_distribution<int> x(0, r.width - 1), y(0, r.height - 1);
    for (int i = 0; i < 4000; ++i) set_dark(r, x(rng), y(rng));  // speckle
    // Rows of "letters": strokes of 3 x 12 and bars of 8 x 2, apart.
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 40; ++col) {
            const int px = 20 + col * 14, py = 20 + row * 45;
            for (int j = 0; j < 12; ++j)
                for (int i = 0; i < 3; ++i) set_dark(r, px + i, py + j);
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 8; ++i) set_dark(r, px + i, py + 5 + j);
        }
    const auto v = pl::detect_dark_lines(r);
    INFO(listing(v));
    CHECK(v.empty());
}

TEST_CASE("IND-11: light lines, filled areas and short lines are no walls") {
    pl::Raster r = white(500, 400);
    // A light grey line: not dark.
    for (int x = 30; x < 470; ++x)
        for (int y = 40; y < 43; ++y) set_dark(r, x, y, 180);
    // A filled block.
    for (int x = 100; x < 300; ++x)
        for (int y = 120; y < 280; ++y) set_dark(r, x, y);
    // A short line: below the (configurable) minimum, but above it when lowered.
    draw(r, {350, 350}, {395, 350}, 3);
    pl::WallDetectOptions o;
    o.max_thickness_px = 20.0;
    const auto v = pl::detect_dark_lines(r, o);
    INFO(listing(v));
    CHECK(v.empty());
    o.min_length_px = 40.0;
    const auto w = pl::detect_dark_lines(r, o);
    INFO(listing(w));
    CHECK(w.size() == 1);
    CHECK(has_line(w, {350, 350}, {395, 350}));
}

TEST_CASE("IND-11: the two faces of a drawn wall become one centre line") {
    pl::Raster r = white(500, 300);
    draw(r, {50, 100}, {450, 100}, 2);
    draw(r, {50, 116}, {450, 116}, 2);
    pl::WallDetectOptions o;
    o.parallel_px = 20.0;
    const auto v = pl::detect_dark_lines(r, o);
    INFO(listing(v));
    CHECK(v.size() == 1);
    CHECK(has_line(v, {50, 108}, {450, 108}));
    // Further apart than that: a corridor, two walls.
    o.parallel_px = 10.0;
    CHECK(pl::detect_dark_lines(r, o).size() == 2);
}

TEST_CASE("IND-11: detection is deterministic, reports progress and can be cancelled") {
    pl::Raster r = white(500, 400);
    draw(r, {50, 60}, {450, 90}, 3);
    draw(r, {100, 40}, {130, 360}, 3);
    draw(r, {50, 300}, {450, 300}, 4);
    const auto first = pl::detect_dark_lines(r);
    const auto second = pl::detect_dark_lines(r);
    REQUIRE(first.size() == 3);
    REQUIRE(second.size() == first.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].a == second[i].a);
        CHECK(first[i].b == second[i].b);
    }
    std::vector<double> seen;
    pl::WallDetectControl c;
    c.progress = [&](double f) { seen.push_back(f); };
    (void)pl::detect_dark_lines(r, {}, c);
    REQUIRE(!seen.empty());
    CHECK(std::is_sorted(seen.begin(), seen.end()));
    CHECK(seen.back() == 1.0);

    pl::WallDetectControl stop;
    stop.cancelled = [] { return true; };
    CHECK(pl::detect_dark_lines(r, {}, stop).empty());
    // Cancelled from the progress callback, half-way.
    bool later = false;
    pl::WallDetectControl mid;
    mid.cancelled = [&] { return later; };
    mid.progress = [&](double f) {
        if (f > 0.2) later = true;
    };
    CHECK(pl::detect_dark_lines(r, {}, mid).empty());
}

TEST_CASE("IND-11: a trim's turn, deskew and outline map the proposals to plan coordinates") {
    // 50 pixels per metre at 1 pixel per unit.
    constexpr double kMetresPerUnit = 0.02;
    pl::Raster r = white(900, 700);
    const Point2 a1{150, 150}, b1{750, 150}, a2{150, 150}, b2{150, 550}, a3{300, 550}, b3{750, 550};
    draw(r, a1, b1, 5);
    draw(r, a2, b2, 5);
    draw(r, a3, b3, 5);
    // Outside the trim outline: not looked at.
    draw(r, {20, 680}, {880, 680}, 5);
    const fx::Bytes bytes = fx::png(r);
    const auto source = pl::open_plan_source(bytes, "walls.png");
    REQUIRE(source);

    for (const int q : {0, 1, 2, 3})
        for (const double deskew : {0.0, 12.0, -30.0}) {
            CAPTURE(q);
            CAPTURE(deskew);
            const pl::Trim trim{pl::Trim::Shape::Polygon, {{60, 60}, {840, 60}, {840, 640}, {60, 640}}, q, deskew};
            const pl::TrimTransform tt(trim);
            const auto v = pl::propose_walls(*source, 0, trim, kMetresPerUnit);
            REQUIRE(v.size() == 3);
            CHECK(has_line(v, tt.page_to_floor(a1), tt.page_to_floor(b1), 2.0 * kTolPx));
            CHECK(has_line(v, tt.page_to_floor(a2), tt.page_to_floor(b2), 2.0 * kTolPx));
            CHECK(has_line(v, tt.page_to_floor(a3), tt.page_to_floor(b3), 2.0 * kTolPx));
            for (const auto& s : v) CHECK(s.length_m == doctest::Approx(dist(s.a, s.b) * kMetresPerUnit));
        }
    SUBCASE("the configurable minimum, in metres") {
        pl::WallProposalOptions o;
        o.min_length_m = 10.0;  // 500 units: the top wall and, with no outline, the line along the bottom
        const auto v = pl::propose_walls(*source, 0, pl::Trim{}, kMetresPerUnit, o);
        CHECK(v.size() == 2);
        for (const auto& s : v) CHECK(s.length_m >= 10.0);
    }
    SUBCASE("cancelled") {
        pl::WallDetectControl c;
        c.cancelled = [] { return true; };
        CHECK(pl::propose_walls(*source, 0, pl::Trim{}, kMetresPerUnit, {}, c).empty());
    }
}

TEST_CASE("IND-11: a big sheet is looked at within the pixel bound") {
    const pl::PageSize big{20000, 14000};
    const double s = pl::wall_detect_scale(big, 1.0, 0.01, 6'000'000);
    CHECK(s * s * big.width * big.height <= doctest::Approx(6'000'000.0));
    // At most 50 pixels per metre, at most the plan's own detail.
    CHECK(pl::wall_detect_scale({100, 100}, 10.0, 0.1, 6'000'000) == doctest::Approx(5.0));
    CHECK(pl::wall_detect_scale({100, 100}, 2.0, 0.1, 6'000'000) == doctest::Approx(2.0));
}
