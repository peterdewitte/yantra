// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cell database editor's model (cell_db_editor.h; DB-01, DB-02).

#include "yantra/app/cell_db_editor.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <sstream>

#include "yantra/core/celldb/csv.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool identifier(const std::string& s) {
    if (s.empty() || !(std::islower(static_cast<unsigned char>(s[0])) != 0)) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) != 0 || std::isdigit(static_cast<unsigned char>(c)) != 0 ||
               c == '_';
    });
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string part;
    std::istringstream in(s);
    while (std::getline(in, part, sep)) out.push_back(part);
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

std::string mhz_text(std::uint64_t hz) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f MHz", static_cast<double>(hz) / 1e6);
    return b;
}

Result parse_neighbours(const std::string& text, std::vector<celldb::NeighbourRef>& out) {
    out.clear();
    int line_no = 0;
    for (const std::string& raw : split(text, '\n')) {
        ++line_no;
        const std::string line = trim(raw);
        if (line.empty()) continue;
        celldb::NeighbourRef ref;
        for (const std::string& pair : split(line, '/')) {
            const auto eq = pair.find('=');
            const std::string name = eq == std::string::npos ? std::string() : trim(pair.substr(0, eq));
            const auto value = eq == std::string::npos ? std::nullopt : celldb::parse_value(trim(pair.substr(eq + 1)));
            if (!identifier(name) || !value)
                return Result{false, tr("Neighbour line ") + std::to_string(line_no) + " (\"" + line +
                                         "\") is not a list of name=value pairs separated by '/', e.g. "
                                         "la=200/frequency_hz=390200000."};
            ref.fields[name] = *value;
        }
        out.push_back(std::move(ref));
    }
    if (out.size() > celldb::kMaxNeighbours)
        return Result{false, tr("A cell has at most ") + std::to_string(celldb::kMaxNeighbours) + tr(" neighbours; ") +
                                 std::to_string(out.size()) + tr(" are listed.")};
    return Result{};
}

CellDbEditor::CellDbEditor(project::Project& project, TechnologySource technologies)
    : project_(project), technologies_(std::move(technologies)), dirty_(std::make_shared<std::atomic<bool>>(true)) {
    token_ = project_.subscribe([flag = dirty_](const project::Change& c) {
        if (c.kind == project::Change::Kind::Cells || c.kind == project::Change::Kind::Building) flag->store(true);
    });
}

CellDbEditor::~CellDbEditor() {
    try {
        project_.unsubscribe(token_);
    } catch (...) {
        // A closed project has no listeners left.
    }
}

std::vector<std::string> CellDbEditor::hex_fields() const {
    if (csv_.hex_la) return {"la"};
    return {};
}

void CellDbEditor::refresh() {
    if (loaded_ && !dirty_->exchange(false)) return;
    dirty_->store(false);
    loaded_ = true;
    db_ = project_.cell_database();
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const std::vector<std::string> hex = hex_fields();
    rows_.clear();
    for (const celldb::CellEntry& e : db_.entries()) {
        CellRowView r;
        r.id = e.cell.id;
        r.name = e.display_name();
        r.technology = e.cell.technology_id;
        r.site = e.site_name;
        r.sector = e.sector;
        if (e.cell.carrier_hz != 0) {
            r.carrier = mhz_text(e.cell.carrier_hz);
            if (const celldb::Technology* t = celldb::find_technology(techs, e.cell.technology_id))
                if (const auto ch = t->channel_of_frequency(e.cell.carrier_hz)) r.channel = std::to_string(*ch);
        }
        for (const auto& [name, value] : e.fields()) {
            if (name == celldb::kCarrierField || !celldb::is_identity_field(name)) continue;
            r.identity += (r.identity.empty() ? "" : " ") + name + "=" + celldb::value_text(value, contains(hex, name));
        }
        r.neighbours = e.neighbours.size();
        r.origin = celldb::origin_name(e.origin);
        r.colour = e.colour;
        rows_.push_back(std::move(r));
    }
    if (!selected_.empty() && db_.find(selected_) == nullptr) {
        selected_.clear();
        editing_ = false;
    }
}

const std::vector<CellRowView>& CellDbEditor::rows() {
    refresh();
    return rows_;
}

std::size_t CellDbEditor::size() { return rows().size(); }

std::vector<std::size_t> CellDbEditor::filtered(const std::string& filter) {
    const std::vector<CellRowView>& rs = rows();
    const std::string f = lower(trim(filter));
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < rs.size(); ++i) {
        const CellRowView& r = rs[i];
        const std::string all =
            lower(r.id + " " + r.name + " " + r.technology + " " + r.site + " " + r.sector + " " + r.carrier + " " +
                  r.channel + " " + r.identity + " " + r.origin);
        if (f.empty() || all.find(f) != std::string::npos) out.push_back(i);
    }
    return out;
}

std::vector<CellFieldForm> CellDbEditor::schema_fields(const std::string& technology) const {
    std::vector<CellFieldForm> out;
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const celldb::Technology* t = celldb::find_technology(techs, technology);
    if (t == nullptr) return out;
    for (const plugin::Field& f : t->cell_schema.fields) {
        if (!f.key && !f.verify) continue;
        CellFieldForm ff;
        ff.name = f.name;
        ff.label = f.label;
        ff.help = f.help;
        ff.unit = f.unit;
        ff.key = f.key;
        ff.verify = f.verify;
        out.push_back(std::move(ff));
    }
    return out;
}

CellForm CellDbEditor::form_of(const celldb::CellEntry& e) const {
    CellForm f;
    f.is_new = false;
    f.id = e.cell.id;
    f.label = e.cell.label;
    f.technology = e.cell.technology_id;
    f.schema = e.cell.schema_id;
    f.site_name = e.site_name;
    f.sector = e.sector;
    if (e.azimuth_deg) f.azimuth = celldb::value_text(*e.azimuth_deg);
    f.colour = e.colour;
    f.notes = e.notes;
    const std::vector<std::string> hex = hex_fields();
    f.fields = schema_fields(e.cell.technology_id);
    for (const auto& [name, value] : e.fields()) {
        auto it = std::find_if(f.fields.begin(), f.fields.end(), [&](const CellFieldForm& x) { return x.name == name; });
        if (it == f.fields.end()) {
            f.fields.push_back(CellFieldForm{name, "", "", "", "", false, false});
            it = std::prev(f.fields.end());
        }
        it->value = celldb::value_text(value, contains(hex, name));
    }
    for (const celldb::NeighbourRef& n : e.neighbours) f.neighbours += celldb::neighbour_text(n) + "\n";
    return f;
}

Result CellDbEditor::select(const std::string& id) {
    refresh();
    const celldb::CellEntry* e = db_.find(id);
    if (e == nullptr) return Result{false, tr("There is no cell ") + id + tr(" in the database.")};
    selected_ = id;
    form_ = form_of(*e);
    editing_ = true;
    return Result{};
}

Result CellDbEditor::new_entry(const std::string& technology) {
    refresh();
    form_ = CellForm{};
    form_.technology = technology;
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    if (const celldb::Technology* t = celldb::find_technology(techs, technology)) form_.schema = t->cell_schema.id;
    form_.fields = schema_fields(technology);
    if (form_.fields.empty()) form_.fields.push_back(CellFieldForm{celldb::kCarrierField, "", "", "", tr("Hz"), true, false});
    selected_.clear();
    editing_ = true;
    return Result{};
}

Result CellDbEditor::add_field() {
    const std::string name = trim(form_.new_field);
    if (!identifier(name))
        return Result{false, tr("A field name is lower-case letters, digits and '_', starting with a letter (e.g. srt_db).")};
    for (const CellFieldForm& f : form_.fields)
        if (f.name == name) return Result{false, tr("The form already has the field ") + name + "."};
    form_.fields.push_back(CellFieldForm{name, "", "", "", "", false, false});
    form_.new_field.clear();
    return Result{};
}

void CellDbEditor::cancel() {
    editing_ = false;
    if (!selected_.empty()) (void)select(selected_);
    editing_ = false;
}

void CellDbEditor::store(const celldb::CellDatabase& db) {
    project_.set_cell_database(db);
    dirty_->store(true);
}

Result CellDbEditor::apply() {
    refresh();
    celldb::CellEntry e;
    if (!form_.is_new) {
        const celldb::CellEntry* old = db_.find(form_.id);
        if (old == nullptr) return Result{false, tr("The cell ") + form_.id + tr(" is no longer in the database.")};
        e = *old;  // keeps what the form does not show (origin, plugin version)
        e.parameters.clear();
        e.cell.identity.clear();
        e.cell.carrier_hz = 0;
    } else {
        e.cell.id = trim(form_.id);
    }
    e.cell.label = trim(form_.label);
    e.cell.technology_id = trim(form_.technology);
    e.cell.schema_id = trim(form_.schema);
    e.site_name = trim(form_.site_name);
    e.sector = trim(form_.sector);
    e.colour = trim(form_.colour);
    e.notes = form_.notes;
    const std::string az = trim(form_.azimuth);
    e.azimuth_deg.reset();
    if (!az.empty()) {
        const auto v = celldb::parse_value(az);
        if (!v || std::holds_alternative<std::string>(*v))
            return Result{false, tr("The azimuth is a number of degrees, 0 to less than 360.")};
        e.azimuth_deg = std::holds_alternative<double>(*v) ? std::get<double>(*v)
                                                           : static_cast<double>(std::get<std::int64_t>(*v));
    }
    for (const CellFieldForm& f : form_.fields) {
        const std::string text = trim(f.value);
        if (text.empty()) continue;
        const auto v = celldb::parse_value(text);
        if (!v) continue;
        try {
            e.set_field(f.name, *v);
        } catch (const std::exception& ex) {
            return Result{false, f.name + ": " + ex.what()};
        }
    }
    if (const Result r = parse_neighbours(form_.neighbours, e.neighbours); !r.ok) return r;
    if (const std::string problem = e.validate(); !problem.empty()) return Result{false, tr("Not stored: ") + problem};

    celldb::CellDatabase db = db_;
    std::string id;
    try {
        if (form_.is_new) {
            id = db.add(e);
        } else {
            db.update(e);
            id = e.cell.id;
        }
        store(db);
    } catch (const std::exception& ex) {
        return Result{false, std::string(tr("Not stored: ")) + ex.what()};
    }
    const bool was_new = form_.is_new;
    (void)select(id);
    return Result{true, (was_new ? tr("Added cell ") : tr("Stored cell ")) + id};
}

Result CellDbEditor::remove(const std::string& id) {
    refresh();
    celldb::CellDatabase db = db_;
    if (!db.remove(id)) return Result{false, tr("There is no cell ") + id + tr(" in the database.")};
    try {
        store(db);
    } catch (const std::exception& ex) {
        return Result{false, tr("Cannot remove cell ") + id + ": " + ex.what()};
    }
    if (selected_ == id) {
        selected_.clear();
        editing_ = false;
    }
    return Result{true, tr("Removed cell ") + id};
}

fs::path CellDbEditor::default_csv_file() const {
    const fs::path p = project_.path();
    return p.parent_path() / (p.stem().string() + "_cells.csv");
}

Result CellDbEditor::import_csv() {
    problems_.clear();
    if (csv_.file.empty()) return Result{false, "Choose the CSV file to import first."};
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    celldb::CsvOptions o;
    o.technologies = &techs;
    celldb::CellDatabase read;
    try {
        read = celldb::read_csv_file(csv_.file.string(), o, &problems_);
    } catch (const std::exception& ex) {
        return Result{false, "Cannot import " + csv_.file.string() + ": " + ex.what()};
    }
    refresh();
    celldb::CellDatabase db = csv_.replace ? celldb::CellDatabase{} : db_;
    if (csv_.replace)
        for (const auto& [tech, fields] : db_.all_match_fields()) db.set_match_fields(tech, fields);
    try {
        (void)db.merge(read.entries());
        for (const auto& [tech, fields] : read.all_match_fields()) db.set_match_fields(tech, fields);
        store(db);
    } catch (const std::exception& ex) {
        return Result{false, "Cannot import " + csv_.file.string() + ": " + ex.what()};
    }
    std::string msg = "Imported " + std::to_string(read.entries().size()) + " cells from " + csv_.file.filename().string();
    if (!problems_.empty()) msg += " (" + std::to_string(problems_.size()) + tr(" rows skipped, see the list)");
    return Result{true, msg};
}

Result CellDbEditor::export_csv() {
    problems_.clear();
    if (csv_.file.empty()) return Result{false, "Choose the CSV file to write first."};
    refresh();
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    celldb::CsvOptions o;
    o.technologies = &techs;
    o.hex_fields = hex_fields();
    o.separator = csv_.semicolon ? ';' : ',';
    try {
        celldb::write_csv_file(db_, csv_.file.string(), o);
    } catch (const std::exception& ex) {
        return Result{false, "Cannot write " + csv_.file.string() + ": " + ex.what()};
    }
    return Result{true, "Wrote " + std::to_string(db_.entries().size()) + " cells to " + csv_.file.string()};
}

std::vector<std::string> CellDbEditor::technologies() const {
    std::vector<std::string> out;
    if (technologies_)
        for (const celldb::Technology& t : technologies_()) out.push_back(t.id);
    const celldb::CellDatabase db = project_.cell_database();
    for (const celldb::CellEntry& e : db.entries())
        if (!e.cell.technology_id.empty() && !contains(out, e.cell.technology_id)) out.push_back(e.cell.technology_id);
    return out;
}

std::string CellDbEditor::match_fields_text(const std::string& technology) const {
    std::vector<std::string> fields = project_.cell_database().match_fields(technology);
    if (fields.empty() && technologies_) {
        const celldb::Technologies techs = technologies_();
        if (const celldb::Technology* t = celldb::find_technology(techs, technology)) fields = t->key_fields();
    }
    std::string s;
    for (const std::string& f : fields) s += (s.empty() ? "" : ", ") + f;
    return s;
}

Result CellDbEditor::set_match_fields(const std::string& technology, const std::string& list) {
    std::vector<std::string> fields;
    for (const std::string& part : split(list, ',')) {
        const std::string f = trim(part);
        if (f.empty()) continue;
        if (!identifier(f)) return Result{false, "\"" + f + "\" is not a field name."};
        if (!contains(fields, f)) fields.push_back(f);
    }
    refresh();
    celldb::CellDatabase db = db_;
    db.set_match_fields(technology, fields);
    try {
        store(db);
    } catch (const std::exception& ex) {
        return Result{false, std::string(tr("Not stored: ")) + ex.what()};
    }
    return Result{true, fields.empty() ? technology + tr(" cells are paired by their key fields")
                                       : technology + tr(" cells are paired by ") + match_fields_text(technology)};
}

}  // namespace yantra::app
