// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Legend layout partly modelled on yantra-v1 c24e8f5: core/export/image.cpp
// (legend box, banner), adapted for EXP-08 indoor (floor plans, walls,
// antennas, the shared point list's scale).

#include "yantra/core/export/image.h"

#include <algorithm>
#include <cmath>

#include "yantra/core/plan/import.h"
#include "yantra/core/plan/trim.h"

namespace yantra::exports {

namespace {

constexpr Rgba8 kText{20, 20, 20, 255};
constexpr Rgba8 kWall{60, 60, 60, 220};
constexpr Rgba8 kAntenna{30, 70, 200, 255};
constexpr Rgba8 kOutline{0, 0, 0, 160};

std::string band_label(const survey::ColourScale& s, std::size_t i) {
    const survey::ColourBand& b = s.bands[i];
    if (!b.label.empty()) return b.label;
    return (s.lower_is_better ? "<= " : ">= ") + number_text(b.from) + (s.unit.empty() ? "" : " " + s.unit);
}

}  // namespace

std::vector<LegendEntry> legend_entries(const Table& t) {
    const survey::ColourScale& s = t.scale;
    std::vector<std::size_t> counts(s.bands.size() + 1, 0);
    std::size_t none = 0;
    for (const Row& r : t.rows) {
        const auto b = band_of(s, r.value);
        if (!b) ++none;
        else ++counts[*b];
    }
    std::vector<LegendEntry> out;
    // Best first: the highest band (or the lowest for lower-is-better).
    for (std::size_t k = 0; k < s.bands.size(); ++k) {
        const std::size_t i = s.lower_is_better ? k : s.bands.size() - 1 - k;
        out.push_back({band_label(s, i), s.bands[i].rgb, counts[i + 1]});
    }
    std::string outside = "outside";
    if (!s.bands.empty())
        outside = (s.lower_is_better ? "> " : "< ") +
                  number_text(s.lower_is_better ? s.bands.back().from : s.bands.front().from) +
                  (s.unit.empty() ? "" : " " + s.unit);
    out.push_back({outside, s.outside_rgb, counts[0]});
    out.push_back({"No value", s.no_value_rgb, none});
    return out;
}

ImageResult floor_image(const project::Project& project, const Table& table, const ImageOptions& o) {
    if (o.max_plan_px < 100 || o.max_plan_px > 8000) throw ExportError("image: the plan size must be 100..8000 px");
    const auto floor = project.floor(table.floor_id);
    if (!floor) throw ExportError("unknown floor '" + table.floor_id + "'");
    if (floor->plan_width <= 0 || floor->plan_height <= 0) throw ExportError("floor '" + floor->name + "' has no plan size");
    ImageResult res;

    Font font;
    if (!o.font_file.empty()) {
        std::string err;
        if (!font.load(o.font_file, &err)) res.warnings.push_back(err + ": text is left out");
    } else {
        res.warnings.push_back("no font given: text is left out");
    }

    const double scale = o.max_plan_px / std::max(floor->plan_width, floor->plan_height);
    const int pw = std::max(1, static_cast<int>(std::lround(floor->plan_width * scale)));
    const int ph = std::max(1, static_cast<int>(std::lround(floor->plan_height * scale)));
    const double ui = std::max(1.0, o.max_plan_px / 1600.0);
    const double text_px = 15 * ui;
    const int margin = static_cast<int>(12 * ui);
    const int banner_h = o.banner.empty() ? 0 : static_cast<int>(34 * ui);
    const int title_h = static_cast<int>(28 * ui);
    const std::vector<LegendEntry> entries = legend_entries(table);
    const int legend_w = o.legend ? static_cast<int>(250 * ui) : 0;
    const int legend_h = o.legend ? static_cast<int>((entries.size() + 3) * 22 * ui) : 0;
    const int top = banner_h + title_h;
    const int width = pw + 2 * margin + (o.legend ? legend_w + margin : 0);
    const int height = top + std::max(ph, legend_h) + 2 * margin;
    Canvas c(width, height);
    const double ox = margin, oy = top + margin;

    // The plan.
    c.fill_rect(ox, oy, ox + pw, oy + ph, {248, 248, 248, 255});
    if (o.plan && !floor->source.empty()) {
        try {
            const auto info = project.source(floor->source);
            std::shared_ptr<const plan::PlanSource> source;
            if (info && !info->plan.embedded) {
                // A licence-restricted reference (DEMO-12): drawn only from
                // the user's cached copy given for this picture, checked
                // against the project's SHA-256 now (never from the hint).
                res.restricted_plan = true;
                if (o.reference_file.empty())
                    throw ExportError("it is a licence-restricted reference (no cached copy given)");
                const std::vector<std::uint8_t> bytes = project.read_source_from(floor->source, o.reference_file);
                source = plan::open_plan_source(bytes, info->plan.file_name);
            } else {
                source = plan::open_project_source(project, floor->source);
            }
            plan::Raster raster;
            if (floor->trim.shape == plan::Trim::Shape::None)
                raster = source->render(floor->page, scale, {0, 0, pw, ph});
            else
                raster = plan::render_floor(*source, floor->page, plan::TrimTransform(floor->trim), scale, {0, 0, pw, ph});
            c.draw_rgba(raster.rgba.data(), raster.width, raster.height, ox, oy, raster.width, raster.height);
            res.plan_drawn = true;
        } catch (const std::exception& e) {
            res.warnings.push_back(std::string("the plan is not drawn: ") + e.what());
        }
    }
    c.stroke_rect(ox - 1, oy - 1, ox + pw + 1, oy + ph + 1, 1, kOutline);
    auto px = [&](building::Point2 p) { return building::Point2{ox + p.x * scale, oy + p.y * scale}; };
    if (o.walls)
        for (const building::Wall& w : floor->walls) {
            std::vector<double> xy;
            for (const auto& p : w.points) {
                const auto q = px(p);
                xy.push_back(q.x);
                xy.push_back(q.y);
            }
            c.polyline(xy, 2 * ui, kWall);
        }

    // The points: every row with a plan position, in list order.
    std::size_t placed = 0;
    for (const Row& r : table.rows) placed += r.plan ? 1 : 0;
    double radius = o.point_radius_px;
    if (radius <= 0) {
        const double area = static_cast<double>(pw) * ph;
        radius = placed > 0 ? std::clamp(std::sqrt(area / static_cast<double>(placed)) / 4.0, 1.5 * ui, 6.0 * ui) : 6 * ui;
    }
    for (const Row& r : table.rows) {
        if (!r.plan) continue;
        const auto q = px(*r.plan);
        c.fill_circle(q.x, q.y, radius, rgb(r.rgb));
        if (radius >= 3) c.ring(q.x, q.y, radius, std::max(0.75, radius / 6), kOutline);
        ++res.points;
    }
    if (o.antennas)
        for (const building::Antenna& a : floor->antennas) {
            const auto q = px(a.position);
            const double s = 7 * ui;
            c.fill_polygon({q.x, q.y - s, q.x + s, q.y + s * 0.8, q.x - s, q.y + s * 0.8}, kAntenna);
            font.draw(c, q.x + s + 2, q.y - text_px / 2, a.name.empty() ? a.id : a.name, text_px * 0.85, kAntenna);
        }

    // Banner, title, legend.
    if (banner_h > 0) {
        c.fill_rect(0, 0, width, banner_h, {200, 30, 30, 255});
        const double bw = font.text_width(o.banner, 20 * ui);
        font.draw(c, (width - bw) / 2, (banner_h - 20 * ui) / 2, o.banner, 20 * ui, {255, 255, 255, 255});
    }
    const std::string title = o.title.empty() ? project.meta().info.name + ", " + floor->name : o.title;
    font.draw(c, margin, banner_h + (title_h - 18 * ui) / 2 + 4 * ui, title, 18 * ui, kText);
    if (o.legend) {
        const double lx = ox + pw + margin, ly = oy;
        c.fill_rect(lx, ly, lx + legend_w, ly + legend_h, {255, 255, 255, 235});
        c.stroke_rect(lx, ly, lx + legend_w, ly + legend_h, 1, kOutline);
        const double row = 22 * ui, pad = 10 * ui;
        font.draw(c, lx + pad, ly + pad, table.metric.label(), text_px, kText);
        double y = ly + pad + row * 1.3;
        for (const LegendEntry& e : entries) {
            c.fill_rect(lx + pad, y + 2 * ui, lx + pad + 14 * ui, y + 16 * ui, rgb(e.rgb));
            c.stroke_rect(lx + pad, y + 2 * ui, lx + pad + 14 * ui, y + 16 * ui, 1, kOutline);
            font.draw(c, lx + pad + 22 * ui, y, e.label, text_px, kText);
            const std::string n = std::to_string(e.count);
            font.draw(c, lx + legend_w - pad - font.text_width(n, text_px), y, n, text_px, kText);
            y += row;
        }
        const std::string total = std::to_string(table.rows.size()) + " points";
        font.draw(c, lx + pad, y + row * 0.3, total, text_px, kText);
    }
    if (placed != table.rows.size())
        res.warnings.push_back(std::to_string(table.rows.size() - placed) + " rows have no plan position");
    res.canvas = std::move(c);
    return res;
}

}  // namespace yantra::exports
