// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-destructive trims (IND-05, IND-07): rectangle and polygon outlines,
// quarter turns and deskew, page <-> floor <-> pixel round trips, and
// floor rendering (exact for quarter turns, masked outside the outline,
// tiles that fit together).
//
// Copied from yantra-v2 68ea20a: tests/plan/test_plan_trim.cpp.

#include "plan_fixtures.h"
#include "yantra/core/plan/trim.h"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <random>

namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;
using pl::Point2;
using pl::Trim;

namespace {

bool near(Point2 a, Point2 b, double tol) { return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol; }

Trim poly(std::vector<Point2> pts, int q = 0, double deskew = 0.0) {
    return Trim{Trim::Shape::Polygon, std::move(pts), q, deskew};
}

}  // namespace

TEST_CASE("trim: deskew from two points") {
    CHECK(pl::deskew_from_line({0, 0}, {10, 0}) == 0.0);
    CHECK(pl::deskew_from_line({0, 0}, {0, 10}) == 0.0);    // vertical: already straight
    CHECK(pl::deskew_from_line({0, 0}, {-10, 0}) == 0.0);
    // A line going down to the right by 2 degrees (clockwise on screen)
    // is turned back by -2 degrees.
    const double a = 2.0 * std::numbers::pi / 180.0;
    CHECK(pl::deskew_from_line({5, 5}, {5 + 100 * std::cos(a), 5 + 100 * std::sin(a)}) == doctest::Approx(-2.0));
    CHECK(pl::deskew_from_line({5, 5}, {5 - 100 * std::cos(a), 5 - 100 * std::sin(a)}) == doctest::Approx(-2.0));
    // Nearly vertical, leaning 3 degrees: turned by +3 or -3 to vertical.
    const double b = (90.0 + 3.0) * std::numbers::pi / 180.0;
    CHECK(pl::deskew_from_line({0, 0}, {std::cos(b), std::sin(b)}) == doctest::Approx(-3.0));
    // 45 degrees exactly: one way, consistently.
    const double d = pl::deskew_from_line({0, 0}, {1, 1});
    CHECK(std::fabs(d) == doctest::Approx(45.0));
    CHECK_THROWS_AS(pl::deskew_from_line({1, 1}, {1, 1}), pl::PlanError);
    // The deskewed line is horizontal in floor coordinates.
    const Point2 p1{10, 20}, p2{410, 20 + 400 * std::tan(a)};
    const double ds = pl::deskew_from_line(p1, p2);
    const pl::TrimTransform t(pl::rectangle_trim({0, 0}, {500, 500}, 0, ds));
    CHECK(t.page_to_floor(p1).y == doctest::Approx(t.page_to_floor(p2).y));
}

TEST_CASE("trim: validation") {
    using Code = pl::PlanError::Code;
    auto code = [](const Trim& t) {
        try {
            pl::validate_trim(t);
        } catch (const pl::PlanError& e) {
            return e.code();
        }
        return Code::Cancelled;  // "valid"
    };
    const Code valid = Code::Cancelled;
    CHECK(code(poly({{0, 0}, {10, 0}, {10, 10}})) == valid);
    CHECK(code(Trim{}) == Code::Invalid);
    CHECK(code(poly({{0, 0}, {10, 0}})) == Code::Invalid);
    CHECK(code(poly({{0, 0}, {10, 0}, {20, 0}})) == Code::Invalid);                 // no area
    CHECK(code(poly({{0, 0}, {10, 10}, {10, 0}, {0, 10}})) == Code::Invalid);       // bow tie
    CHECK(code(poly({{0, 0}, {10, 0}, {10, NAN}})) == Code::Invalid);
    CHECK(code(poly({{0, 0}, {10, 0}, {10, 10}}, 4)) == Code::Invalid);
    CHECK(code(poly({{0, 0}, {10, 0}, {10, 10}}, 0, 46.0)) == Code::Invalid);
    CHECK(code(Trim{Trim::Shape::Rectangle, {{0, 0}, {10, 0}, {10, 10}}, 0, 0}) == Code::Invalid);
    CHECK(code(Trim{Trim::Shape::Rectangle, {{0, 0}, {10, 0}, {12, 10}, {0, 10}}, 0, 0}) == Code::Invalid);  // not a rectangle
    CHECK(code(pl::rectangle_trim({3, 4}, {100, 50}, 1, 7.5)) == valid);
    CHECK(code(pl::whole_page_trim({595, 842}, 3, -12.0)) == valid);
}

TEST_CASE("trim: rectangles, quarter turns and floor size") {
    const Trim r = pl::rectangle_trim({100, 50}, {20, 10});  // corners in any order
    CHECK(r.shape == Trim::Shape::Rectangle);
    CHECK(r.outline == std::vector<Point2>{{20, 10}, {100, 10}, {100, 50}, {20, 50}});
    for (int q = 0; q < 4; ++q) {
        CAPTURE(q);
        Trim t = r;
        t.quarter_turns = q;
        const pl::TrimTransform tr(t);
        const pl::PageSize s = tr.floor_size();
        CHECK(s.width == (q % 2 ? 40.0 : 80.0));
        CHECK(s.height == (q % 2 ? 80.0 : 40.0));
        CHECK(tr.rotation_deg() == q * 90.0);
        // Quarter turns are exact: corners land exactly on the floor corners.
        for (const Point2& c : tr.floor_outline()) {
            CHECK((c.x == 0.0 || c.x == s.width));
            CHECK((c.y == 0.0 || c.y == s.height));
        }
    }
    // One clockwise quarter turn: the page's top-left corner of the trim
    // goes to the floor's top-right.
    Trim t = r;
    t.quarter_turns = 1;
    const pl::TrimTransform tr(t);
    CHECK(tr.page_to_floor({20, 10}) == Point2{40, 0});
    CHECK(tr.page_to_floor({100, 50}) == Point2{0, 80});
    CHECK(tr.floor_to_page({40, 0}) == Point2{20, 10});
    // The whole page.
    const pl::TrimTransform whole(pl::whole_page_trim({595, 842}));
    CHECK(whole.floor_size() == pl::PageSize{595, 842});
    CHECK(whole.page_to_floor({12.5, 7.25}) == Point2{12.5, 7.25});
    // Deskewed whole page: the floor holds the turned page.
    const pl::TrimTransform dw(pl::whole_page_trim({100, 100}, 0, 10.0));
    const double c = std::cos(10.0 * std::numbers::pi / 180.0), s = std::sin(10.0 * std::numbers::pi / 180.0);
    CHECK(dw.floor_size().width == doctest::Approx(100 * (c + s)));
    // A rectangle drawn in a deskewed view is a rectangle there.
    const pl::TrimTransform dr(pl::rectangle_trim({10, 10}, {90, 60}, 1, 5.0));
    for (const Point2& p : dr.floor_outline()) {
        CHECK((std::fabs(p.x) < 1e-9 || std::fabs(p.x - dr.floor_size().width) < 1e-9));
        CHECK((std::fabs(p.y) < 1e-9 || std::fabs(p.y - dr.floor_size().height) < 1e-9));
    }
}

TEST_CASE("trim: page <-> floor <-> pixel round trips") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-500.0, 3000.0);
    std::uniform_real_distribution<double> ang(-45.0, 45.0);
    for (int n = 0; n < 200; ++n) {
        const int q = n % 4;
        const double d = (n % 3 == 0) ? 0.0 : ang(rng);
        const Trim t = poly({{100, 100}, {2000, 150}, {1800, 1400}, {300, 1200}, {150, 600}}, q, d);
        const pl::TrimTransform tr(t);
        for (int k = 0; k < 10; ++k) {
            const Point2 p{u(rng), u(rng)};
            const Point2 f = tr.page_to_floor(p);
            CHECK(near(tr.floor_to_page(f), p, 1e-9));
            CHECK(near(tr.page_to_floor(tr.floor_to_page(f)), f, 1e-9));
            if (d == 0.0) {  // exact without deskew (whole-unit points, as clicked pixels)
                const Point2 pi{std::round(p.x), std::round(p.y)};
                CHECK(tr.floor_to_page(tr.page_to_floor(pi)) == pi);
            }
            // Distances are kept (same unit length).
            const Point2 p2{p.x + 3.0, p.y - 4.0};
            const Point2 f2 = tr.page_to_floor(p2);
            CHECK(std::hypot(f2.x - f.x, f2.y - f.y) == doctest::Approx(5.0));
            for (double scale : {0.1, 1.0, 4.1667}) {
                CHECK(near(pl::from_pixels(pl::to_pixels(f, scale), scale), f, 1e-9));
            }
        }
        // Outline inside the floor rectangle.
        for (const Point2& f : tr.floor_outline()) {
            CHECK(f.x >= -1e-9);
            CHECK(f.y >= -1e-9);
            CHECK(f.x <= tr.floor_size().width + 1e-9);
            CHECK(f.y <= tr.floor_size().height + 1e-9);
        }
    }
    const pl::TrimTransform tr(poly({{0, 0}, {100, 0}, {100, 100}, {50, 50}, {0, 100}}));
    CHECK(tr.contains_page({10, 20}));
    CHECK_FALSE(tr.contains_page({50, 80}));  // in the notch
    CHECK_FALSE(tr.contains_page({150, 20}));
    CHECK(tr.page_bounds().second == Point2{100, 100});
}

TEST_CASE("trim: floor rendering") {
    const pl::Raster img = fx::pattern(120, 80);
    const auto src = pl::open_plan_source(fx::png(img));

    SUBCASE("quarter turns copy pixels exactly") {
        const Trim r = pl::rectangle_trim({10, 20}, {70, 60});  // 60 x 40
        for (int q = 0; q < 4; ++q) {
            CAPTURE(q);
            Trim t = r;
            t.quarter_turns = q;
            const pl::TrimTransform tr(t);
            const pl::PixelSize px = pl::floor_pixels(tr, 1.0);
            const pl::Raster f = pl::render_floor(*src, 0, tr, 1.0, {0, 0, int(px.width), int(px.height)});
            CHECK(f.width == (q % 2 ? 40 : 60));
            int mismatches = 0;
            for (int y = 0; y < f.height; ++y)
                for (int x = 0; x < f.width; ++x) {
                    const Point2 p = tr.floor_to_page({x + 0.5, y + 0.5});
                    const std::uint8_t* want = img.pixel(int(std::floor(p.x)), int(std::floor(p.y)));
                    const std::uint8_t* got = f.pixel(x, y);
                    if (!std::equal(want, want + 4, got)) ++mismatches;
                }
            CHECK(mismatches == 0);
        }
    }
    SUBCASE("outside the polygon is transparent") {
        const pl::TrimTransform tr(poly({{0, 0}, {100, 0}, {100, 60}, {50, 30}, {0, 60}}));
        const pl::Raster f = pl::render_floor(*src, 0, tr, 1.0, {0, 0, 100, 60});
        CHECK(f.pixel(10, 5)[3] == 255);
        CHECK(f.pixel(50, 55)[3] == 0);  // in the notch
        CHECK(f.pixel(95, 50)[3] == 255);
    }
    SUBCASE("deskew turns a skewed line straight") {
        // A dark line at 4 degrees on a white sheet.
        pl::Raster sheet(400, 200);
        std::fill(sheet.rgba.begin(), sheet.rgba.end(), 255);
        const double a = 4.0 * std::numbers::pi / 180.0;
        for (int x = 20; x < 380; ++x) {
            const int y = int(std::lround(100 + (x - 200) * std::tan(a)));
            for (int k = -1; k <= 1; ++k) {
                std::uint8_t* p = sheet.pixel(x, y + k);
                p[0] = p[1] = p[2] = 0;
            }
        }
        const auto s2 = pl::open_plan_source(fx::png(sheet));
        const double ds = pl::deskew_from_line({20, 100 - 180 * std::tan(a)}, {380, 100 + 180 * std::tan(a)});
        CHECK(ds == doctest::Approx(-4.0));
        const pl::TrimTransform tr(pl::whole_page_trim({400, 200}, 0, ds));
        const pl::PixelSize px = pl::floor_pixels(tr, 1.0);
        const pl::Raster f = pl::render_floor(*s2, 0, tr, 1.0, {0, 0, int(px.width), int(px.height)});
        // The darkest row is the same along the line.
        auto darkest = [&](int x) {
            int best = -1, val = 1000;
            for (int y = 0; y < f.height; ++y)
                if (f.pixel(x, y)[3] == 255 && f.pixel(x, y)[0] < val) {
                    val = f.pixel(x, y)[0];
                    best = y;
                }
            return best;
        };
        const Point2 l = tr.page_to_floor({60, 100 - 140 * std::tan(a)});
        const Point2 r = tr.page_to_floor({340, 100 + 140 * std::tan(a)});
        CHECK(std::abs(darkest(int(l.x)) - darkest(int(r.x))) <= 1);
        // Corners outside the turned page are transparent.
        CHECK(f.pixel(0, 0)[3] == 0);
    }
    SUBCASE("floor tiles fit together") {
        const pl::TrimTransform tr(poly({{5, 5}, {115, 10}, {100, 75}, {10, 70}}, 3, 6.0));
        for (double scale : {0.5, 1.0, 2.0}) {
            const pl::PixelSize px = pl::floor_pixels(tr, scale);
            const int W = int(px.width), H = int(px.height);
            const pl::Raster whole = pl::render_floor(*src, 0, tr, scale, {0, 0, W, H});
            pl::Raster built(W, H, 0);
            for (int y0 = 0; y0 < H; y0 += 32)
                for (int x0 = 0; x0 < W; x0 += 32) {
                    const int w = std::min(32, W - x0), h = std::min(32, H - y0);
                    const pl::Raster t = pl::render_floor(*src, 0, tr, scale, {x0, y0, w, h});
                    for (int y = 0; y < h; ++y) std::copy(t.pixel(0, y), t.pixel(0, y) + 4 * w, built.pixel(x0, y0 + y));
                }
            CHECK(built == whole);
        }
    }
}
