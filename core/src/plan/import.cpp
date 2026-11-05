// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/plan/import.cpp, adapted (string ids, building::Floor, plan size, source pages).

#include "yantra/core/plan/import.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace yantra::plan {

using Code = PlanError::Code;

namespace {

// Rotation of a trim frame (quarter turns exact, deskew by cos/sin).
struct Frame {
    int q = 0;
    double d = 0.0;
    double c = 1.0, s = 0.0;
    Frame(int quarters, double deskew) : q(quarters & 3), d(deskew) {
        if (d != 0.0) {
            c = std::cos(d * std::numbers::pi / 180.0);
            s = std::sin(d * std::numbers::pi / 180.0);
        }
    }
    static Point2 quarter(Point2 p, int k) {
        switch (k & 3) {
        case 1: return {-p.y, p.x};
        case 2: return {-p.x, -p.y};
        case 3: return {p.y, -p.x};
        default: return p;
        }
    }
    Point2 turn(Point2 p) const {
        const Point2 r = d == 0.0 ? p : Point2{p.x * c - p.y * s, p.x * s + p.y * c};
        return quarter(r, q);
    }
    Point2 unturn(Point2 p) const {
        const Point2 r = quarter(p, 4 - q);
        return d == 0.0 ? r : Point2{r.x * c + r.y * s, -r.x * s + r.y * c};
    }
};

Point2 centroid(const std::vector<Point2>& o) {
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (std::size_t i = 0, n = o.size(); i < n; ++i) {
        const Point2& p = o[i];
        const Point2& q = o[(i + 1) % n];
        const double k = p.x * q.y - q.x * p.y;
        a += k;
        cx += (p.x + q.x) * k;
        cy += (p.y + q.y) * k;
    }
    if (std::fabs(a) < 1e-12) {
        Point2 m{0, 0};
        for (const Point2& p : o) m = {m.x + p.x, m.y + p.y};
        const double n = o.empty() ? 1.0 : static_cast<double>(o.size());
        return {m.x / n, m.y / n};
    }
    return {cx / (3.0 * a), cy / (3.0 * a)};
}

}  // namespace

// ---- PlanView ---------------------------------------------------------------

Point2 PlanView::turned(Point2 p) const noexcept { return Frame(quarters_, deskew_).turn(p); }
Point2 PlanView::unturned(Point2 p) const noexcept { return Frame(quarters_, deskew_).unturn(p); }

void PlanView::set_viewport(double width, double height) {
    if (width > 0.0 && height > 0.0 && std::isfinite(width) && std::isfinite(height)) {
        vw_ = width;
        vh_ = height;
    }
}

void PlanView::fit(const PageSize& page, int quarter_turns, double deskew_deg) {
    quarters_ = quarter_turns & 3;
    deskew_ = deskew_deg;
    Point2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    for (const Point2 p : {Point2{0, 0}, Point2{page.width, 0}, Point2{page.width, page.height}, Point2{0, page.height}}) {
        const Point2 t = turned(p);
        lo = {std::min(lo.x, t.x), std::min(lo.y, t.y)};
        hi = {std::max(hi.x, t.x), std::max(hi.y, t.y)};
    }
    const double bw = std::max(hi.x - lo.x, 1e-9);
    const double bh = std::max(hi.y - lo.y, 1e-9);
    zoom_ = std::clamp(std::min(vw_ / bw, vh_ / bh), 1e-4, 1e4);
    pan_ = {vw_ / 2.0 - zoom_ * (lo.x + hi.x) / 2.0, vh_ / 2.0 - zoom_ * (lo.y + hi.y) / 2.0};
}

void PlanView::set_rotation(int quarter_turns, double deskew_deg) {
    const Point2 anchor = screen_to_page({vw_ / 2.0, vh_ / 2.0});
    quarters_ = quarter_turns & 3;
    deskew_ = deskew_deg;
    const Point2 t = turned(anchor);
    pan_ = {vw_ / 2.0 - zoom_ * t.x, vh_ / 2.0 - zoom_ * t.y};
}

void PlanView::zoom_at(double factor, Point2 anchor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) return;
    const Point2 p = screen_to_page(anchor);
    zoom_ = std::clamp(zoom_ * factor, 1e-4, 1e4);
    const Point2 t = turned(p);
    pan_ = {anchor.x - zoom_ * t.x, anchor.y - zoom_ * t.y};
}

void PlanView::pan_by(Point2 delta) { pan_ = {pan_.x + delta.x, pan_.y + delta.y}; }

Point2 PlanView::page_to_screen(Point2 page) const noexcept {
    const Point2 t = turned(page);
    return {zoom_ * t.x + pan_.x, zoom_ * t.y + pan_.y};
}

Point2 PlanView::screen_to_page(Point2 screen) const noexcept {
    return unturned({(screen.x - pan_.x) / zoom_, (screen.y - pan_.y) / zoom_});
}

std::pair<Point2, Point2> PlanView::visible_page_bounds() const noexcept {
    Point2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    for (const Point2 s : {Point2{0, 0}, Point2{vw_, 0}, Point2{vw_, vh_}, Point2{0, vh_}}) {
        const Point2 p = screen_to_page(s);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    return {lo, hi};
}

// ---- helpers ----------------------------------------------------------------

double default_base_scale(const PlanSource& source, int page) {
    if (source.kind() == SourceKind::Image) return 1.0;
    const PageSize s = source.page_size(page);
    return std::min(300.0 / 72.0, static_cast<double>(1 << 30) / std::max(s.width, s.height));
}

std::shared_ptr<const PlanSource> open_project_source(const project::Project& project, const std::string& source_id,
                                                      const OpenLimits& limits) {
    const auto info = project.source(source_id);
    if (!info) throw project::ProjectError(project::ProjectError::Code::NotFound, "no source " + source_id);
    const std::vector<std::uint8_t> bytes = project.read_source(source_id);
    return std::shared_ptr<const PlanSource>(open_plan_source(bytes, info->plan.file_name, limits));
}

std::unique_ptr<TileRenderer> floor_tiles(std::shared_ptr<const PlanSource> source, const building::Floor& floor,
                                          double base_scale, std::size_t budget_bytes, TileRenderer::ReadyFn ready) {
    if (floor.trim.shape == Trim::Shape::None) throw PlanError(Code::Invalid, "floor " + floor.id + " has no trim");
    auto t = std::make_shared<const TrimTransform>(floor.trim);
    TileLayout layout(t->floor_size(), base_scale);
    const int page = floor.page;
    return std::make_unique<TileRenderer>(
        layout,
        [source = std::move(source), t, page](int, double scale, const PixelRect& w) {
            return render_floor(*source, page, *t, scale, w);
        },
        budget_bytes, std::move(ready));
}

// ---- PlanImport ---------------------------------------------------------------

PlanImport::PlanImport(project::Project& project, const std::string& source_id, std::shared_ptr<const PlanSource> source)
    : project_(project), source_id_(source_id), source_(std::move(source)) {
    trim_ = whole_page_trim(source_->page_size(0));
    refit();
}

std::unique_ptr<PlanImport> PlanImport::add_file(project::Project& project, const std::filesystem::path& file,
                                                 const OpenLimits& limits) {
    const project::AddSourceResult r = project.add_source(file);
    try {
        return open(project, r.id, limits);
    } catch (const PlanError&) {
        if (r.added) project.remove_source(r.id);  // not a plan: do not keep it
        throw;
    }
}

std::unique_ptr<PlanImport> PlanImport::open(project::Project& project, const std::string& source_id,
                                             const OpenLimits& limits) {
    auto src = open_project_source(project, source_id, limits);
    // Record the page sizes once (IND-02: the page list without the file).
    if (project.source_pages(source_id).empty()) {
        std::vector<project::SourcePage> pages;
        for (int p = 0; p < src->page_count(); ++p) {
            const PageSize size = src->page_size(p);
            pages.push_back({size.width, size.height, src->units_per_inch(p)});
        }
        project.set_source_pages(source_id, pages);
    }
    return std::unique_ptr<PlanImport>(new PlanImport(project, source_id, std::move(src)));
}

void PlanImport::refit() { view_.fit(source_->page_size(page_), trim_.quarter_turns, trim_.deskew_deg); }

void PlanImport::set_page(int page) {
    const PageSize size = source_->page_size(page);  // NoSuchPage
    page_ = page;
    trim_ = whole_page_trim(size, trim_.quarter_turns, trim_.deskew_deg);
    editing_.reset();
    refit();
}

void PlanImport::set_rectangle(Point2 a, Point2 b) {
    Trim t = rectangle_trim(a, b, trim_.quarter_turns, trim_.deskew_deg);
    validate_trim(t);
    trim_ = std::move(t);
}

void PlanImport::set_polygon(std::vector<Point2> outline) {
    Trim t{Trim::Shape::Polygon, std::move(outline), trim_.quarter_turns, trim_.deskew_deg};
    validate_trim(t);
    trim_ = std::move(t);
}

void PlanImport::rotate_quarter(int steps) {
    trim_.quarter_turns = ((trim_.quarter_turns + steps) % 4 + 4) % 4;
    view_.set_rotation(trim_.quarter_turns, trim_.deskew_deg);
}

void PlanImport::set_deskew(double deg) {
    if (!std::isfinite(deg) || std::fabs(deg) > kMaxDeskewDeg)
        throw PlanError(Code::Invalid, "deskew must be between -45 and 45 degrees");
    const PageSize page = source_->page_size(page_);
    if (trim_ == whole_page_trim(page, trim_.quarter_turns, trim_.deskew_deg)) {
        trim_ = whole_page_trim(page, trim_.quarter_turns, deg);
    } else if (trim_.shape == Trim::Shape::Rectangle) {
        // Same centre and size, in the new frame.
        const TrimTransform old(trim_);
        const PageSize s = old.floor_size();
        const Point2 centre = old.floor_to_page({s.width / 2.0, s.height / 2.0});
        const Frame f(trim_.quarter_turns, deg);
        const Point2 c = f.turn(centre);
        trim_ = rectangle_trim(f.unturn({c.x - s.width / 2.0, c.y - s.height / 2.0}),
                               f.unturn({c.x + s.width / 2.0, c.y + s.height / 2.0}), trim_.quarter_turns, deg);
    } else {
        trim_.deskew_deg = deg;
    }
    view_.set_rotation(trim_.quarter_turns, trim_.deskew_deg);
}

void PlanImport::deskew_from_line(Point2 a, Point2 b) { set_deskew(plan::deskew_from_line(a, b)); }

void PlanImport::reset_trim() {
    trim_ = whole_page_trim(source_->page_size(page_), trim_.quarter_turns, trim_.deskew_deg);
}

PageSize PlanImport::trim_size() const { return TrimTransform(trim_).floor_size(); }

Raster PlanImport::preview(int max_px) const {
    if (max_px <= 0) throw PlanError(Code::Invalid, "preview size must be > 0");
    const TrimTransform t(trim_);
    const PageSize s = t.floor_size();
    const double scale = static_cast<double>(max_px) / std::max(s.width, s.height);
    const PixelSize px = floor_pixels(t, scale);
    return render_floor(*source_, page_, t, scale,
                        {0, 0, static_cast<int>(std::min<std::int64_t>(px.width, max_px)),
                         static_cast<int>(std::min<std::int64_t>(px.height, max_px))});
}

std::string PlanImport::add_as_floor(const std::string& name, int level, double height_m) {
    building::Floor f;
    f.name = name;
    f.level = level;
    f.height_m = height_m;
    f.source = source_id_;
    f.page = page_;
    f.trim = trim_;
    const PageSize size = TrimTransform(trim_).floor_size();
    f.plan_width = size.width;
    f.plan_height = size.height;
    return project_.add_floor(std::move(f));
}

std::vector<PlacedTrim> PlanImport::placed() const {
    std::vector<PlacedTrim> out;
    for (const building::Floor& f : project_.floors()) {
        if (f.source != source_id_ || f.trim.shape == Trim::Shape::None) continue;
        out.push_back(PlacedTrim{f.id, f.name, f.level, f.page, f.trim, centroid(f.trim.outline)});
    }
    return out;
}

void PlanImport::edit_floor(const std::string& floor_id) {
    const auto f = project_.floor(floor_id);
    if (!f || f->source != source_id_)
        throw project::ProjectError(project::ProjectError::Code::NotFound,
                                    "floor " + floor_id + " is not cut from this source");
    const PageSize size = source_->page_size(f->page);
    page_ = f->page;
    trim_ = f->trim.shape == Trim::Shape::None ? whole_page_trim(size) : f->trim;
    editing_ = floor_id;
    refit();
}

void PlanImport::apply_to_floor() {
    if (!editing_) throw PlanError(Code::Invalid, "no floor is being edited");
    auto f = project_.floor(*editing_);
    if (!f) {
        editing_.reset();
        throw project::ProjectError(project::ProjectError::Code::NotFound, "the edited floor was removed");
    }
    // Walls, antennas, scale points and the reference point stay where
    // they are on the sheet (IND-07 with IND-08..12).
    reanchor_floor(*f, f->trim, trim_);
    f->page = page_;
    f->trim = trim_;
    const PageSize size = TrimTransform(trim_).floor_size();
    f->plan_width = size.width;
    f->plan_height = size.height;
    project_.update_floor(*f);
}

void PlanImport::remove_floor(const std::string& floor_id) {
    const auto f = project_.floor(floor_id);
    if (!f || f->source != source_id_)
        throw project::ProjectError(project::ProjectError::Code::NotFound,
                                    "floor " + floor_id + " is not cut from this source");
    project_.remove_floor(floor_id);
    if (editing_ == floor_id) editing_.reset();
}

std::unique_ptr<TileRenderer> PlanImport::page_tiles(std::size_t budget_bytes, TileRenderer::ReadyFn ready) const {
    TileLayout layout(source_->page_size(page_), default_base_scale(*source_, page_));
    return std::make_unique<TileRenderer>(
        layout, [src = source_](int page, double scale, const PixelRect& w) { return src->render(page, scale, w); },
        budget_bytes, std::move(ready));
}

}  // namespace yantra::plan
