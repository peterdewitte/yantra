// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wall and antenna editing, see floor_editor.h.
#include "yantra/app/floor_editor.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <set>

#include "yantra/core/building/edit.h"
#include "yantra/core/project/project.h"

namespace yantra::app {

namespace {

std::string fixed(double v, int decimals, const char* unit) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f%s", decimals, v, unit);
    return buf;
}

constexpr double kShownMhz = 400.0;  // the attenuation shown for a class (as the wall colours)

bool finite(double v) { return std::isfinite(v); }

}  // namespace

const char* edit_tool_key(EditTool t) noexcept {
    switch (t) {
    case EditTool::Pan: return "pan";
    case EditTool::Select: return "select";
    case EditTool::Wall: return "wall";
    case EditTool::Antenna: return "antenna";
    case EditTool::Aim: return "aim";
    }
    return "pan";
}

FloorEditor::FloorEditor(project::Project& project) : project_(project) {}

void FloorEditor::set_floor(const std::string& floor_id) {
    if (floor_id == floor_) return;
    floor_ = floor_id;
    draft_.clear();
    hover_.reset();
    drag_from_.reset();
    drag_wall_.reset();
    drag_antenna_.reset();
    select({});
    if (tool_ == EditTool::Aim) tool_ = EditTool::Select;
}

std::optional<building::Floor> FloorEditor::floor_data() const {
    if (floor_.empty()) return std::nullopt;
    return project_.floor(floor_);
}

// -- tools ---------------------------------------------------------------------

std::vector<EditToolView> FloorEditor::tools() const {
    const auto a = selected_antenna();
    const bool aimable = a && (a->type == building::AntennaType::OutdoorDonor ||
                               a->pattern.type != building::AntennaPattern::Type::Omni);
    std::vector<EditToolView> v;
    auto add = [&](EditTool t, const char* label, const char* tip) {
        EditToolView e;
        e.tool = t;
        e.id = std::string("edit.tool.") + edit_tool_key(t);
        e.label = label;
        e.tooltip = tip;
        if (floor_.empty()) {
            e.enabled = t == EditTool::Pan;
            if (!e.enabled) e.reason = tr("Add a floor first.");
        }
        v.push_back(std::move(e));
    };
    add(EditTool::Pan, tr("Pan"), tr("Look around: drag to pan, wheel to zoom. Nothing on the plan changes. [P]"));
    add(EditTool::Select, tr("Select"),
        tr("Click a wall, a wall corner or an antenna to select it; drag to move it. Double click on the selected wall "
        "adds a corner there. Delete removes the selection. [S]"));
    add(EditTool::Wall, tr("Draw wall"),
        tr("Click the corners of a wall; double click or Enter ends it, Backspace removes the last corner, Esc drops it. "
        "The wall gets the class chosen in the panel. [W]"));
    add(EditTool::Antenna, tr("Place antenna"),
        tr("Click on the plan to place an antenna (indoor DAS antenna or outdoor donor direction), then set its "
        "properties. [A]"));
    add(EditTool::Aim, tr("Aim"),
        tr("Click where the selected directional antenna points, or the direction of the outdoor donor site."));
    if (!floor_.empty() && !aimable) {
        v.back().enabled = false;
        v.back().reason = tr("Select a directional antenna or an outdoor donor first.");
    }
    return v;
}

Result FloorEditor::set_tool(EditTool t) {
    for (const EditToolView& v : tools())
        if (v.tool == t && !v.enabled) return Result{false, v.reason};
    if (t != EditTool::Wall) draft_.clear();
    tool_ = t;
    return {};
}

Result FloorEditor::set_draw_class(const std::string& id) {
    for (const building::WallClass& c : project_.wall_classes())
        if (c.id == id) {
            draw_class_ = id;
            return {};
        }
    return Result{false, tr("There is no wall class '") + id + "'."};
}

// -- selection and forms -----------------------------------------------------------

void FloorEditor::select(Selection s) {
    selection_ = std::move(s);
    load_forms();
}

void FloorEditor::load_forms() {
    if (const auto w = selected_wall()) {
        wall_form_ = WallForm{};
        wall_form_.wall_id = w->id;
        wall_form_.wall_class = w->wall_class;
        wall_form_.own_value = w->attenuation_db.has_value();
        if (w->attenuation_db) wall_form_.attenuation_db = *w->attenuation_db;
        else if (const building::Building b = project_.building(); const auto* c = b.find_class(w->wall_class))
            wall_form_.attenuation_db = c->attenuation_at_db(kShownMhz);
    }
    if (const auto a = selected_antenna()) {
        AntennaForm f;
        f.antenna_id = a->id;
        f.name = a->name;
        f.model = a->model;
        f.outdoor = a->type == building::AntennaType::OutdoorDonor;
        f.height_m = a->height_m;
        f.gain_dbi = a->gain_dbi;
        f.directional = a->pattern.type == building::AntennaPattern::Type::Sector;
        f.azimuth_deg = a->pattern.azimuth_deg;
        f.beamwidth_deg = a->pattern.beamwidth_deg;
        f.front_to_back_db = a->pattern.front_to_back_db;
        f.direction_deg = a->direction_deg;
        f.eirp_known = a->eirp_dbm.has_value();
        if (a->eirp_dbm) f.eirp_dbm = *a->eirp_dbm;
        f.cable_loss_db = a->cable_loss_db;
        f.cell = a->cell;
        antenna_form_ = f;
    }
}

std::optional<building::Wall> FloorEditor::selected_wall() const {
    if (selection_.kind != Selection::Kind::Wall) return std::nullopt;
    const auto f = floor_data();
    if (!f) return std::nullopt;
    for (const building::Wall& w : f->walls)
        if (w.id == selection_.id) return w;
    return std::nullopt;
}

std::optional<building::Antenna> FloorEditor::selected_antenna() const {
    if (selection_.kind != Selection::Kind::Antenna) return std::nullopt;
    const auto f = floor_data();
    if (!f) return std::nullopt;
    for (const building::Antenna& a : f->antennas)
        if (a.id == selection_.id) return a;
    return std::nullopt;
}

std::string FloorEditor::selection_text() const {
    const building::Building b = project_.building();
    const building::Floor* f = b.find_floor(floor_);
    if (f == nullptr) return {};
    if (const auto w = selected_wall()) {
        const auto att = b.wall_attenuation_db(*w, kShownMhz);
        std::string t = tr("Wall ") + w->id + ": " + (w->attenuation_db ? std::string(tr("own value")) : w->wall_class) +
                        ", " + (att ? fixed(*att, 1, " dB") : std::string("?")) + ", " +
                        building::format_length(building::length_m(b, *f, w->points));
        if (selection_.vertex) t += tr(", corner ") + std::to_string(*selection_.vertex + 1) + " of " +
                                    std::to_string(w->points.size());
        return t;
    }
    if (const auto a = selected_antenna()) {
        std::string t = (a->type == building::AntennaType::OutdoorDonor ? tr("Outdoor donor ") : tr("Antenna ")) +
                        (a->name.empty() ? a->id : a->name) + ": " + fixed(a->gain_dbi, 1, " dBi") + ", " +
                        fixed(a->height_m, 1, tr(" m high"));
        if (const auto e = a->effective_eirp_dbm()) t += ", EIRP " + fixed(*e, 1, " dBm");
        else t += tr(", EIRP estimated");
        if (const building::Cell* c = b.find_cell(a->cell)) t += tr(", cell ") + (c->label.empty() ? c->id : c->label);
        return t;
    }
    return {};
}

// -- undo ------------------------------------------------------------------------------

FloorEditor::State FloorEditor::capture() const {
    State s;
    if (const auto f = floor_data()) {
        s.walls = f->walls;
        s.antennas = f->antennas;
    }
    s.classes = project_.wall_classes();
    s.cells = project_.cells();
    return s;
}

void FloorEditor::restore(const State& s) {
    // Classes and cells the state needs first, then the floor, then what
    // the state does not have (nothing uses it any more).
    const State now = capture();
    for (const building::WallClass& c : s.classes)
        if (std::find(now.classes.begin(), now.classes.end(), c) == now.classes.end()) project_.set_wall_class(c);
    for (const building::Cell& c : s.cells) {
        const auto it = std::find_if(now.cells.begin(), now.cells.end(), [&](const building::Cell& x) { return x.id == c.id; });
        if (it == now.cells.end()) project_.add_cell(c);
        else if (!(*it == c)) project_.update_cell(c);
    }
    if (auto f = floor_data(); f && (f->walls != s.walls || f->antennas != s.antennas)) {
        f->walls = s.walls;
        f->antennas = s.antennas;
        project_.update_floor(*f);
    }
    for (const building::Cell& c : now.cells)
        if (std::none_of(s.cells.begin(), s.cells.end(), [&](const building::Cell& x) { return x.id == c.id; }))
            project_.remove_cell(c.id);
    for (const building::WallClass& c : now.classes)
        if (std::none_of(s.classes.begin(), s.classes.end(), [&](const building::WallClass& x) { return x.id == c.id; }))
            project_.remove_wall_class(c.id);
}

template <class F>
Result FloorEditor::edit(const std::string& label, F&& change) {
    Step st;
    st.label = label;
    st.floor_id = floor_;
    st.before = capture();
    try {
        change();
    } catch (const std::exception& e) {
        // A refused change stores nothing (each project call is one
        // transaction); put back what an earlier call of it stored.
        try {
            restore(st.before);
        } catch (const std::exception&) {
        }
        load_forms();
        return Result{false, e.what()};
    }
    st.after = capture();
    if (st.after == st.before) return {};
    undo_.push_back(std::move(st));
    redo_.clear();
    undo_note_.clear();
    load_forms();
    return Result{true, label};
}

std::string FloorEditor::undo_label() const { return undo_.empty() ? std::string() : tr("Undo: ") + undo_.back().label; }
std::string FloorEditor::redo_label() const { return redo_.empty() ? std::string() : tr("Redo: ") + redo_.back().label; }

Result FloorEditor::step(std::vector<Step>& from, std::vector<Step>& to, bool undoing) {
    undo_note_.clear();
    if (from.empty()) return Result{false, undoing ? tr("Nothing to undo.") : tr("Nothing to redo.")};
    Step st = std::move(from.back());
    from.pop_back();
    const std::string keep = floor_;
    floor_ = st.floor_id;
    const State expect = undoing ? st.after : st.before;
    if (!project_.floor(floor_) || capture() != expect) {
        // Changed elsewhere since (or the floor is gone): this step and the
        // older ones no longer fit; dropped instead of undone.
        floor_ = keep;
        from.clear();
        undo_note_ = tr("The floor was changed elsewhere since; the older edits can no longer be undone.");
        return Result{false, undo_note_};
    }
    try {
        restore(undoing ? st.before : st.after);
    } catch (const std::exception& e) {
        floor_ = keep;
        from.clear();
        undo_note_ = std::string(tr("Could not ")) + (undoing ? "undo" : "redo") + ": " + e.what();
        return Result{false, undo_note_};
    }
    floor_ = keep;
    const std::string text = (undoing ? tr("Undone: ") : tr("Redone: ")) + st.label;
    to.push_back(std::move(st));
    // The selection may be gone.
    if (selection_.kind == Selection::Kind::Wall && !selected_wall()) select({});
    if (selection_.kind == Selection::Kind::Antenna && !selected_antenna()) select({});
    if (const auto w = selected_wall(); w && selection_.vertex && *selection_.vertex >= w->points.size())
        selection_.vertex.reset();
    load_forms();
    return Result{true, text};
}

Result FloorEditor::undo() { return step(undo_, redo_, true); }
Result FloorEditor::redo() { return step(redo_, undo_, false); }

// -- pointer ---------------------------------------------------------------------------

void FloorEditor::pointer_down(Point2 p, double tol) {
    const auto f = floor_data();
    if (!f) return;
    drag_from_.reset();
    dragged_ = false;
    drag_wall_.reset();
    drag_antenna_.reset();
    switch (tool_) {
    case EditTool::Pan: return;
    case EditTool::Wall: {
        // Snap to a corner of another wall (walls that meet).
        Point2 q = p;
        double best = tol;
        for (const building::Wall& w : f->walls)
            for (const Point2& c : w.points)
                if (const double d = building::distance_units(c, p); d <= best) {
                    best = d;
                    q = c;
                }
        if (draft_.empty() || !(draft_.back() == q)) draft_.push_back(q);
        return;
    }
    case EditTool::Antenna: {
        building::Antenna a;
        a.position = p;
        a.name = tr("Antenna ") + std::to_string(f->antennas.size() + 1);
        const auto cells = project_.cells();
        if (cells.size() == 1) a.cell = cells.front().id;
        std::string id;
        const Result r = edit(tr("place antenna"), [&] { id = project_.add_antenna(floor_, a); });
        if (r.ok && !id.empty()) {
            tool_ = EditTool::Select;
            select({Selection::Kind::Antenna, id, std::nullopt});
        }
        return;
    }
    case EditTool::Aim: {
        auto a = selected_antenna();
        if (!a) return;
        if (!building::aim_antenna(*a, p)) return;
        (void)edit(tr("aim antenna"), [&] { project_.update_antenna(*a); });
        tool_ = EditTool::Select;
        return;
    }
    case EditTool::Select: break;
    }
    // Select: an antenna first, then a corner of the selected wall, then a corner of any wall, then a wall.
    if (const auto i = building::nearest_antenna(*f, p, tol)) {
        select({Selection::Kind::Antenna, f->antennas[*i].id, std::nullopt});
        drag_from_ = p;
        return;
    }
    if (const auto w = selected_wall())
        if (const auto v = building::nearest_vertex(*w, p, tol)) {
            selection_.vertex = v;
            drag_from_ = p;
            return;
        }
    if (const auto hit = building::nearest_wall(*f, p, tol)) {
        const building::Wall& w = f->walls[hit->wall];
        select({Selection::Kind::Wall, w.id, building::nearest_vertex(w, p, tol)});
        drag_from_ = p;
        return;
    }
    select({});
}

void FloorEditor::pointer_move(Point2 p, double tol) {
    (void)tol;
    hover_ = p;
    if (!drag_from_ || tool_ != EditTool::Select) return;
    const Point2 by{p.x - drag_from_->x, p.y - drag_from_->y};
    if (!dragged_ && building::distance_units(*drag_from_, p) <= tol * 0.25) return;
    dragged_ = true;
    if (const auto a = selected_antenna()) {
        drag_antenna_ = Point2{a->position.x + by.x, a->position.y + by.y};
    } else if (auto w = selected_wall()) {
        if (selection_.vertex) building::move_vertex(*w, *selection_.vertex, p);
        else building::move_wall(*w, by);
        drag_wall_ = std::move(*w);
    }
}

void FloorEditor::pointer_up(Point2 p, double tol) {
    if (drag_from_ && tool_ == EditTool::Select) pointer_move(p, tol);
    const bool moved = dragged_;
    drag_from_.reset();
    dragged_ = false;
    if (moved && drag_antenna_) {
        const Point2 to = *drag_antenna_;
        (void)edit(tr("move antenna"), [&] { project_.move_antenna(selection_.id, floor_, to); });
    } else if (moved && drag_wall_) {
        const building::Wall w = *drag_wall_;
        (void)edit(selection_.vertex ? tr("move wall corner") : tr("move wall"), [&] { project_.update_wall(floor_, w); });
    }
    drag_wall_.reset();
    drag_antenna_.reset();
}

void FloorEditor::double_click(Point2 p, double tol) {
    if (tool_ == EditTool::Wall) {
        (void)finish_wall();
        return;
    }
    if (tool_ != EditTool::Select) return;
    auto w = selected_wall();
    const auto f = floor_data();
    if (!w || !f) return;
    const auto hit = building::nearest_wall(*f, p, tol);
    if (!hit || f->walls[hit->wall].id != w->id) return;
    if (building::nearest_vertex(*w, p, tol)) return;  // on a corner already
    if (!building::insert_vertex(*w, hit->segment + 1, hit->nearest)) return;
    if (edit(tr("add wall corner"), [&] { project_.update_wall(floor_, *w); }).ok)
        selection_.vertex = hit->segment + 1;
}

void FloorEditor::hover(std::optional<Point2> p) { hover_ = p; }

Result FloorEditor::finish_wall() {
    if (draft_.size() < 2) {
        draft_.clear();
        return Result{false, tr("A wall needs at least two corners.")};
    }
    building::Wall w;
    w.wall_class = draw_class_;
    w.points = draft_;
    std::string id;
    const Result r = edit(tr("draw wall"), [&] { id = project_.add_wall(floor_, w); });
    draft_.clear();
    if (r.ok) select({Selection::Kind::Wall, id, std::nullopt});
    return r;
}

Result FloorEditor::add_proposed_walls(const std::vector<std::pair<Point2, Point2>>& segments) {
    std::vector<building::Wall> walls;
    walls.reserve(segments.size());
    for (const auto& [a, b] : segments) {
        building::Wall w;
        w.wall_class = draw_class_;
        w.points = {a, b};
        walls.push_back(std::move(w));
    }
    std::vector<std::string> ids;
    const Result r = edit(tr("accept proposed walls"), [&] { ids = project_.add_walls(floor_, std::move(walls)); });
    if (r.ok && ids.size() == 1) select({Selection::Kind::Wall, ids.front(), std::nullopt});
    return r;
}

void FloorEditor::cancel() {
    if (!draft_.empty()) {
        draft_.clear();
        return;
    }
    drag_from_.reset();
    drag_wall_.reset();
    drag_antenna_.reset();
    if (tool_ == EditTool::Aim) tool_ = EditTool::Select;
    select({});
}

bool FloorEditor::remove_last_corner() {
    if (draft_.empty()) return false;
    draft_.pop_back();
    return true;
}

// -- commands --------------------------------------------------------------------------

Result FloorEditor::delete_selected() {
    if (auto w = selected_wall()) {
        if (selection_.vertex && w->points.size() > 2) {
            const std::size_t v = *selection_.vertex;
            building::remove_vertex(*w, v);
            const Result r = edit(tr("remove wall corner"), [&] { project_.update_wall(floor_, *w); });
            selection_.vertex.reset();
            return r;
        }
        const std::string id = w->id;
        const Result r = edit(tr("delete wall"), [&] { project_.remove_wall(floor_, id); });
        if (r.ok) select({});
        return r;
    }
    if (const auto a = selected_antenna()) {
        const std::string id = a->id;
        const Result r = edit(tr("delete antenna"), [&] { project_.remove_antenna(id); });
        if (r.ok) select({});
        return r;
    }
    return Result{false, tr("Select a wall, a corner or an antenna first.")};
}

std::string FloorEditor::split_reason() const {
    const auto w = selected_wall();
    if (!w) return tr("Select a wall first.");
    if (!selection_.vertex) return tr("Select an inner corner of the wall (double click on the wall adds one).");
    if (*selection_.vertex == 0 || *selection_.vertex + 1 >= w->points.size())
        return tr("The wall cannot be split at its end; select an inner corner.");
    return {};
}

Result FloorEditor::split_selected() {
    if (const std::string why = split_reason(); !why.empty()) return Result{false, why};
    const building::Wall w = *selected_wall();
    const auto parts = building::split_wall(w, *selection_.vertex);
    if (!parts) return Result{false, tr("The wall cannot be split there.")};
    const Result r = edit(tr("split wall"), [&] {
        project_.update_wall(floor_, parts->first);
        building::Wall second = parts->second;
        second.id.clear();
        project_.add_wall(floor_, second);
    });
    if (r.ok) select({Selection::Kind::Wall, w.id, std::nullopt});
    return r;
}

Result FloorEditor::apply_wall_form() {
    auto w = selected_wall();
    if (!w) return Result{false, tr("Select a wall first.")};
    const WallForm& f = wall_form_;
    if (f.own_value) {
        if (!finite(f.attenuation_db) || f.attenuation_db < 0.0 || f.attenuation_db > 100.0)
            return Result{false, tr("The attenuation must be between 0 and 100 dB.")};
        w->attenuation_db = f.attenuation_db;
        if (w->wall_class.empty()) w->wall_class = f.wall_class;
    } else {
        if (f.wall_class.empty()) return Result{false, tr("Choose a wall class, or give the wall its own value.")};
        w->attenuation_db.reset();
    }
    w->wall_class = f.wall_class;
    if (!f.wall_class.empty()) draw_class_ = f.wall_class;
    return edit(tr("wall properties"), [&] { project_.update_wall(floor_, *w); });
}

Result FloorEditor::apply_antenna_form() {
    auto a = selected_antenna();
    if (!a) return Result{false, tr("Select an antenna first.")};
    const AntennaForm& f = antenna_form_;
    if (!finite(f.height_m) || f.height_m < 0.0 || f.height_m > 100.0)
        return Result{false, tr("The height must be between 0 and 100 m.")};
    if (!finite(f.gain_dbi) || f.gain_dbi < -20.0 || f.gain_dbi > 30.0)
        return Result{false, tr("The gain must be between -20 and 30 dBi.")};
    if (f.eirp_known && (!finite(f.eirp_dbm) || f.eirp_dbm < -30.0 || f.eirp_dbm > 70.0))
        return Result{false, tr("The EIRP must be between -30 and 70 dBm.")};
    if (!finite(f.cable_loss_db) || f.cable_loss_db < 0.0 || f.cable_loss_db > 60.0)
        return Result{false, tr("The cable loss must be between 0 and 60 dB.")};
    a->name = f.name;
    a->model = f.model;
    a->type = f.outdoor ? building::AntennaType::OutdoorDonor : building::AntennaType::IndoorDas;
    a->height_m = f.height_m;
    a->gain_dbi = f.gain_dbi;
    if (f.directional) {
        if (!finite(f.beamwidth_deg) || f.beamwidth_deg <= 0.0 || f.beamwidth_deg > 360.0)
            return Result{false, tr("The beamwidth must be above 0 and at most 360 degrees.")};
        a->pattern = building::directional_pattern(f.azimuth_deg, f.beamwidth_deg, std::max(0.0, f.front_to_back_db));
    } else {
        a->pattern = building::AntennaPattern{};
    }
    if (f.outdoor && f.direction_deg) a->direction_deg = std::fmod(std::fmod(*f.direction_deg, 360.0) + 360.0, 360.0);
    else if (!f.outdoor) a->direction_deg.reset();
    a->eirp_dbm = f.eirp_known ? std::optional<double>(f.eirp_dbm) : std::nullopt;
    a->cable_loss_db = f.cable_loss_db;
    a->cell = f.cell;
    return edit(tr("antenna properties"), [&] { project_.update_antenna(*a); });
}

std::vector<CellChoice> FloorEditor::cells() const {
    std::vector<CellChoice> out;
    for (const building::Cell& c : project_.cells()) {
        std::string label = c.label.empty() ? c.id : c.label;
        if (c.carrier_hz > 0) label += " (" + fixed(static_cast<double>(c.carrier_hz) / 1e6, 4, tr(" MHz")) + ")";
        out.push_back({c.id, label});
    }
    return out;
}

Result FloorEditor::add_cell(const std::string& label, double carrier_mhz) {
    if (label.empty()) return Result{false, tr("The cell needs a name.")};
    if (!finite(carrier_mhz) || carrier_mhz < 0.0 || carrier_mhz > 6000.0)
        return Result{false, tr("The carrier must be between 0 (unknown) and 6000 MHz.")};
    building::Cell c;
    c.label = label;
    c.carrier_hz = static_cast<std::uint64_t>(std::llround(carrier_mhz * 1e6));
    std::string id;
    // The antenna dialog stays open: keep what the user typed in it.
    const AntennaForm typed = antenna_form_;
    const Result r = edit(tr("add cell"), [&] { id = project_.add_cell(c); });
    antenna_form_ = typed;
    if (r.ok) antenna_form_.cell = id;
    return r;
}

// -- materials -------------------------------------------------------------------------

std::vector<MaterialRow> FloorEditor::materials() const {
    const building::Building b = project_.building();
    std::set<std::string> defaults;
    for (const building::WallClass& c : building::default_wall_classes()) defaults.insert(c.id);
    std::vector<MaterialRow> out;
    for (const building::WallClass& c : b.wall_classes) {
        MaterialRow r;
        r.id = c.id;
        r.name = c.name.empty() ? c.id : c.name;
        for (const building::BandAttenuation& band : c.bands)
            r.bands += (r.bands.empty() ? "" : ", ") + fixed(band.frequency_mhz, 0, tr(" MHz: ")) +
                       fixed(band.attenuation_db, 1, " dB");
        r.walls = building::walls_of_class(b, c.id);
        r.default_class = defaults.count(c.id) != 0;
        out.push_back(std::move(r));
    }
    return out;
}

Result FloorEditor::edit_material(const std::string& id) {
    MaterialForm f;
    if (id.empty()) {
        f.is_new = true;
        f.bands = {{400.0, 10.0}, {900.0, 12.0}};
        material_form_ = f;
        return {};
    }
    for (const building::WallClass& c : project_.wall_classes())
        if (c.id == id) {
            f.id = c.id;
            f.name = c.name;
            f.bands = c.bands;
            f.reflection_loss_db = c.reflection_loss_db;
            material_form_ = f;
            return {};
        }
    return Result{false, tr("There is no wall class '") + id + "'."};
}

Result FloorEditor::apply_material() {
    MaterialForm& f = material_form_;
    if (f.id.empty()) return Result{false, tr("The class needs an id (e.g. \"glass_coated\").")};
    if (f.is_new)
        for (const building::WallClass& c : project_.wall_classes())
            if (c.id == f.id) return Result{false, tr("There is already a class '") + f.id + "'."};
    std::vector<building::BandAttenuation> bands = f.bands;
    std::sort(bands.begin(), bands.end(), [](const auto& x, const auto& y) { return x.frequency_mhz < y.frequency_mhz; });
    if (bands.empty()) return Result{false, tr("The class needs at least one frequency band.")};
    for (std::size_t i = 0; i < bands.size(); ++i) {
        if (!finite(bands[i].frequency_mhz) || bands[i].frequency_mhz <= 0.0)
            return Result{false, tr("Every band needs a frequency above 0 MHz.")};
        if (i > 0 && bands[i].frequency_mhz == bands[i - 1].frequency_mhz)
            return Result{false, tr("Two bands have the same frequency.")};
        if (!finite(bands[i].attenuation_db) || bands[i].attenuation_db < 0.0 || bands[i].attenuation_db > 100.0)
            return Result{false, tr("Every attenuation must be between 0 and 100 dB.")};
    }
    building::WallClass c{f.id, f.name.empty() ? f.id : f.name, bands, f.reflection_loss_db};
    const Result r = edit(f.is_new ? tr("add wall class") : tr("edit wall class"), [&] { project_.set_wall_class(c); });
    if (r.ok) f.is_new = false;
    return r;
}

Result FloorEditor::remove_material(const std::string& id) {
    const building::Building b = project_.building();
    if (const std::size_t n = building::walls_of_class(b, id); n > 0)
        return Result{false, std::to_string(n) + (n == 1 ? tr(" wall uses") : tr(" walls use")) + tr(" the class '") + id +
                                 tr("': give them another class first.")};
    return edit(tr("remove wall class"), [&] { project_.remove_wall_class(id); });
}

Result FloorEditor::reset_materials() {
    return edit(tr("reset wall classes"), [&] { project_.reset_wall_classes(); });
}

}  // namespace yantra::app
