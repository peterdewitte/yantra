// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/plan/trim.cpp.

#include "yantra/core/plan/trim.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace yantra::plan {

using Code = PlanError::Code;

namespace {

constexpr std::size_t kMaxOutlinePoints = 4096;

double deg2rad(double d) { return d * std::numbers::pi / 180.0; }

// Exact quarter turns, clockwise on screen (y down).
Point2 quarter(Point2 p, int q) {
    switch (q & 3) {
    case 1: return {-p.y, p.x};
    case 2: return {-p.x, -p.y};
    case 3: return {p.y, -p.x};
    default: return p;
    }
}

double cross(Point2 o, Point2 a, Point2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

bool segments_cross(Point2 a, Point2 b, Point2 c, Point2 d) {
    // Proper intersection or touching (collinear overlap counts too).
    const double d1 = cross(c, d, a);
    const double d2 = cross(c, d, b);
    const double d3 = cross(a, b, c);
    const double d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
    auto on = [](Point2 p, Point2 q, Point2 r, double c0) {
        return c0 == 0.0 && std::min(p.x, q.x) <= r.x && r.x <= std::max(p.x, q.x) && std::min(p.y, q.y) <= r.y &&
               r.y <= std::max(p.y, q.y);
    };
    return on(c, d, a, d1) || on(c, d, b, d2) || on(a, b, c, d3) || on(a, b, d, d4);
}

double area(const std::vector<Point2>& pts) {
    double a = 0.0;
    for (std::size_t i = 0, n = pts.size(); i < n; ++i) {
        const Point2& p = pts[i];
        const Point2& q = pts[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * a;
}

}  // namespace

void validate_trim(const Trim& t) {
    if (t.shape == Trim::Shape::None) throw PlanError(Code::Invalid, "no trim");
    if (t.quarter_turns < 0 || t.quarter_turns > 3) throw PlanError(Code::Invalid, "quarter turns must be 0..3");
    if (!std::isfinite(t.deskew_deg) || std::fabs(t.deskew_deg) > kMaxDeskewDeg)
        throw PlanError(Code::Invalid, "deskew must be between -45 and 45 degrees");
    const std::size_t n = t.outline.size();
    if (t.shape == Trim::Shape::Rectangle && n != 4) throw PlanError(Code::Invalid, "a rectangle trim has 4 corners");
    if (n < 3) throw PlanError(Code::Invalid, "a trim outline needs at least 3 points");
    if (n > kMaxOutlinePoints)
        throw PlanError(Code::Invalid, "a trim outline has at most " + std::to_string(kMaxOutlinePoints) + " points");
    double extent = 0.0;
    for (const Point2& p : t.outline) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::fabs(p.x) > 1e9 || std::fabs(p.y) > 1e9)
            throw PlanError(Code::Invalid, "a trim point is not a valid number");
        extent = std::max({extent, std::fabs(p.x), std::fabs(p.y)});
    }
    if (!(std::fabs(area(t.outline)) > 1e-9 * std::max(1.0, extent * extent)))
        throw PlanError(Code::Invalid, "the trim outline has no area");
    // Simple polygon: no two non-adjacent edges meet.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            if (segments_cross(t.outline[i], t.outline[(i + 1) % n], t.outline[j], t.outline[(j + 1) % n]))
                throw PlanError(Code::Invalid, "the trim outline crosses itself");
        }
    }
    if (t.shape == Trim::Shape::Rectangle) {
        // Its sides run along the turned frame.
        TrimTransform probe(Trim{Trim::Shape::Polygon, t.outline, t.quarter_turns, t.deskew_deg});
        const PageSize s = probe.floor_size();
        const double tol = 1e-6 * std::max({1.0, s.width, s.height});
        for (const Point2& f : probe.floor_outline()) {
            const bool on_x = std::fabs(f.x) <= tol || std::fabs(f.x - s.width) <= tol;
            const bool on_y = std::fabs(f.y) <= tol || std::fabs(f.y - s.height) <= tol;
            if (!on_x || !on_y) throw PlanError(Code::Invalid, "the rectangle trim is not a rectangle in its rotation");
        }
    }
}

Trim rectangle_trim(Point2 a, Point2 b, int quarter_turns, double deskew_deg) {
    // Corners in the turned frame, then back to the page.
    const double c = std::cos(deg2rad(deskew_deg));
    const double s = std::sin(deg2rad(deskew_deg));
    auto turn = [&](Point2 p) {
        const Point2 d = deskew_deg == 0.0 ? p : Point2{p.x * c - p.y * s, p.x * s + p.y * c};
        return quarter(d, quarter_turns);
    };
    auto unturn = [&](Point2 p) {
        const Point2 d = quarter(p, 4 - (quarter_turns & 3));
        return deskew_deg == 0.0 ? d : Point2{d.x * c + d.y * s, -d.x * s + d.y * c};
    };
    const Point2 ta = turn(a);
    const Point2 tb = turn(b);
    const Point2 lo{std::min(ta.x, tb.x), std::min(ta.y, tb.y)};
    const Point2 hi{std::max(ta.x, tb.x), std::max(ta.y, tb.y)};
    return Trim{Trim::Shape::Rectangle,
                {unturn(lo), unturn({hi.x, lo.y}), unturn(hi), unturn({lo.x, hi.y})},
                quarter_turns,
                deskew_deg};
}

Trim whole_page_trim(const PageSize& page, int quarter_turns, double deskew_deg) {
    if (deskew_deg == 0.0) {
        // The page corners themselves (exact).
        return Trim{Trim::Shape::Rectangle,
                    {{0.0, 0.0}, {page.width, 0.0}, {page.width, page.height}, {0.0, page.height}},
                    quarter_turns,
                    0.0};
    }
    // Deskewed: the rectangle of the turned frame around the page.
    const double c = std::cos(deg2rad(deskew_deg));
    const double s = std::sin(deg2rad(deskew_deg));
    Point2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    for (const Point2 p : {Point2{0, 0}, Point2{page.width, 0}, Point2{page.width, page.height}, Point2{0, page.height}}) {
        const Point2 d{p.x * c - p.y * s, p.x * s + p.y * c};
        lo = {std::min(lo.x, d.x), std::min(lo.y, d.y)};
        hi = {std::max(hi.x, d.x), std::max(hi.y, d.y)};
    }
    auto back = [&](Point2 d) { return Point2{d.x * c + d.y * s, -d.x * s + d.y * c}; };
    return Trim{Trim::Shape::Rectangle, {back(lo), back({hi.x, lo.y}), back(hi), back({lo.x, hi.y})}, quarter_turns,
                deskew_deg};
}

double deskew_from_line(Point2 a, Point2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0 && dy == 0.0))
        throw PlanError(Code::Invalid, "deskew needs two different points");
    // Angle of the line, clockwise on screen; turning by its negative
    // makes it horizontal. Modulo 90: horizontal or vertical, the nearer.
    const double phi = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    double d = std::fmod(-phi, 90.0);
    if (d > 45.0) d -= 90.0;
    if (d <= -45.0) d += 90.0;
    if (d == 0.0) d = 0.0;  // no negative zero
    return d;
}

TrimTransform::TrimTransform(const Trim& trim) : trim_(trim) {
    // A rectangle's own check needs this transform: check the outline
    // as a polygon first, the rectangle at the end.
    Trim poly = trim;
    if (poly.shape == Trim::Shape::Rectangle) poly.shape = Trim::Shape::Polygon;
    validate_trim(poly);
    if (trim_.deskew_deg != 0.0) {
        cos_ = std::cos(deg2rad(trim_.deskew_deg));
        sin_ = std::sin(deg2rad(trim_.deskew_deg));
    }
    Point2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    for (const Point2& p : trim_.outline) {
        const Point2 t = turn(p);
        lo = {std::min(lo.x, t.x), std::min(lo.y, t.y)};
        hi = {std::max(hi.x, t.x), std::max(hi.y, t.y)};
    }
    origin_ = lo;
    size_ = {hi.x - lo.x, hi.y - lo.y};
    if (trim.shape == Trim::Shape::Rectangle) validate_trim(trim);
}

Point2 TrimTransform::turn(Point2 p) const noexcept {
    const Point2 d = trim_.deskew_deg == 0.0 ? p : Point2{p.x * cos_ - p.y * sin_, p.x * sin_ + p.y * cos_};
    return quarter(d, trim_.quarter_turns);
}

Point2 TrimTransform::unturn(Point2 p) const noexcept {
    const Point2 d = quarter(p, 4 - (trim_.quarter_turns & 3));
    return trim_.deskew_deg == 0.0 ? d : Point2{d.x * cos_ + d.y * sin_, -d.x * sin_ + d.y * cos_};
}

Point2 TrimTransform::page_to_floor(Point2 page) const noexcept {
    const Point2 t = turn(page);
    return {t.x - origin_.x, t.y - origin_.y};
}

Point2 TrimTransform::floor_to_page(Point2 floor) const noexcept {
    return unturn({floor.x + origin_.x, floor.y + origin_.y});
}

std::vector<Point2> TrimTransform::floor_outline() const {
    std::vector<Point2> v;
    v.reserve(trim_.outline.size());
    for (const Point2& p : trim_.outline) v.push_back(page_to_floor(p));
    return v;
}

bool TrimTransform::contains_page(Point2 p) const noexcept {
    bool inside = false;
    const auto& o = trim_.outline;
    for (std::size_t i = 0, j = o.size() - 1; i < o.size(); j = i++) {
        if ((o[i].y > p.y) != (o[j].y > p.y) &&
            p.x < (o[j].x - o[i].x) * (p.y - o[i].y) / (o[j].y - o[i].y) + o[i].x)
            inside = !inside;
    }
    return inside;
}

std::pair<Point2, Point2> TrimTransform::page_bounds() const noexcept {
    Point2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    for (const Point2& p : trim_.outline) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    return {lo, hi};
}

PixelSize floor_pixels(const TrimTransform& t, double scale) { return page_pixels(t.floor_size(), scale); }

Raster render_floor(const PlanSource& source, int page, const TrimTransform& t, double scale,
                    const PixelRect& window) {
    if (window.width <= 0 || window.height <= 0) throw PlanError(Code::Invalid, "empty render window");
    const PixelSize page_px = page_pixels(source.page_size(page), scale);
    Raster out(window.width, window.height, 0);
    // The page pixels under the window (its corners mapped to the page).
    double lx = 1e300, ly = 1e300, hx = -1e300, hy = -1e300;
    for (int cy = 0; cy < 2; ++cy) {
        for (int cx = 0; cx < 2; ++cx) {
            const Point2 f{static_cast<double>(window.x + cx * window.width) / scale,
                           static_cast<double>(window.y + cy * window.height) / scale};
            const Point2 p = t.floor_to_page(f);
            lx = std::min(lx, p.x * scale);
            ly = std::min(ly, p.y * scale);
            hx = std::max(hx, p.x * scale);
            hy = std::max(hy, p.y * scale);
        }
    }
    const std::int64_t px0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(lx)) - 1);
    const std::int64_t py0 = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(ly)) - 1);
    const std::int64_t px1 = std::min<std::int64_t>(page_px.width, static_cast<std::int64_t>(std::ceil(hx)) + 1);
    const std::int64_t py1 = std::min<std::int64_t>(page_px.height, static_cast<std::int64_t>(std::ceil(hy)) + 1);
    if (px1 <= px0 || py1 <= py0) return out;
    const PixelRect src_win{px0, py0, static_cast<int>(px1 - px0), static_cast<int>(py1 - py0)};
    const Raster src = source.render(page, scale, src_win);
    const bool exact = t.trim().deskew_deg == 0.0;
    const bool rect = t.trim().shape == Trim::Shape::Rectangle;
    const PageSize fs = t.floor_size();
    auto fetch = [&](std::int64_t ix, std::int64_t iy) -> const std::uint8_t* {
        static constexpr std::uint8_t kNone[4] = {0, 0, 0, 0};
        if (ix < 0 || iy < 0 || ix >= src.width || iy >= src.height) return kNone;
        return src.pixel(static_cast<int>(ix), static_cast<int>(iy));
    };
    for (int j = 0; j < window.height; ++j) {
        for (int i = 0; i < window.width; ++i) {
            const Point2 f{(static_cast<double>(window.x + i) + 0.5) / scale,
                           (static_cast<double>(window.y + j) + 0.5) / scale};
            const bool inside = rect ? (f.x >= 0.0 && f.y >= 0.0 && f.x <= fs.width && f.y <= fs.height)
                                     : t.contains_page(t.floor_to_page(f));
            if (!inside) continue;
            const Point2 p = t.floor_to_page(f);
            // Continuous index in the source window (integer = pixel centre).
            const double sx = p.x * scale - 0.5 - static_cast<double>(px0);
            const double sy = p.y * scale - 0.5 - static_cast<double>(py0);
            std::uint8_t* o = out.pixel(i, j);
            if (exact) {
                const std::uint8_t* s = fetch(static_cast<std::int64_t>(std::floor(sx + 0.5)),
                                              static_cast<std::int64_t>(std::floor(sy + 0.5)));
                std::copy(s, s + 4, o);
                continue;
            }
            const double fx = std::floor(sx);
            const double fy = std::floor(sy);
            const double ax = sx - fx;
            const double ay = sy - fy;
            const auto ix = static_cast<std::int64_t>(fx);
            const auto iy = static_cast<std::int64_t>(fy);
            const std::uint8_t* p00 = fetch(ix, iy);
            const std::uint8_t* p10 = fetch(ix + 1, iy);
            const std::uint8_t* p01 = fetch(ix, iy + 1);
            const std::uint8_t* p11 = fetch(ix + 1, iy + 1);
            // Premultiplied blend, so transparent neighbours add no colour.
            const double w00 = (1 - ax) * (1 - ay) * p00[3], w10 = ax * (1 - ay) * p10[3];
            const double w01 = (1 - ax) * ay * p01[3], w11 = ax * ay * p11[3];
            const double wa = w00 + w10 + w01 + w11;
            if (wa <= 0.0) continue;
            for (int c = 0; c < 3; ++c) {
                const double v = (w00 * p00[c] + w10 * p10[c] + w01 * p01[c] + w11 * p11[c]) / wa;
                o[c] = static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
            o[3] = static_cast<std::uint8_t>(std::clamp(std::lround(wa), 0L, 255L));
        }
    }
    return out;
}

Point2 trim_floor_to_page(const Trim& t, Point2 floor) {
    if (t.shape == Trim::Shape::None) return floor;
    return TrimTransform(t).floor_to_page(floor);
}

Point2 trim_page_to_floor(const Trim& t, Point2 page) {
    if (t.shape == Trim::Shape::None) return page;
    return TrimTransform(t).page_to_floor(page);
}

void reanchor_floor(building::Floor& f, const Trim& from, const Trim& to) {
    if (from == to) return;
    const std::optional<TrimTransform> a =
        from.shape == Trim::Shape::None ? std::nullopt : std::optional<TrimTransform>(TrimTransform(from));
    const std::optional<TrimTransform> b =
        to.shape == Trim::Shape::None ? std::nullopt : std::optional<TrimTransform>(TrimTransform(to));
    auto map = [&](Point2 p) {
        const Point2 page = a ? a->floor_to_page(p) : p;
        return b ? b->page_to_floor(page) : page;
    };
    const double turn = (b ? b->rotation_deg() : 0.0) - (a ? a->rotation_deg() : 0.0);
    auto turned = [&](double deg) {
        double d = std::fmod(deg + turn, 360.0);
        if (d < 0.0) d += 360.0;
        return d >= 360.0 ? 0.0 : d;
    };
    for (building::Wall& w : f.walls)
        for (Point2& p : w.points) p = map(p);
    for (building::Antenna& ant : f.antennas) {
        ant.position = map(ant.position);
        if (ant.pattern.type != building::AntennaPattern::Type::Omni) ant.pattern.azimuth_deg = turned(ant.pattern.azimuth_deg);
        if (ant.direction_deg) ant.direction_deg = turned(*ant.direction_deg);
    }
    if (f.scale.method == building::Scale::Method::TwoPoints || f.scale.method == building::Scale::Method::ScaleBar) {
        f.scale.a = map(f.scale.a);
        f.scale.b = map(f.scale.b);
    }
    if (f.reference_point) f.reference_point = map(*f.reference_point);
}

}  // namespace yantra::plan
