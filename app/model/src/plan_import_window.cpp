// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plan import window's editing state, see plan_import_window.h.
// Copied from yantra-v2 68ea20a: app/model/src/plan_import_window.cpp,
// adapted (string ids, building::Scale, core reanchoring, drawing scale,
// restricted-source credits).
#include "yantra/app/plan_import_window.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>

#include "yantra/core/building/edit.h"
#include "yantra/core/demo/demo_project.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

namespace yantra::app {

namespace fs = std::filesystem;
using building::Scale;

namespace {

constexpr int kLargeSheetPx = 15000;  // IND-02: larger sheets are rendered in tiles
constexpr double kMetresPerInch = 0.0254;

double dist(Point2 a, Point2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// printf-style formatting of one number; `f` is always a literal here.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

std::string floor_label(const building::Floor& f) { return trf("{} (level {})", {f.name, std::to_string(f.level)}); }

std::string suggested_name(int level) {
    if (level == 0) return tr("Ground floor");
    if (level < 0) return tr("Basement ") + std::to_string(-level);
    return tr("Floor ") + std::to_string(level);
}

std::string trimmed(const std::string& text) {
    std::size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a])) != 0) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1])) != 0) --b;
    return text.substr(a, b - a);
}

/// A whole-string number ("2.5", "2,5"); nullopt when anything is left.
std::optional<double> number(std::string s) {
    for (char& c : s)
        if (c == ',') c = '.';
    if (s.empty()) return std::nullopt;
    std::size_t used = 0;
    double v = 0.0;
    try {
        v = std::stod(s, &used);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (used != s.size()) return std::nullopt;
    return v;
}

std::string file_sha256(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    util::Sha256 h;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) h.update(buf.data(), static_cast<std::size_t>(n));
    }
    if (in.bad()) return {};
    return h.finish_hex();
}

/// The two plan points of a TwoPoints / ScaleBar scale.
bool has_line(const Scale& s) { return s.method == Scale::Method::TwoPoints || s.method == Scale::Method::ScaleBar; }

}  // namespace

std::optional<double> parse_distance_m(const std::string& text, std::string& error) {
    std::string s = trimmed(text);
    if (!s.empty() && s.back() == 'm') s = trimmed(s.substr(0, s.size() - 1));
    if (s.empty()) {
        error = tr("Type the real distance in metres.");
        return std::nullopt;
    }
    const auto v = number(s);
    if (!v) {
        error = "\"" + text + "\" is not a number of metres.";
        return std::nullopt;
    }
    if (!std::isfinite(*v) || *v <= 0.0) {
        error = tr("The distance must be more than 0 m.");
        return std::nullopt;
    }
    return v;
}

std::optional<double> parse_ratio(const std::string& text, std::string& error) {
    std::string s = trimmed(text);
    if (const std::size_t colon = s.find(':'); colon != std::string::npos) {
        if (trimmed(s.substr(0, colon)) != "1") {
            error = tr("Type the drawing scale as 1:N, e.g. 1:100.");
            return std::nullopt;
        }
        s = trimmed(s.substr(colon + 1));
    }
    if (s.empty()) {
        error = tr("Type the drawing scale, e.g. 1:100.");
        return std::nullopt;
    }
    const auto v = number(s);
    if (!v) {
        error = "\"" + text + "\" is not a drawing scale such as 1:100.";
        return std::nullopt;
    }
    if (!std::isfinite(*v) || *v < 1.0) {
        error = tr("The drawing scale must be 1:1 or smaller (1:N with N at least 1).");
        return std::nullopt;
    }
    return v;
}

std::optional<RestrictedView> find_restricted_sheet(const std::vector<fs::path>& data_folders,
                                                    const std::string& sha256) {
    if (sha256.empty()) return std::nullopt;
    for (const fs::path& folder : data_folders) {
        std::error_code ec;
        const fs::path demo_dir = folder / "demo";
        if (!fs::is_directory(demo_dir, ec)) continue;
        for (const auto& sub : fs::directory_iterator(demo_dir, ec)) {
            if (!sub.is_directory()) continue;
            std::error_code ec2;
            for (const auto& e : fs::directory_iterator(sub.path(), ec2)) {
                if (e.path().extension() != ".json") continue;
                try {
                    const demo::DemoDescription d = demo::read_description(e.path());
                    for (const demo::DemoSheet& s : d.sheets) {
                        if (!s.restricted || s.sha256 != sha256) continue;
                        RestrictedView v;
                        v.title = s.title;
                        std::string credit = s.credit.holder;
                        std::string who = s.credit.designer;
                        if (!s.credit.year.empty()) who += (who.empty() ? "" : ", ") + s.credit.year;
                        if (!s.credit.inventory_number.empty())
                            who += (who.empty() ? "inv. " : tr(", inv. ")) + s.credit.inventory_number;
                        if (!who.empty()) credit += (credit.empty() ? "" : "; ") + who;
                        v.credit = credit;
                        v.licence = s.licence;
                        v.licence_url = s.licence_url;
                        v.item_url = s.item_url;
                        for (const demo::DemoFloor& f : d.floors)
                            if (f.sheet == s.id && f.scale_approximate) v.scale_approximate = true;
                        v.note = tr("Licence-restricted plan: the project keeps a reference to your cached copy (checked "
                                 "by its SHA-256), never a copy. It is not altered, packaged or put into exports.");
                        return v;
                    }
                } catch (const std::exception&) {
                    // Not a demo description (e.g. a scenario list).
                }
            }
        }
    }
    return std::nullopt;
}

// ---- opening -----------------------------------------------------------------

PlanImportWindow::PlanImportWindow(project::Project& project, std::unique_ptr<plan::PlanImport> import,
                                   std::optional<RestrictedView> restricted)
    : project_(project), import_(std::move(import)), restricted_(std::move(restricted)) {}

std::unique_ptr<PlanImportWindow> PlanImportWindow::open_file(project::Project& project, const fs::path& file,
                                                              const std::vector<fs::path>& data_folders,
                                                              std::string& error, const plan::OpenLimits& limits) {
    const std::string name = file.filename().string();
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        error = tr("Cannot add ") + name + tr(": the file does not exist.");
        return nullptr;
    }
    std::optional<project::AddSourceResult> ref;
    try {
        std::optional<RestrictedView> restricted = find_restricted_sheet(data_folders, file_sha256(file));
        std::unique_ptr<plan::PlanImport> imp;
        if (restricted) {
            // IND-03, DEMO-12: a licence-restricted plan stays in its cache;
            // the project keeps a reference with its SHA-256.
            ref = project.add_source_reference(file);
            imp = plan::PlanImport::open(project, ref->id, limits);
        } else {
            imp = plan::PlanImport::add_file(project, file, limits);
        }
        return std::unique_ptr<PlanImportWindow>(new PlanImportWindow(project, std::move(imp), std::move(restricted)));
    } catch (const plan::PlanError& e) {
        if (ref && ref->added) {
            try {
                project.remove_source(ref->id);
            } catch (const std::exception&) {
                // Nothing else to undo.
            }
        }
        error = tr("Cannot read ") + name + tr(" as a plan: ") + e.what();
    } catch (const std::exception& e) {
        error = tr("Cannot add ") + name + ": " + e.what();
    }
    return nullptr;
}

std::unique_ptr<PlanImportWindow> PlanImportWindow::open_source(project::Project& project, const std::string& source_id,
                                                                const std::vector<fs::path>& data_folders,
                                                                std::string& error, const plan::OpenLimits& limits) {
    const auto s = project.source(source_id);
    try {
        auto imp = plan::PlanImport::open(project, source_id, limits);
        std::optional<RestrictedView> restricted;
        if (s && !s->plan.embedded) restricted = find_restricted_sheet(data_folders, s->plan.sha256);
        return std::unique_ptr<PlanImportWindow>(new PlanImportWindow(project, std::move(imp), std::move(restricted)));
    } catch (const std::exception& e) {
        error = tr("Cannot open the plan ") + (s ? s->plan.file_name : source_id) + ": " + e.what();
    }
    return nullptr;
}

std::string PlanImportWindow::title() const {
    const auto s = project_.source(import_->source_id());
    return s ? s->plan.file_name : tr("Plan");
}

bool PlanImportWindow::referenced() const {
    const auto s = project_.source(import_->source_id());
    return s && !s->plan.embedded;
}

std::string PlanImportWindow::description() const {
    const plan::PlanSource& src = import_->source();
    std::string d = src.describe();
    if (src.kind() == plan::SourceKind::Pdf) {
        // Images already say their pixel size; a PDF page is drawn at 300 dpi.
        const plan::PageSize ps = src.page_size(page());
        const plan::PixelSize px = plan::page_pixels(ps, plan::default_base_scale(src, page()));
        d += ", page " + std::to_string(page() + 1) + ": " + fmt("%.0f", ps.width * 25.4 / 72.0) + " x " +
             fmt("%.0f", ps.height * 25.4 / 72.0) + " mm (" + std::to_string(px.width) + " x " +
             std::to_string(px.height) + tr(" px at 300 dpi)");
    }
    if (referenced()) d += tr("; referenced by its SHA-256, not copied into the project");
    return d;
}

bool PlanImportWindow::large_sheet() const {
    const plan::PlanSource& src = import_->source();
    const plan::PixelSize px = plan::page_pixels(src.page_size(page()), plan::default_base_scale(src, page()));
    return px.width > kLargeSheetPx || px.height > kLargeSheetPx;
}

std::vector<building::Floor> PlanImportWindow::floors_here() const {
    std::vector<building::Floor> out;
    for (building::Floor& f : project_.floors())
        if (f.source == import_->source_id()) out.push_back(std::move(f));
    return out;
}

// ---- pages ---------------------------------------------------------------------

std::string PlanImportWindow::page_label(int page) const {
    std::size_t n = 0;
    for (const plan::PlacedTrim& t : import_->placed()) n += t.page == page ? 1 : 0;
    std::string s = tr("Page ") + std::to_string(page + 1);
    if (n > 0) s += " (" + std::to_string(n) + (n == 1 ? " floor)" : " floors)");
    return s;
}

Result PlanImportWindow::set_page(int page) {
    if (page < 0 || page >= page_count()) return fail(tr("There is no page ") + std::to_string(page + 1) + ".");
    if (editing()) return fail(tr("Apply or cancel the trim you are editing first."));
    try {
        cancel_draft();
        import_->set_page(page);
        trim_changed();
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot show the page: ")) + e.what());
    }
    if (const auto f = project_.floor(selected_); !f || f->page != page) {
        selected_.clear();
        scale_ = ScaleForm{};
    }
    return Result{true, tr("Page ") + std::to_string(page + 1)};
}

// ---- tools -------------------------------------------------------------------

std::vector<ToolView> PlanImportWindow::tools() const {
    const bool floor_here = [&] {
        const auto f = project_.floor(selected_);
        return f && f->page == page();
    }();
    const std::string need_floor =
        tr("Select a floor first: add the trim as a floor, or click its outline with Pan/select.");
    std::vector<ToolView> out = {
        {ImportTool::Pan, "tool.pan", "Pan/select", "Drag to move the sheet; click a floor's outline to select it.",
         true, ""},
        {ImportTool::Rectangle, "tool.rectangle", "Rectangle",
         tr("Drag a rectangle around one floor. Drag a corner of the current rectangle to adjust it."), true, ""},
        {ImportTool::Polygon, "tool.polygon", "Polygon",
         tr("Click the corners of an irregular outline; click the first point or Finish to close it. Drag a corner "
         "of the current polygon to adjust it."),
         true, ""},
        {ImportTool::Deskew, "tool.deskew", "Deskew line",
         tr("Drag along a wall or the sheet's border that should be straight: the plan turns so it is."), true, ""},
        {ImportTool::ScaleLine, "tool.scale_line", "Two points",
         tr("Drag between two points whose real distance you know, then type the distance and Apply."), floor_here,
         floor_here ? "" : need_floor},
        {ImportTool::ScaleBar, "tool.scale_bar", "Scale bar",
         tr("Drag along the plan's scale bar, end to end, then type its length and Apply."), floor_here,
         floor_here ? "" : need_floor},
        {ImportTool::Reference, "tool.reference", "Reference point",
         tr("Click a point found on every floor (e.g. a staircase corner) to line the floors up."), floor_here,
         floor_here ? "" : need_floor},
    };
    return out;
}

Result PlanImportWindow::set_tool(ImportTool tool) {
    for (const ToolView& t : tools()) {
        if (t.tool != tool) continue;
        if (!t.enabled) return fail(t.label + ": " + t.disabled_reason);
    }
    cancel_draft();
    tool_ = tool;
    if (tool == ImportTool::ScaleLine || tool == ImportTool::ScaleBar) {
        const ScaleForm::Kind k = tool == ImportTool::ScaleBar ? ScaleForm::Kind::ScaleBar : ScaleForm::Kind::TwoPoints;
        if (scale_.kind != k) {
            scale_.kind = k;
            scale_.a.reset();
            scale_.b.reset();
        }
    }
    return Result{};
}

std::string PlanImportWindow::hint() const {
    switch (tool_) {
    case ImportTool::Pan: return tr("Drag to pan, wheel to zoom. Click a floor's outline to select it.");
    case ImportTool::Rectangle: return tr("Drag a rectangle around the floor, or drag a corner to adjust it.");
    case ImportTool::Polygon:
        if (polygon_.empty()) return tr("Click the first corner of the outline, or drag a corner of the current polygon.");
        return std::to_string(polygon_.size()) +
               tr(" points. Click the next corner; click the first point or Finish to close [Enter], Undo point "
               "[Backspace], Esc cancels.");
    case ImportTool::Deskew: return tr("Drag along a line that should be horizontal or vertical.");
    case ImportTool::ScaleLine:
        return tr("Drag from one point to another whose real distance you know, type the distance and Apply.");
    case ImportTool::ScaleBar: return tr("Drag along the scale bar from end to end, type its length and Apply.");
    case ImportTool::Reference: return tr("Click the reference point (the same spot on every floor, e.g. a stair corner).");
    }
    return {};
}

// ---- pointer input -------------------------------------------------------------

std::optional<std::size_t> PlanImportWindow::handle_at(Point2 p) const {
    if (!has_handles()) return std::nullopt;
    const plan::Trim& t = import_->trim();
    std::optional<std::size_t> best;
    double best_d = pick_;
    for (std::size_t i = 0; i < t.outline.size(); ++i) {
        const double d = dist(p, t.outline[i]);
        if (d <= best_d) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

bool PlanImportWindow::has_handles() const {
    const plan::Trim& t = import_->trim();
    // The whole page has no handles: a drag from its corner draws a new
    // rectangle.
    if (t == plan::whole_page_trim(import_->source().page_size(page()), t.quarter_turns, t.deskew_deg)) return false;
    return (tool_ == ImportTool::Rectangle && t.shape == plan::Trim::Shape::Rectangle) ||
           (tool_ == ImportTool::Polygon && t.shape == plan::Trim::Shape::Polygon && polygon_.empty());
}

bool PlanImportWindow::drafting() const noexcept { return down_ || !polygon_.empty(); }

void PlanImportWindow::cancel_draft() {
    down_ = false;
    start_.reset();
    current_.reset();
    drag_handle_.reset();
    rect_anchor_.reset();
    polygon_.clear();
    drag_outline_.clear();
}

bool PlanImportWindow::pointer_down(Point2 p) {
    down_ = true;
    start_ = p;
    current_ = p;
    switch (tool_) {
    case ImportTool::Pan: return false;
    case ImportTool::Rectangle: {
        const auto h = handle_at(p);
        rect_anchor_ = h ? import_->trim().outline[(*h + 2) % 4] : p;
        return true;
    }
    case ImportTool::Polygon:
        if (const auto h = handle_at(p)) {
            drag_handle_ = h;
            drag_outline_ = import_->trim().outline;
        }
        return true;
    default: return true;
    }
}

void PlanImportWindow::pointer_move(Point2 p) {
    if (!down_) return;
    current_ = p;
    if (drag_handle_ && *drag_handle_ < drag_outline_.size()) drag_outline_[*drag_handle_] = p;
}

Result PlanImportWindow::pointer_up(Point2 p) {
    if (!down_) return Result{};
    pointer_move(p);
    const Point2 a = start_.value_or(p);
    down_ = false;
    const bool click = dist(a, p) <= pick_;
    auto end = [&](Result r) {
        start_.reset();
        current_.reset();
        rect_anchor_.reset();
        drag_handle_.reset();
        drag_outline_.clear();
        return r;
    };
    try {
        switch (tool_) {
        case ImportTool::Pan:
            if (click) {
                if (const auto id = floor_at(p)) return end(select_floor(*id));
            }
            return end(Result{});
        case ImportTool::Rectangle: {
            const Point2 anchor = rect_anchor_.value_or(a);
            if (dist(anchor, p) <= pick_) return end(fail(tr("Drag a larger rectangle around the floor.")));
            import_->set_rectangle(anchor, p);
            trim_changed();
            return end(Result{true, tr("Trim: ") + trim_text()});
        }
        case ImportTool::Polygon:
            if (drag_handle_) {
                std::vector<Point2> outline = drag_outline_;
                try {
                    import_->set_polygon(std::move(outline));
                } catch (const std::exception& e) {
                    return end(fail(std::string(tr("This outline cannot be used: ")) + e.what()));
                }
                trim_changed();
                return end(Result{true, tr("Trim: ") + trim_text()});
            }
            if (polygon_.size() >= 3 && dist(p, polygon_.front()) <= pick_) return end(finish_polygon());
            if (!polygon_.empty() && dist(p, polygon_.back()) <= pick_ * 0.25) return end(Result{});  // double click
            polygon_.push_back(p);
            return end(Result{});
        case ImportTool::Deskew:
            if (click) return end(fail(tr("Drag a longer line along the wall or border.")));
            import_->deskew_from_line(a, p);
            trim_changed();
            return end(Result{true, "Deskew " + fmt("%.2f", deskew()) + " degrees"});
        case ImportTool::ScaleLine:
        case ImportTool::ScaleBar:
            if (click) return end(fail(tr("Drag a longer line between the two points.")));
            scale_.kind = tool_ == ImportTool::ScaleBar ? ScaleForm::Kind::ScaleBar : ScaleForm::Kind::TwoPoints;
            scale_.a = a;
            scale_.b = p;
            return end(Result{true, tr("Now type the real distance in metres and Apply.")});
        case ImportTool::Reference: return end(set_reference(p));
        }
    } catch (const std::exception& e) {
        return end(fail(e.what()));
    }
    return end(Result{});
}

Result PlanImportWindow::finish_polygon() {
    if (polygon_.size() < 3) return fail(tr("A polygon needs at least 3 corners."));
    try {
        import_->set_polygon(polygon_);
    } catch (const std::exception& e) {
        return fail(std::string(tr("This outline cannot be used: ")) + e.what() + ". Undo the last points or Esc.");
    }
    polygon_.clear();
    trim_changed();
    return Result{true, tr("Trim: ") + trim_text()};
}

void PlanImportWindow::undo_point() {
    if (!polygon_.empty()) polygon_.pop_back();
}

DraftView PlanImportWindow::draft() const {
    DraftView d;
    const plan::Trim& t = import_->trim();
    d.trim = t.outline;
    d.trim_is_polygon = t.shape == plan::Trim::Shape::Polygon;
    if (down_ && tool_ == ImportTool::Rectangle && rect_anchor_ && current_)
        d.trim = plan::rectangle_trim(*rect_anchor_, *current_, t.quarter_turns, t.deskew_deg).outline;
    if (down_ && drag_handle_) d.trim = drag_outline_;
    d.polygon = polygon_;
    if (down_ && start_ && current_ &&
        (tool_ == ImportTool::Deskew || tool_ == ImportTool::ScaleLine || tool_ == ImportTool::ScaleBar))
        d.line = std::make_pair(*start_, *current_);
    if (has_handles()) d.handles = d.trim;
    return d;
}

// ---- rotation ------------------------------------------------------------------

Result PlanImportWindow::rotate(int quarter_steps) {
    cancel_draft();
    import_->rotate_quarter(quarter_steps);
    trim_changed();
    return Result{true, tr("Turned to ") + std::to_string(quarter_turns() * 90) + " degrees"};
}

Result PlanImportWindow::set_deskew(double deg) {
    try {
        import_->set_deskew(deg);
    } catch (const std::exception& e) {
        return fail(std::string(tr("Deskew: ")) + e.what());
    }
    trim_changed();
    return Result{true, "Deskew " + fmt("%.2f", deg) + " degrees"};
}

Result PlanImportWindow::reset_trim() {
    cancel_draft();
    import_->reset_trim();
    trim_changed();
    return Result{true, tr("Trim: the whole page")};
}

std::string PlanImportWindow::trim_text() const {
    const plan::Trim& t = import_->trim();
    std::string s = t.shape == plan::Trim::Shape::Polygon ? tr("Polygon (") + std::to_string(t.outline.size()) + " corners)"
                                                          : tr("Rectangle");
    try {
        const plan::PageSize z = import_->trim_size();
        s += " " + fmt("%.0f", z.width) + " x " + fmt("%.0f", z.height) + " units";
    } catch (const std::exception&) {
        s += tr(" (not valid)");
    }
    const double rot = t.quarter_turns * 90.0 + t.deskew_deg;
    if (rot != 0.0) s += ", turned " + fmt("%.2f", rot) + " deg";
    return s;
}

// ---- add as floor --------------------------------------------------------------

void PlanImportWindow::begin_add_floor() {
    cancel_draft();
    std::set<int> used;
    for (const building::Floor& f : project_.floors()) used.insert(f.level);
    int level = used.empty() ? 0 : *used.rbegin() + 1;
    if (used.count(0) == 0) level = 0;
    add_form_ = ImportFloorForm{suggested_name(level), level, 3.0};
}

Result PlanImportWindow::confirm_add_floor() {
    if (!add_form_) return fail(tr("No floor form is open."));
    if (editing()) return fail(tr("Apply or cancel the trim you are editing first."));
    const ImportFloorForm f = *add_form_;
    if (trimmed(f.name).empty()) return fail(tr("The floor needs a name."));
    if (!(f.height_m > 0.0 && f.height_m <= 50.0)) return fail(tr("The floor height must be more than 0 and at most 50 m."));
    for (const building::Floor& other : project_.floors())
        if (other.level == f.level)
            return fail(trf("Level {} is already used by \"{}\": every floor has its own level.",
                            {std::to_string(f.level), other.name}));
    try {
        const std::string id = import_->add_as_floor(trimmed(f.name), f.level, f.height_m);
        added_.push_back(id);
        add_form_.reset();
        selected_ = id;
        load_scale_form();
        import_->reset_trim();
        trim_changed();
        return Result{true, tr("Added \"") + trimmed(f.name) + "\". Trim the next floor, or set this floor's scale."};
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot add the floor: ")) + e.what());
    }
}

// ---- placed trims --------------------------------------------------------------

std::vector<PlacedView> PlanImportWindow::placed() const {
    std::vector<PlacedView> out;
    const building::Building b = project_.building();
    const std::vector<building::Floor> floors = floors_here();
    for (const building::Floor& f : floors) {
        if (f.trim.shape == plan::Trim::Shape::None) continue;
        PlacedView v;
        v.floor_id = f.id;
        v.label = floor_label(f);
        v.page = f.page;
        v.outline = f.trim.outline;
        Point2 c{0.0, 0.0};
        for (const Point2& p : f.trim.outline) c = Point2{c.x + p.x, c.y + p.y};
        const double n = static_cast<double>(std::max<std::size_t>(f.trim.outline.size(), 1));
        v.label_at = Point2{c.x / n, c.y / n};
        v.on_page = f.page == page();
        v.selected = f.id == selected_;
        v.editing = editing() && *editing() == f.id;
        const std::optional<double> mpu = b.metres_per_unit(f);
        v.to_scale = mpu.has_value();
        v.aligned = f.reference_point.has_value();
        if (mpu) {
            v.scale_text = fmt("%.4g m per unit", *mpu);
            switch (f.scale.method) {
            case Scale::Method::TwoPoints: v.scale_text += " (two points " + fmt("%g", f.scale.distance_m) + " m)"; break;
            case Scale::Method::ScaleBar: v.scale_text += " (scale bar " + fmt("%g", f.scale.distance_m) + " m)"; break;
            case Scale::Method::Shared:
                if (const building::Floor* from = b.find_floor(f.scale.shared_floor))
                    v.scale_text += tr(" (shared with ") + from->name + ")";
                break;
            default: break;
            }
        } else {
            v.scale_text = tr("not to scale");
        }
        for (const std::string& id : building::floors_sharing_scale(b, f.id))
            if (const building::Floor* o = b.find_floor(id))
                v.shared_with += (v.shared_with.empty() ? "" : ", ") + o->name;
        try {
            if (has_line(f.scale))
                v.scale_line = std::make_pair(plan::trim_floor_to_page(f.trim, f.scale.a),
                                              plan::trim_floor_to_page(f.trim, f.scale.b));
            if (f.reference_point) v.reference = plan::trim_floor_to_page(f.trim, *f.reference_point);
        } catch (const std::exception&) {
            // A stored floor always has a valid trim; nothing to show otherwise.
        }
        out.push_back(std::move(v));
    }
    return out;
}

Result PlanImportWindow::select_floor(const std::string& floor_id) {
    const auto f = project_.floor(floor_id);
    if (!f || f->source != import_->source_id()) return fail(tr("That floor is not cut from this plan."));
    if (f->page != page()) {
        if (editing()) return fail(tr("Apply or cancel the trim you are editing first."));
        cancel_draft();
        import_->set_page(f->page);
        trim_changed();
    }
    selected_ = floor_id;
    load_scale_form();
    return Result{true, tr("Selected ") + floor_label(*f)};
}

std::optional<std::string> PlanImportWindow::floor_at(Point2 page_point) const {
    std::optional<std::string> hit;
    for (const building::Floor& f : floors_here()) {
        if (f.page != page() || f.trim.shape == plan::Trim::Shape::None) continue;
        try {
            if (plan::TrimTransform(f.trim).contains_page(page_point)) {
                // The selected floor first when trims overlap, else the first.
                if (f.id == selected_) return f.id;
                if (!hit) hit = f.id;
            }
        } catch (const std::exception&) {
        }
    }
    return hit;
}

Result PlanImportWindow::edit_floor(const std::string& floor_id) {
    if (editing() && *editing() != floor_id) return fail(tr("Apply or cancel the trim you are editing first."));
    const auto f = project_.floor(floor_id);
    if (!f || f->source != import_->source_id()) return fail(tr("That floor is not cut from this plan."));
    try {
        cancel_draft();
        add_form_.reset();
        import_->edit_floor(floor_id);
        trim_changed();
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    selected_ = floor_id;
    load_scale_form();
    if (tool_ != ImportTool::Polygon && tool_ != ImportTool::Rectangle)
        tool_ = import_->trim().shape == plan::Trim::Shape::Polygon ? ImportTool::Polygon : ImportTool::Rectangle;
    return Result{true, tr("Editing the trim of \"") + f->name + "\": adjust it, then Apply (or Cancel)."};
}

Result PlanImportWindow::apply_edit() {
    if (!editing()) return fail(tr("No trim is being edited."));
    const std::string id = *editing();
    try {
        // The core keeps the scale points, the reference point and the
        // measured points where they are on the sheet (IND-07).
        import_->apply_to_floor();
        import_->stop_editing();
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot change the trim: ")) + e.what());
    }
    cancel_draft();
    import_->reset_trim();
    trim_changed();
    load_scale_form();
    const auto f = project_.floor(id);
    return Result{true, tr("Trim of \"") + (f ? f->name : id) + "\" changed."};
}

Result PlanImportWindow::cancel_edit() {
    if (!editing()) return Result{};
    cancel_draft();
    import_->stop_editing();
    import_->reset_trim();
    trim_changed();
    return Result{true, tr("Trim not changed.")};
}

Result PlanImportWindow::request_remove(const std::string& floor_id) {
    const auto f = project_.floor(floor_id);
    if (!f || f->source != import_->source_id()) return fail(tr("That floor is not cut from this plan."));
    if (editing()) return fail(tr("Apply or cancel the trim you are editing first."));
    pending_remove_ = floor_id;
    return Result{};
}

std::string PlanImportWindow::remove_question() const {
    if (!pending_remove_) return {};
    const auto f = project_.floor(*pending_remove_);
    if (!f) return {};
    return trf("Remove the floor \"{}\" (level {})? The plan file stays in the project.", {f->name, std::to_string(f->level)});
}

Result PlanImportWindow::confirm_remove() {
    if (!pending_remove_) return fail(tr("Nothing to remove."));
    const std::string id = *pending_remove_;
    pending_remove_.reset();
    const auto f = project_.floor(id);
    try {
        import_->remove_floor(id);
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot remove the floor: ")) + e.what());
    }
    if (selected_ == id) {
        selected_.clear();
        scale_ = ScaleForm{};
    }
    std::erase(added_, id);
    return Result{true, tr("Removed \"") + (f ? f->name : id) + "\"."};
}

// ---- scale and alignment -------------------------------------------------------

void PlanImportWindow::load_scale_form() {
    ScaleForm s;
    s.kind = tool_ == ImportTool::ScaleBar ? ScaleForm::Kind::ScaleBar : ScaleForm::Kind::TwoPoints;
    const auto f = project_.floor(selected_);
    if (f) {
        try {
            if (has_line(f->scale)) {
                s.kind = f->scale.method == Scale::Method::ScaleBar ? ScaleForm::Kind::ScaleBar
                                                                     : ScaleForm::Kind::TwoPoints;
                s.a = plan::trim_floor_to_page(f->trim, f->scale.a);
                s.b = plan::trim_floor_to_page(f->trim, f->scale.b);
                s.distance_m = fmt("%g", f->scale.distance_m);
            } else if (f->scale.method == Scale::Method::Factor && drawing_scale_available()) {
                const double upi = import_->source().units_per_inch(f->page);
                s.ratio = fmt("%g", std::round(f->scale.metres_per_unit * upi / kMetresPerInch * 1000.0) / 1000.0);
            }
        } catch (const std::exception&) {
        }
    }
    scale_ = s;
}

bool PlanImportWindow::drawing_scale_available() const {
    try {
        return import_->source().units_per_inch(page()) > 0.0;
    } catch (const std::exception&) {
        return false;
    }
}

Result PlanImportWindow::apply_scale() {
    const auto f = project_.floor(selected_);
    if (!f) return fail(tr("Select a floor first."));
    if (f->page != page()) return fail(tr("The selected floor is on another page."));
    std::string error;
    std::optional<Scale> s;
    if (scale_.kind == ScaleForm::Kind::DrawingScale) {
        if (!drawing_scale_available())
            return fail(tr("This page has no physical size (an image without a stored resolution): use two points or "
                        "the scale bar."));
        const auto n = parse_ratio(scale_.ratio, error);
        if (!n) return fail(error);
        // 1:N: one page unit is 1 / units_per_inch inch on paper, N times
        // that in the building.
        s = building::factor_scale(*n * kMetresPerInch / import_->source().units_per_inch(page()));
    } else {
        if (!scale_.a || !scale_.b)
            return fail(scale_.kind == ScaleForm::Kind::ScaleBar ? tr("Drag along the scale bar first.")
                                                                 : tr("Drag between the two points first."));
        if (dist(*scale_.a, *scale_.b) < 1.0) return fail(tr("The two points are too close together."));
        const auto d = parse_distance_m(scale_.distance_m, error);
        if (!d) return fail(error);
        const Point2 a = plan::trim_page_to_floor(f->trim, *scale_.a);
        const Point2 b = plan::trim_page_to_floor(f->trim, *scale_.b);
        s = scale_.kind == ScaleForm::Kind::ScaleBar ? building::scale_bar(a, b, *d)
                                                     : building::two_point_scale(a, b, *d);
    }
    if (!s) return fail(tr("This scale cannot be used."));
    try {
        project_.set_scale(selected_, *s);
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot set the scale: ")) + e.what());
    }
    const auto after = project_.floor(selected_);
    const std::optional<double> mpu = after ? project_.building().metres_per_unit(*after) : std::nullopt;
    return Result{true, "Scale of \"" + f->name + "\": " + fmt("%.4g", mpu.value_or(0.0)) + " m per unit."};
}

Result PlanImportWindow::clear_scale() {
    const auto f = project_.floor(selected_);
    if (!f) return fail(tr("Select a floor first."));
    try {
        project_.set_scale(selected_, Scale{});
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot remove the scale: ")) + e.what());
    }
    load_scale_form();
    return Result{true, "\"" + f->name + "\" is now marked \"not to scale\"."};
}

std::vector<ShareChoice> PlanImportWindow::share_choices() const {
    std::vector<ShareChoice> out;
    const auto sel = project_.floor(selected_);
    if (!sel) return out;
    for (const building::Floor& f : floors_here()) {
        if (f.id == sel->id || !building::same_sheet(f, *sel)) continue;
        const bool shared = f.scale.method == Scale::Method::Shared && f.scale.shared_floor == sel->id;
        out.push_back(ShareChoice{f.id, floor_label(f), shared});
    }
    return out;
}

Result PlanImportWindow::share_scale(const std::string& floor_id, bool share) {
    const auto sel = project_.floor(selected_);
    const auto f = project_.floor(floor_id);
    if (!sel || !f) return fail(tr("Select a floor first."));
    try {
        if (share) {
            if (!project_.building().has_scale(*sel))
                return fail(tr("Set the scale of \"") + sel->name + "\" first; then other floors can share it.");
            project_.share_scale(selected_, {floor_id});
            return Result{true, "\"" + f->name + "\" now shares the scale of \"" + sel->name + "\"."};
        }
        // Keeps the value as its own scale.
        const std::optional<double> mpu = project_.building().metres_per_unit(*f);
        project_.set_scale(floor_id, mpu ? *building::factor_scale(*mpu) : Scale{});
        return Result{true, "\"" + f->name + "\" keeps the value but no longer follows \"" + sel->name + "\"."};
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}

Result PlanImportWindow::set_reference(Point2 page_point) {
    const auto f = project_.floor(selected_);
    if (!f) return fail(tr("Select a floor first."));
    if (f->page != page()) return fail(tr("The selected floor is on another page."));
    try {
        if (!plan::TrimTransform(f->trim).contains_page(page_point))
            return fail(tr("The reference point must be inside the floor's trim."));
        project_.set_reference_point(selected_, plan::trim_page_to_floor(f->trim, page_point));
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return Result{true, tr("Reference point of \"") + f->name + "\" set."};
}

Result PlanImportWindow::clear_reference() {
    const auto f = project_.floor(selected_);
    if (!f) return fail(tr("Select a floor first."));
    try {
        project_.set_reference_point(selected_, std::nullopt);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return Result{true, tr("Reference point of \"") + f->name + "\" removed."};
}

void PlanImportWindow::refresh() {
    if (!selected_.empty() && !project_.floor(selected_)) {
        selected_.clear();
        scale_ = ScaleForm{};
    }
    if (pending_remove_ && !project_.floor(*pending_remove_)) pending_remove_.reset();
}

}  // namespace yantra::app
