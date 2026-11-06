// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's model, see project_window.h.
// Partly copied from yantra-v2 68ea20a: app/model/src/project_window.cpp, adapted.
#include "yantra/app/project_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

#include "yantra/core/plan/import.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/viewer.h"

namespace yantra::app {

namespace {

/// `v` with `decimals` decimals and a unit: fixed(2.5, 1, " m") = "2.5 m".
std::string fixed(double v, int decimals, const char* unit) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f%s", decimals, v, unit);
    return buf;
}

std::string plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

/// Heavier walls darker: light grey (plasterboard) to near black
/// (concrete, metal), slightly warm so walls stand out from a grey plan.
std::uint32_t wall_rgb(double attenuation_db) {
    const double t = std::clamp(attenuation_db / 20.0, 0.0, 1.0);
    const auto v = static_cast<std::uint32_t>(std::lround(200.0 - 170.0 * t));
    const std::uint32_t r = std::min<std::uint32_t>(255U, v + 40U);
    return (r << 16U) | (v << 8U) | (v / 2U);
}

/// The frequency the wall colours use: the model's default band.
constexpr double kWallColourMhz = 400.0;

bool has_plan(const building::Floor& f) { return !f.source.empty() && f.trim.shape != building::Trim::Shape::None; }

}  // namespace

ScaleBar scale_bar(std::optional<double> metres_per_unit, double screen_px_per_unit, double target_px) {
    ScaleBar s;
    if (!metres_per_unit || !(*metres_per_unit > 0.0) || !(screen_px_per_unit > 0.0) || !(target_px > 0.0)) {
        s.label = "not to scale";
        return s;
    }
    const double metres_per_px = *metres_per_unit / screen_px_per_unit;
    const double want = target_px * metres_per_px;
    const double decade = std::pow(10.0, std::floor(std::log10(want)));
    double len = decade;
    for (double f : {1.0, 2.0, 5.0, 10.0})
        if (f * decade <= want * 1.0000001) len = f * decade;
    s.length_m = len;
    s.pixels = len / metres_per_px;
    if (len >= 1000.0)
        s.label = fixed(len / 1000.0, 0, " km");
    else if (len >= 1.0)
        s.label = fixed(len, 0, " m");
    else if (len >= 0.01)
        s.label = fixed(len * 100.0, 0, " cm");
    else
        s.label = fixed(len * 1000.0, len * 1000.0 >= 1.0 ? 0 : 3, " mm");
    return s;
}

ProjectWindow::ProjectWindow(project::Project& project)
    : project_(project), dirty_(std::make_shared<std::atomic<bool>>(true)) {
    // The listener may run on a worker thread (background jobs): it only
    // sets the flag; the shared_ptr keeps the flag alive for it.
    token_ = project_.subscribe([flag = dirty_](const project::Change& c) {
        if (c.kind != project::Change::Kind::Saved && c.kind != project::Change::Kind::Info) flag->store(true);
    });
}

ProjectWindow::~ProjectWindow() {
    try {
        project_.unsubscribe(token_);
    } catch (...) {
        // A closed project has no listeners left.
    }
}

void ProjectWindow::invalidate() noexcept { dirty_->store(true); }

void ProjectWindow::refresh() {
    if (!dirty_->exchange(false)) return;
    tabs_.clear();
    content_.reset();
    const building::Building b = project_.building();
    const auto counts = survey::point_counts(project_);
    for (const std::size_t i : b.floors_by_level()) {
        const building::Floor& f = b.floors[i];
        FloorTab t;
        t.id = f.id;
        t.name = f.name;
        t.level = f.level;
        t.height_m = f.height_m;
        t.to_scale = b.has_scale(f);
        t.has_plan = has_plan(f);
        for (const auto& [id, n] : counts)
            if (id == f.id) t.points = n;
        t.label = f.name + " (" + std::to_string(f.level) + ")";
        t.tooltip = "Level " + std::to_string(f.level) + ", floor-to-floor height " + fixed(f.height_m, 2, " m") +
                    "\n" + (t.to_scale ? "To scale" : "Not to scale: distances and areas are not shown") + "\n" +
                    (t.has_plan ? "Plan: trimmed from its source" : "No plan picture yet") + "\n" +
                    plural(t.points, "measured point", "measured points");
        tabs_.push_back(std::move(t));
    }
    const bool still_there =
        std::any_of(tabs_.begin(), tabs_.end(), [&](const FloorTab& t) { return t.id == selected_; });
    if (!still_there) {
        selected_ = tabs_.empty() ? std::string() : tabs_.front().id;
        chosen_interpolation_.reset();
    }
}

const std::vector<FloorTab>& ProjectWindow::tabs() {
    refresh();
    return tabs_;
}

const std::string& ProjectWindow::selected() {
    refresh();
    return selected_;
}

bool ProjectWindow::select(const std::string& floor_id) {
    refresh();
    for (const FloorTab& t : tabs_) {
        if (t.id != floor_id) continue;
        if (selected_ != floor_id) {
            content_.reset();
            chosen_interpolation_.reset();
        }
        selected_ = floor_id;
        return true;
    }
    return false;
}

const FloorContent* ProjectWindow::content(double bin_size_m) {
    refresh();
    if (selected_.empty()) return nullptr;
    if (!content_ || content_->floor_id != selected_) {
        content_ = build_content(selected_, bin_size_m);
        content_->generation = ++generation_;
        content_bin_size_ = bin_size_m;
    } else if (content_bin_size_ != bin_size_m) {
        const building::Building b = project_.building();
        if (const building::Floor* f = b.find_floor(selected_)) build_binned(*content_, b, *f, bin_size_m);
        content_bin_size_ = bin_size_m;
        content_->generation = ++generation_;
    }
    return &*content_;
}

bool ProjectWindow::show_interpolation(std::int64_t id) {
    const FloorContent* c = content(content_bin_size_ > 0.0 ? content_bin_size_ : 2.0);
    if (c == nullptr) return false;
    const bool known = std::any_of(c->interpolations.begin(), c->interpolations.end(),
                                   [&](const auto& e) { return e.first == id; });
    if (!known) return false;
    chosen_interpolation_ = id;
    invalidate();
    return true;
}

FloorContent ProjectWindow::build_content(const std::string& floor_id, double bin_size_m) const {
    FloorContent c;
    c.floor_id = floor_id;
    const building::Building b = project_.building();
    const building::Floor* floor = b.find_floor(floor_id);
    if (floor == nullptr) return c;
    c.name = floor->name;
    c.width = floor->plan_width;
    c.height = floor->plan_height;
    c.metres_per_unit = b.metres_per_unit(*floor);
    c.has_plan = has_plan(*floor);
    if (!c.has_plan)
        c.plan_note = "This floor has no plan picture yet: the walls, antennas and points are drawn on a blank sheet.";

    for (const building::Wall& w : floor->walls) {
        const double att = b.wall_attenuation_db(w, kWallColourMhz).value_or(0.0);
        const std::string cls = w.wall_class.empty() ? std::string("own value") : w.wall_class;
        c.walls.push_back(PlanWall{w.points, wall_rgb(att), cls + ", " + fixed(att, 1, " dB")});
    }
    for (const building::Antenna& a : floor->antennas)
        c.antennas.push_back(PlanAntenna{a.position, a.name.empty() ? a.id : a.name,
                                         a.type == building::AntennaType::OutdoorDonor, a.direction_deg});

    survey::PointQuery q;
    q.floor_id = floor_id;
    c.points = survey::point_list(project_, q);
    c.legend = c.points.scale;

    // Without a plan picture the sheet is the drawing's extent.
    if (!(c.width > 0.0 && c.height > 0.0)) {
        double w = 0.0, h = 0.0;
        auto grow = [&](Point2 p) {
            w = std::max(w, p.x);
            h = std::max(h, p.y);
        };
        for (const PlanWall& pw : c.walls)
            for (const Point2& p : pw.points) grow(p);
        for (const PlanAntenna& a : c.antennas) grow(a.position);
        for (const survey::SurveyPoint& p : c.points.points) grow(p.plan);
        c.width = w > 0.0 ? w * 1.1 : 1000.0;
        c.height = h > 0.0 ? h * 1.1 : 700.0;
    }
    build_binned(c, b, *floor, bin_size_m);
    build_interpolation(c, b, *floor);
    return c;
}

void ProjectWindow::build_binned(FloorContent& c, const building::Building& b, const building::Floor& f,
                                 double size_m) const {
    c.binned.reset();
    c.binned_note.clear();
    if (!c.metres_per_unit) {
        c.binned_note = "Needs a scale on this floor (squares are in metres).";
        return;
    }
    if (c.points.count() == 0) {
        c.binned_note = "No measured points on this floor yet.";
        return;
    }
    BinnedLayer layer;
    try {
        layer.view = survey::bin_points(c.points, size_m);
    } catch (const std::exception& e) {
        c.binned_note = std::string("Cannot bin the points: ") + e.what();
        return;
    }
    layer.label = layer.view.label;
    layer.left_out = layer.view.left_out;
    const double half = layer.view.size_m * 0.5;
    for (const survey::Bin& bin : layer.view.bins) {
        const auto lo = b.from_plan_metres(f, Point2{bin.centre_m.x - half, bin.centre_m.y - half});
        const auto hi = b.from_plan_metres(f, Point2{bin.centre_m.x + half, bin.centre_m.y + half});
        if (!lo || !hi) continue;
        layer.bins.push_back(PlanBin{*lo, *hi, bin.rgb, bin.value.has_value()});
    }
    c.binned = std::move(layer);
}

void ProjectWindow::build_interpolation(FloorContent& c, const building::Building& b, const building::Floor& f) const {
    c.interpolation.reset();
    c.interpolations.clear();
    std::vector<project::StoredInterpolation> rows;
    try {
        rows = project_.interpolations(f.id, false);
    } catch (const std::exception& e) {
        c.interpolation_note = std::string("The stored interpolations cannot be read: ") + e.what();
        return;
    }
    if (rows.empty()) {
        c.interpolation_note = c.metres_per_unit ? "Not computed for this floor yet." : "Needs a scale on this floor.";
        return;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& x, const auto& y) {
        return x.created_utc != y.created_utc ? x.created_utc > y.created_utc : x.id > y.id;
    });
    for (const auto& r : rows) c.interpolations.emplace_back(r.id, r.key);
    std::int64_t id = rows.front().id;
    if (chosen_interpolation_ &&
        std::any_of(rows.begin(), rows.end(), [&](const auto& r) { return r.id == *chosen_interpolation_; }))
        id = *chosen_interpolation_;
    std::optional<survey::Interpolation> in;
    try {
        in = survey::stored_interpolation(project_, id);
    } catch (const std::exception& e) {
        c.interpolation_note = std::string("The interpolation cannot be read: ") + e.what();
        return;
    }
    if (!in) return;
    if (!c.metres_per_unit) {
        c.interpolation_note = "The floor has no scale any more: the stored map cannot be placed.";
        return;
    }
    InterpolationLayer layer;
    layer.id = in->id;
    layer.label = in->label();
    layer.key = in->request.key();
    const raytrace::Grid& g = in->grid;
    const auto lo = b.from_plan_metres(f, g.origin_m);
    const auto hi = b.from_plan_metres(f, Point2{g.origin_m.x + g.cell_m * g.cols, g.origin_m.y + g.cell_m * g.rows});
    if (!lo || !hi) return;
    layer.min = *lo;
    layer.max = *hi;
    layer.cols = g.cols;
    layer.rows = g.rows;
    const std::vector<std::uint32_t> argb = survey::cell_colours(*in, c.legend);
    layer.rgba.resize(argb.size() * 4U);
    for (std::size_t i = 0; i < argb.size(); ++i) {
        layer.rgba[i * 4 + 0] = static_cast<std::uint8_t>(argb[i] >> 16U);
        layer.rgba[i * 4 + 1] = static_cast<std::uint8_t>(argb[i] >> 8U);
        layer.rgba[i * 4 + 2] = static_cast<std::uint8_t>(argb[i]);
        layer.rgba[i * 4 + 3] = static_cast<std::uint8_t>(argb[i] >> 24U);
    }
    const survey::Staleness st = survey::staleness(project_, *in);
    layer.stale = st.stale;
    layer.stale_reasons = st.reasons;
    layer.rms_db = in->quality.rms_db;
    layer.fit_points = in->quality.points;
    layer.data = std::move(*in);
    c.interpolation = std::move(layer);
}

std::vector<LayerRow> ProjectWindow::layer_rows(const LayerSettings& layers) {
    std::vector<LayerRow> rows;
    const FloorContent* c = content(layers.bin_size_m);
    for (auto it = layers.order.rbegin(); it != layers.order.rend(); ++it) {
        const Layer l = *it;
        LayerRow r;
        r.layer = l;
        r.id = std::string("layer.") + layer_key(l);
        r.visible = layers.visible(l);
        const std::size_t pos = layers.position(l);
        r.can_move_up = pos + 1 < layers.order.size();
        r.can_move_down = pos > 0;
        switch (l) {
        case Layer::Plan:
            r.label = "Plan";
            r.tooltip = "The trimmed floor plan (the part of the source sheet that is this floor).";
            r.available = c && c->has_plan;
            if (c && !c->has_plan) r.note = c->plan_note;
            break;
        case Layer::Walls:
            r.label = "Walls (" + std::to_string(c ? c->walls.size() : 0) + ")";
            r.tooltip = "Walls drawn on this floor; darker lines attenuate more.";
            r.available = c && !c->walls.empty();
            break;
        case Layer::Antennas:
            r.label = "Antennas (" + std::to_string(c ? c->antennas.size() : 0) + ")";
            r.tooltip = "Antennas placed on this floor: indoor DAS antennas (circles) and outdoor donor directions "
                        "(triangles with an arrow).";
            r.available = c && !c->antennas.empty();
            break;
        case Layer::Points:
            r.label = "Measured points (" + std::to_string(c ? c->points.count() : 0) + ")";
            r.tooltip = "Every measured point of the floor's walk tests and manual points, coloured by level. "
                        "The report shows exactly these points; dense points are drawn smaller, never left out.";
            r.available = c && c->points.count() > 0;
            if (c && c->points.count() == 0) r.note = "No measured points on this floor yet.";
            break;
        case Layer::Binned:
            r.label = "Binned view";
            r.tooltip = "An extra view: the measured points grouped in squares, the median per square. It never "
                        "replaces the measured points.";
            r.available = c && c->binned.has_value();
            if (c && c->binned)
                r.note = c->binned->label;
            else if (c)
                r.note = c->binned_note;
            break;
        case Layer::Interpolation:
            r.label = std::string("Map (") +
                      (c && c->interpolation ? c->interpolation->label : std::string(survey::kInterpolatedLabel)) + ")";
            r.tooltip = "The interpolated map between the measured points (ray-tracing model fitted to the points). "
                        "Areas far from any measured point fade out. Always labelled \"interpolated\"; statistics "
                        "keep it apart from measured values.";
            r.available = c && c->interpolation.has_value();
            if (c && c->interpolation) {
                if (c->interpolation->stale) {
                    r.note = "Stale: ";
                    for (std::size_t i = 0; i < c->interpolation->stale_reasons.size(); ++i)
                        r.note += (i ? ", " : "") + c->interpolation->stale_reasons[i];
                    r.note += ". It is shown as computed; run it again to update it.";
                }
            } else if (c) {
                r.note = c->interpolation_note;
            }
            break;
        }
        rows.push_back(std::move(r));
    }
    return rows;
}

PlanTiles ProjectWindow::plan_tiles(const std::string& floor_id, std::size_t budget_bytes,
                                    plan::TileRenderer::ReadyFn ready) const {
    PlanTiles t;
    const std::optional<building::Floor> floor = project_.floor(floor_id);
    if (!floor || !has_plan(*floor)) {
        t.error = "This floor has no plan picture yet.";
        return t;
    }
    try {
        t.source = plan::open_project_source(project_, floor->source);
        t.renderer = plan::floor_tiles(t.source, *floor, plan::default_base_scale(*t.source, floor->page),
                                       budget_bytes, std::move(ready));
    } catch (const std::exception& e) {
        t.renderer.reset();
        t.error = std::string("The plan cannot be shown: ") + e.what();
    }
    return t;
}

HoverReadout ProjectWindow::hover(Point2 plan, double radius, const LayerSettings& layers) {
    HoverReadout h;
    const FloorContent* c = content(layers.bin_size_m);
    if (c == nullptr) return h;
    if (layers.visible(Layer::Points) && c->points.count() > 0) {
        if (const auto i = survey::nearest_point(c->points.points, plan, radius)) {
            const survey::SurveyPoint& p = c->points.points[*i];
            h.any = true;
            h.point = i;
            h.text = "Measured point " + std::to_string(*i + 1) + " of " + std::to_string(c->points.count()) + "\n" +
                     (p.value ? fixed(*p.value, 1, (" " + p.unit).c_str()) : std::string("no value")) + "  (" +
                     c->points.metric.label() + ")";
            if (!p.label.empty()) h.text += "\n" + p.label;
            if (!p.cell.empty()) h.text += "\nCell: " + p.cell;
            if (p.metres) h.text += "\nAt " + fixed(p.metres->x, 1, " m, ") + fixed(p.metres->y, 1, " m");
            return h;
        }
    }
    if (!layers.visible(Layer::Interpolation) || !c->interpolation || !c->metres_per_unit) return h;
    const Point2 m{plan.x * *c->metres_per_unit, plan.y * *c->metres_per_unit};
    const survey::Readout r = survey::readout(c->interpolation->data, m);
    if (!r.on_map) return h;
    h.any = true;
    h.text = (r.value_dbm ? fixed(*r.value_dbm, 1, " dBm") : std::string("no coverage")) + "  (" + r.label + ")";
    if (!r.measured) h.text += "\nConfidence " + fixed(r.confidence * 100.0, 0, " %");
    if (r.nearest)
        h.text += "\nNearest measured point " + fixed(r.nearest_m, 1, " m") + " away: " +
                  fixed(r.nearest->measured_dbm, 1, " dBm");
    if (c->interpolation->stale) h.text += "\nStale: the inputs changed since it was computed.";
    return h;
}

Result ProjectWindow::rename_floor(const std::string& id, const std::string& name) {
    if (name.empty()) return Result{false, "The floor needs a name."};
    try {
        project_.rename_floor(id, name);
    } catch (const std::exception& e) {
        return Result{false, std::string("Cannot rename the floor: ") + e.what()};
    }
    invalidate();
    return Result{true, "Renamed the floor to " + name};
}

Result ProjectWindow::set_level(const std::string& id, int level, double height_m) {
    if (!(height_m > 0.0 && height_m <= 50.0))
        return Result{false, "The floor height must be more than 0 and at most 50 m."};
    for (const building::Floor& f : project_.floors())
        if (f.level == level && f.id != id)
            return Result{false, "Level " + std::to_string(level) + " is used by floor \"" + f.name + "\"."};
    try {
        project_.set_floor_level(id, level, height_m);
    } catch (const std::exception& e) {
        return Result{false, std::string("Cannot change the level: ") + e.what()};
    }
    invalidate();
    return Result{true, "Level " + std::to_string(level) + ", height " + fixed(height_m, 2, " m")};
}

Result ProjectWindow::move_floor(const std::string& id, int steps) {
    const std::vector<building::Floor> floors = project_.floors();
    const auto it = std::find_if(floors.begin(), floors.end(), [&](const building::Floor& f) { return f.id == id; });
    if (it == floors.end()) return Result{false, "No such floor."};
    const long index = static_cast<long>(it - floors.begin()) + steps;
    if (steps == 0 || index < 0 || index >= static_cast<long>(floors.size()))
        return Result{false, steps < 0 ? "The floor is already the lowest." : "The floor is already the highest."};
    try {
        project_.move_floor(id, static_cast<std::size_t>(index));
    } catch (const std::exception& e) {
        return Result{false, std::string("Cannot move the floor: ") + e.what()};
    }
    invalidate();
    const std::optional<building::Floor> now = project_.floor(id);
    return Result{true, "Moved " + it->name + (steps < 0 ? " down" : " up") +
                            (now ? " (now level " + std::to_string(now->level) + ")" : std::string())};
}

Result ProjectWindow::request_remove(const std::string& id) {
    const std::optional<building::Floor> f = project_.floor(id);
    if (!f) return Result{false, "No such floor."};
    const std::size_t points = project_.point_count(id);
    const std::size_t sessions = project_.sessions(id).size();
    RemoveQuestion q;
    q.floor_id = id;
    q.title = "Remove floor?";
    q.question = "Remove the floor \"" + f->name + "\" (level " + std::to_string(f->level) + ") with its " +
                 plural(f->walls.size(), "wall", "walls") + ", " + plural(f->antennas.size(), "antenna", "antennas") +
                 ", " + plural(sessions, "walk test", "walk tests") + " and " +
                 plural(points, "measured point", "measured points") +
                 "? This cannot be undone. The plan source stays in the project; recording files are not deleted.";
    pending_remove_ = q;
    return Result{true, q.question};
}

Result ProjectWindow::confirm_remove() {
    if (!pending_remove_) return Result{false, "Nothing to remove."};
    const RemoveQuestion q = *pending_remove_;
    pending_remove_.reset();
    const std::optional<building::Floor> f = project_.floor(q.floor_id);
    if (!f) return Result{false, "The floor no longer exists."};
    // Select the neighbour (the next floor up, else the one below).
    std::string next;
    const std::vector<building::Floor> floors = project_.floors();
    for (std::size_t i = 0; i < floors.size(); ++i) {
        if (floors[i].id != q.floor_id) continue;
        if (i + 1 < floors.size())
            next = floors[i + 1].id;
        else if (i > 0)
            next = floors[i - 1].id;
    }
    try {
        project_.remove_floor(q.floor_id, /*with_sessions=*/true);
    } catch (const std::exception& e) {
        return Result{false, std::string("Cannot remove the floor: ") + e.what()};
    }
    invalidate();
    if (selected_ == q.floor_id) {
        selected_ = next;
        chosen_interpolation_.reset();
    }
    return Result{true, "Removed floor " + f->name};
}

void ProjectWindow::cancel_remove() { pending_remove_.reset(); }

}  // namespace yantra::app
