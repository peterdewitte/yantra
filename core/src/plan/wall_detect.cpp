// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dark-line detection on a floor raster (IND-11 S; contract in wall_detect.h).

#include "yantra/core/plan/wall_detect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>

#include "yantra/core/plan/import.h"

namespace yantra::plan {

namespace {

constexpr int kThetaBins = 360;  // 0.5 degrees
constexpr double kThetaStep = std::numbers::pi / kThetaBins;
constexpr double kBand = 1.5;  // pixels off the Hough line still count as on it

struct Mask {
    int w = 0, h = 0;
    std::vector<std::uint8_t> v;
    bool at(int x, int y) const noexcept {
        return x >= 0 && y >= 0 && x < w && y < h && v[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] != 0;
    }
    std::uint8_t& ref(int x, int y) noexcept { return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]; }
};

Mask dark_pixels(const Raster& r, int level) {
    Mask m{r.width, r.height, std::vector<std::uint8_t>(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height), 0)};
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) {
            const std::uint8_t* p = r.pixel(x, y);
            if (p[3] < 128) continue;
            // Integer Rec. 601 luminance.
            const int lum = (299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000;
            if (lum < level) m.ref(x, y) = 1;
        }
    return m;
}

// Removes the 8-connected groups whose bounding box diagonal is below `min_px`.
void remove_small_groups(Mask& m, double min_px, const WallDetectControl& control) {
    std::vector<std::uint8_t> seen(m.v.size(), 0);
    std::vector<std::pair<int, int>> stack, group;
    for (int y0 = 0; y0 < m.h; ++y0) {
        if (control.cancelled && control.cancelled()) return;
        for (int x0 = 0; x0 < m.w; ++x0) {
            const std::size_t i0 = static_cast<std::size_t>(y0) * static_cast<std::size_t>(m.w) + static_cast<std::size_t>(x0);
            if (m.v[i0] == 0 || seen[i0] != 0) continue;
            int x1 = x0, x2 = x0, y1 = y0, y2 = y0;
            stack.assign(1, {x0, y0});
            group.clear();
            seen[i0] = 1;
            while (!stack.empty()) {
                const auto [x, y] = stack.back();
                stack.pop_back();
                group.emplace_back(x, y);
                x1 = std::min(x1, x);
                x2 = std::max(x2, x);
                y1 = std::min(y1, y);
                y2 = std::max(y2, y);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (!m.at(nx, ny)) continue;
                        const std::size_t i = static_cast<std::size_t>(ny) * static_cast<std::size_t>(m.w) + static_cast<std::size_t>(nx);
                        if (seen[i] != 0) continue;
                        seen[i] = 1;
                        stack.emplace_back(nx, ny);
                    }
            }
            if (std::hypot(x2 - x1 + 1, y2 - y1 + 1) < min_px)
                for (const auto& [x, y] : group) m.ref(x, y) = 0;
        }
    }
}

// The Hough accumulator over the pixels still voting.
class Hough {
public:
    explicit Hough(const Mask& m) : diag_(static_cast<int>(std::ceil(std::hypot(m.w, m.h)))), rhos_(2 * diag_ + 1) {
        votes_.assign(static_cast<std::size_t>(kThetaBins) * static_cast<std::size_t>(rhos_), 0);
        for (int t = 0; t < kThetaBins; ++t) {
            cos_[static_cast<std::size_t>(t)] = std::cos(t * kThetaStep);
            sin_[static_cast<std::size_t>(t)] = std::sin(t * kThetaStep);
        }
    }
    void vote(int x, int y, int amount) noexcept {
        const double px = x + 0.5, py = y + 0.5;
        for (int t = 0; t < kThetaBins; ++t) {
            const int r = static_cast<int>(std::lround(px * cos_[static_cast<std::size_t>(t)] + py * sin_[static_cast<std::size_t>(t)])) + diag_;
            votes_[index(t, r)] += amount;
        }
    }
    std::size_t index(int t, int r) const noexcept { return static_cast<std::size_t>(t) * static_cast<std::size_t>(rhos_) + static_cast<std::size_t>(r); }
    std::size_t cells() const noexcept { return votes_.size(); }
    std::int32_t& at(std::size_t i) noexcept { return votes_[i]; }
    int theta_of(std::size_t i) const noexcept { return static_cast<int>(i / static_cast<std::size_t>(rhos_)); }
    int rho_of(std::size_t i) const noexcept { return static_cast<int>(i % static_cast<std::size_t>(rhos_)) - diag_; }
    double cos_of(int t) const noexcept { return cos_[static_cast<std::size_t>(t)]; }
    double sin_of(int t) const noexcept { return sin_[static_cast<std::size_t>(t)]; }

private:
    int diag_, rhos_;
    std::vector<std::int32_t> votes_;
    double cos_[kThetaBins] = {};
    double sin_[kThetaBins] = {};
};

struct Span {
    double lo = 0.0, hi = 0.0;  // along the normal, relative to the line
};

// The dark width at parameter `t` of the line, scanning along the normal from
// the line itself (a pixel within kBand starts it). False: nothing dark there.
bool width_at(const Mask& m, Point2 o, Point2 d, Point2 n, double t, double limit, Span& out) {
    const double cx = o.x + d.x * t, cy = o.y + d.y * t;
    auto dark = [&](double s) {
        return m.at(static_cast<int>(std::floor(cx + n.x * s)), static_cast<int>(std::floor(cy + n.y * s)));
    };
    double start = 0.0;
    bool found = false;
    for (const double s : {0.0, 0.5, -0.5, 1.0, -1.0, kBand, -kBand})
        if (dark(s)) {
            start = s;
            found = true;
            break;
        }
    if (!found) return false;
    double lo = start, hi = start;
    while (hi - start < limit && dark(hi + 0.5)) hi += 0.5;
    while (start - lo < limit && dark(lo - 0.5)) lo -= 0.5;
    out = {lo, hi};
    return true;
}

double median(std::vector<double> v) {
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

double angle_of(Point2 a, Point2 b) { return std::atan2(b.y - a.y, b.x - a.x); }

double turned_by(const DetectedLine& p, const DetectedLine& q) {
    double d = std::fabs(angle_of(p.a, p.b) - angle_of(q.a, q.b));
    d = std::fmod(d, std::numbers::pi);
    return std::min(d, std::numbers::pi - d);
}

// Position along and offset from the line of `l` (unit direction d, normal n).
struct Frame {
    Point2 o, d, n;
    explicit Frame(const DetectedLine& l) : o(l.a) {
        const double len = std::max(l.length(), 1e-9);
        d = {(l.b.x - l.a.x) / len, (l.b.y - l.a.y) / len};
        n = {-d.y, d.x};
    }
    double along(Point2 p) const noexcept { return (p.x - o.x) * d.x + (p.y - o.y) * d.y; }
    double off(Point2 p) const noexcept { return (p.x - o.x) * n.x + (p.y - o.y) * n.y; }
    Point2 at(double t, double s = 0.0) const noexcept { return {o.x + d.x * t + n.x * s, o.y + d.y * t + n.y * s}; }
};

// Merges collinear lines: the shorter one onto the longer one's line.
bool merge_collinear_once(std::vector<DetectedLine>& v, const WallDetectOptions& o) {
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = i + 1; j < v.size(); ++j) {
            if (turned_by(v[i], v[j]) * 180.0 / std::numbers::pi > o.merge_angle_deg) continue;
            const Frame f(v[i]);
            const double offset = o.merge_offset_px + (v[i].thickness + v[j].thickness) / 4.0;
            if (std::fabs(f.off(v[j].a)) > offset || std::fabs(f.off(v[j].b)) > offset) continue;
            const double a0 = std::min(f.along(v[i].a), f.along(v[i].b)), a1 = std::max(f.along(v[i].a), f.along(v[i].b));
            const double b0 = std::min(f.along(v[j].a), f.along(v[j].b)), b1 = std::max(f.along(v[j].a), f.along(v[j].b));
            if (std::max(a0, b0) - std::min(a1, b1) > o.merge_gap_px) continue;
            const double thick = std::max(v[i].thickness, v[j].thickness);
            v[i] = DetectedLine{f.at(std::min(a0, b0)), f.at(std::max(a1, b1)), thick};
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(j));
            return true;
        }
    return false;
}

// Two faces of one wall (parallel, close, overlapping along at least half of
// the shorter one) become their centre line.
bool merge_parallel_once(std::vector<DetectedLine>& v, const WallDetectOptions& o) {
    for (std::size_t i = 0; i < v.size(); ++i)
        for (std::size_t j = i + 1; j < v.size(); ++j) {
            if (turned_by(v[i], v[j]) * 180.0 / std::numbers::pi > o.merge_angle_deg) continue;
            const Frame f(v[i]);
            const double s = (f.off(v[j].a) + f.off(v[j].b)) / 2.0;
            if (std::fabs(s) > o.parallel_px) continue;
            const double a0 = std::min(f.along(v[i].a), f.along(v[i].b)), a1 = std::max(f.along(v[i].a), f.along(v[i].b));
            const double b0 = std::min(f.along(v[j].a), f.along(v[j].b)), b1 = std::max(f.along(v[j].a), f.along(v[j].b));
            const double overlap = std::min(a1, b1) - std::max(a0, b0);
            if (overlap < 0.5 * std::min(a1 - a0, b1 - b0)) continue;
            const double thick = std::fabs(s) + (v[i].thickness + v[j].thickness) / 2.0;
            v[i] = DetectedLine{f.at(std::min(a0, b0), s / 2.0), f.at(std::max(a1, b1), s / 2.0), thick};
            v.erase(v.begin() + static_cast<std::ptrdiff_t>(j));
            return true;
        }
    return false;
}

// Cuts a -> b to the rectangle 0..w x 0..h (Liang-Barsky); false: all outside.
bool clip_to(Point2& a, Point2& b, double w, double h) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    double t0 = 0.0, t1 = 1.0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x, w - a.x, a.y, h - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double t = q[i] / p[i];
        if (p[i] < 0.0) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
    }
    if (t0 > t1) return false;
    const Point2 from = a;
    a = {from.x + dx * t0, from.y + dy * t0};
    b = {from.x + dx * t1, from.y + dy * t1};
    return true;
}

}  // namespace

double DetectedLine::length() const noexcept { return std::hypot(b.x - a.x, b.y - a.y); }

std::vector<DetectedLine> detect_dark_lines(const Raster& raster, const WallDetectOptions& o, const WallDetectControl& control) {
    std::vector<DetectedLine> found;
    if (raster.width <= 0 || raster.height <= 0) return found;
    auto cancelled = [&] { return control.cancelled && control.cancelled(); };
    auto progress = [&](double f) {
        if (control.progress) control.progress(f);
    };
    Mask ink = dark_pixels(raster, o.dark_level);
    progress(0.05);
    remove_small_groups(ink, o.min_component_px > 0.0 ? o.min_component_px : o.min_length_px * 0.5, control);
    if (cancelled()) return {};
    progress(0.1);

    // Every dark pixel votes; `voting` marks those that still do.
    Hough hough(ink);
    Mask voting = ink;
    std::size_t total = 0;
    for (int y = 0; y < ink.h; ++y) {
        if (cancelled()) return {};
        for (int x = 0; x < ink.w; ++x)
            if (ink.at(x, y)) {
                hough.vote(x, y, 1);
                ++total;
            }
        progress(0.1 + 0.3 * (y + 1) / ink.h);
    }

    // Strongest line first (equal votes: lowest cell), votes re-read lazily.
    const auto threshold = static_cast<std::int32_t>(std::max(4.0, 0.5 * o.min_length_px));
    using Cell = std::pair<std::int32_t, std::size_t>;
    auto weaker = [](const Cell& a, const Cell& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; };
    std::priority_queue<Cell, std::vector<Cell>, decltype(weaker)> queue(weaker);
    for (std::size_t i = 0; i < hough.cells(); ++i)
        if (hough.at(i) >= threshold) queue.emplace(hough.at(i), i);

    std::size_t used = 0;
    const double limit = o.max_thickness_px * 1.5 + 2.0;
    constexpr std::int32_t kDead = std::numeric_limits<std::int32_t>::min() / 2;
    while (!queue.empty()) {
        if (cancelled()) return {};
        const auto [recorded, cell] = queue.top();
        queue.pop();
        const std::int32_t now = hough.at(cell);
        if (now < threshold) continue;
        if (now != recorded) {
            queue.emplace(now, cell);
            continue;
        }
        const int th = hough.theta_of(cell);
        const Point2 n{hough.cos_of(th), hough.sin_of(th)};
        const Point2 d{-n.y, n.x};
        const double rho = hough.rho_of(cell);
        const Point2 o0{n.x * rho, n.y * rho};

        // The dark runs along the line, bridging small breaks: the longest one
        // whose pixels a stronger line has not used yet.
        const double far = std::hypot(ink.w, ink.h);
        std::vector<Span> runs;
        bool in_run = false;
        double last = 0.0;
        Span dummy;
        for (double t = -far; t <= far; t += 1.0) {
            const double cx = o0.x + d.x * t, cy = o0.y + d.y * t;
            if (cx < -kBand || cy < -kBand || cx > ink.w + kBand || cy > ink.h + kBand) {
                in_run = false;
                continue;
            }
            if (!width_at(ink, o0, d, n, t, 0.0, dummy)) continue;
            if (!in_run || t - last > o.max_gap_px + 1.0) runs.push_back({t, t});
            runs.back().hi = t;
            in_run = true;
            last = t;
        }
        std::stable_sort(runs.begin(), runs.end(), [](const Span& p, const Span& q) { return p.hi - p.lo > q.hi - q.lo; });
        double best0 = 0.0, best1 = -1.0;
        for (const Span& r : runs) {
            if (r.hi - r.lo < o.min_length_px * 0.25) break;
            std::size_t unused = 0, tested = 0;
            for (double t = r.lo; t <= r.hi; t += 1.0) {
                ++tested;
                if (width_at(voting, o0, d, n, t, 0.0, dummy)) ++unused;
            }
            if (unused * 2 >= tested) {
                best0 = r.lo;
                best1 = r.hi;
                break;
            }
        }
        if (best1 < best0) {
            hough.at(cell) = kDead;
            continue;
        }

        // Its centre and width from samples along it.
        std::vector<double> centres_t, centres_s, widths;
        for (double t = best0; t <= best1; t += 1.0) {
            Span w;
            if (!width_at(ink, o0, d, n, t, limit, w)) continue;
            centres_t.push_back(t);
            centres_s.push_back((w.lo + w.hi) / 2.0);
            widths.push_back(w.hi - w.lo + 1.0);
        }
        if (widths.empty()) {
            hough.at(cell) = kDead;
            continue;
        }
        const double width = median(widths);
        // A least-squares line through the centres of the samples of about
        // the usual width (a crossing wall's samples are wider).
        double st = 0, ss = 0, stt = 0, sts = 0, count = 0;
        for (std::size_t i = 0; i < widths.size(); ++i) {
            if (widths[i] > width + 2.0) continue;
            st += centres_t[i];
            ss += centres_s[i];
            stt += centres_t[i] * centres_t[i];
            sts += centres_t[i] * centres_s[i];
            count += 1.0;
        }
        double slope = 0.0, base = 0.0;
        if (count >= 2.0 && count * stt - st * st > 1e-9) {
            slope = (count * sts - st * ss) / (count * stt - st * st);
            base = (ss - slope * st) / count;
        } else if (count >= 1.0) {
            base = ss / count;
        }
        const auto on_centre = [&](double t) {
            const double s = base + slope * t;
            return Point2{o0.x + d.x * t + n.x * s, o0.y + d.y * t + n.y * s};
        };

        // These pixels stop voting: this line's own band, not the wall it crosses.
        const double half = width / 2.0 + 1.0;
        const std::size_t used_before = used;
        for (double t = best0 - 1.0; t <= best1 + 1.0; t += 0.5)
            for (double s = -half; s <= half; s += 0.5) {
                const Point2 p = on_centre(t);
                const int x = static_cast<int>(std::floor(p.x + n.x * s)), y = static_cast<int>(std::floor(p.y + n.y * s));
                if (!voting.at(x, y)) continue;
                voting.ref(x, y) = 0;
                hough.vote(x, y, -1);
                ++used;
            }
        // The rest of this line (beyond a door) can still be found: it votes on.
        if (used == used_before) hough.at(cell) = kDead;
        else if (hough.at(cell) >= threshold) queue.emplace(hough.at(cell), cell);
        if (total > 0) progress(0.4 + 0.5 * std::min(1.0, static_cast<double>(used) / static_cast<double>(total)));
        if (width > o.max_thickness_px) continue;  // a filled area
        found.push_back(DetectedLine{on_centre(best0), on_centre(best1 + 1.0), width});
    }
    progress(0.9);

    // Short pieces go only after the merge: a line broken at a crossing.
    while (merge_collinear_once(found, o)) {
    }
    if (o.parallel_px > 0.0) {
        std::erase_if(found, [&](const DetectedLine& l) { return l.length() < o.min_length_px * 0.5; });
        while (merge_parallel_once(found, o)) {
        }
        while (merge_collinear_once(found, o)) {
        }
    }
    // The fitted centre line may run a little past the picture's edge.
    std::erase_if(found, [&](DetectedLine& l) {
        return !clip_to(l.a, l.b, raster.width, raster.height) || l.length() < o.min_length_px;
    });
    // A fixed direction and order: left to right (else top to bottom), longest first.
    for (DetectedLine& l : found)
        if (l.a.x > l.b.x + 1e-9 || (std::fabs(l.a.x - l.b.x) <= 1e-9 && l.a.y > l.b.y)) std::swap(l.a, l.b);
    std::stable_sort(found.begin(), found.end(), [](const DetectedLine& p, const DetectedLine& q) {
        if (p.length() != q.length()) return p.length() > q.length();
        if (p.a.x != q.a.x) return p.a.x < q.a.x;
        return p.a.y < q.a.y;
    });
    progress(1.0);
    return found;
}

double wall_detect_scale(const PageSize& size, double base_scale, double metres_per_unit, std::uint64_t max_pixels) {
    double s = base_scale;
    if (metres_per_unit > 0.0) s = std::min(s, 50.0 * metres_per_unit);
    const double area = size.width * size.height;
    if (area > 0.0 && max_pixels > 0) s = std::min(s, std::sqrt(static_cast<double>(max_pixels) / area));
    return s;
}

std::vector<ProposedSegment> propose_walls(const PlanSource& source, int page, const Trim& trim, double metres_per_unit,
                                           const WallProposalOptions& p, const WallDetectControl& control) {
    const PageSize whole = source.page_size(page);
    const Trim used = trim.shape == Trim::Shape::None ? whole_page_trim(whole) : trim;
    const TrimTransform tt(used);
    const double scale = wall_detect_scale(tt.floor_size(), default_base_scale(source, page), metres_per_unit, p.max_pixels);
    const PixelSize px = floor_pixels(tt, scale);
    const Raster raster = render_floor(source, page, tt, scale, {0, 0, static_cast<int>(px.width), static_cast<int>(px.height)});
    if (control.cancelled && control.cancelled()) return {};
    const double px_per_m = scale / metres_per_unit;
    WallDetectOptions o;
    o.dark_level = p.dark_level;
    o.min_length_px = p.min_length_m * px_per_m;
    o.max_gap_px = std::max(2.0, p.max_gap_m * px_per_m);
    o.merge_gap_px = std::max(o.max_gap_px, p.merge_gap_m * px_per_m);
    o.parallel_px = p.thickness_m * px_per_m;
    o.max_thickness_px = std::max(3.0, p.max_thickness_m * px_per_m);
    std::vector<ProposedSegment> out;
    for (const DetectedLine& l : detect_dark_lines(raster, o, control)) {
        ProposedSegment s;
        s.a = from_pixels(l.a, scale);
        s.b = from_pixels(l.b, scale);
        // The raster is whole pixels: its last one can reach past the floor.
        const PageSize floor = tt.floor_size();
        if (!clip_to(s.a, s.b, floor.width, floor.height)) continue;
        s.length_m = std::hypot(s.b.x - s.a.x, s.b.y - s.a.y) * metres_per_unit;
        out.push_back(s);
    }
    return out;
}

}  // namespace yantra::plan
