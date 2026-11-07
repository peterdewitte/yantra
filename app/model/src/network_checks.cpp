// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app network checks (network_checks.h; DB-03, DB-04, DB-05, DB-08).

#include "yantra/app/network_checks.h"
#include "yantra/app/i18n.h"

#include <algorithm>
#include <ctime>
#include <exception>

#include "yantra/core/celldb/reselection.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::string now_utc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return b;
}

constexpr double kDefaultLevelDbm = -80.0;

}  // namespace

std::uint32_t state_rgb(const std::string& state) {
    if (state == "match") return kMatchRgb;
    if (state == "differs" || state == "ambiguous") return kDiffersRgb;
    if (state == "new" || state == "not_in_database") return kNewRgb;
    return kMissingRgb;  // missing, not_seen
}

NetworkChecks::NetworkChecks(project::Project& project, TechnologySource technologies, ObservationSource observed)
    : project_(project), technologies_(std::move(technologies)), observed_(std::move(observed)) {}

VerificationView NetworkChecks::verification(bool matches) const {
    VerificationView v;
    const celldb::CellDatabase db = project_.cell_database();
    const std::vector<celldb::ObservedCell> received = observed_ ? observed_() : std::vector<celldb::ObservedCell>{};
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    v.received = received.size();
    if (received.empty()) {
        v.note = tr("No cells received yet: start the radio (Survey page) or replay a recording; the received broadcast "
                 "parameters and neighbour lists are compared with the cell database as they come.");
        return v;
    }
    const celldb::Verification ver = celldb::verify(db, received, techs);
    for (const celldb::CellCheck& c : ver.cells) {
        if (c.status == celldb::CellCheck::Status::Paired) ++v.paired;
        if (c.status == celldb::CellCheck::Status::NotInDatabase) ++v.not_in_database;
        if (c.status == celldb::CellCheck::Status::Ambiguous) ++v.ambiguous;
    }
    v.differences = ver.differences();
    v.not_seen = ver.not_seen.size();
    // The non-match lines as the difference log writes them, then (on
    // request) the matching fields of the paired cells.
    celldb::LogOptions lo;
    lo.hex_fields = {"la"};
    lo.include_not_seen = true;
    for (const celldb::LogLine& l : celldb::log_lines(ver, db, received, lo))
        v.rows.push_back(VerificationRow{l.cell, l.item, l.state, l.database, l.received, state_rgb(l.state)});
    if (matches) {
        for (const celldb::CellCheck& c : ver.cells) {
            if (c.status != celldb::CellCheck::Status::Paired) continue;
            const celldb::CellEntry* e = db.find(c.entry);
            const std::string name = e != nullptr ? e->display_name() : c.entry;
            for (const celldb::FieldCheck& f : c.fields)
                if (f.state == celldb::State::Match)
                    v.rows.push_back(VerificationRow{name, f.field, "match",
                                                     f.database ? celldb::value_text(*f.database, f.field == "la") : "",
                                                     f.received ? celldb::value_text(*f.received, f.field == "la") : "",
                                                     kMatchRgb});
            for (const celldb::NeighbourCheck& n : c.neighbours)
                if (n.state == celldb::State::Match)
                    v.rows.push_back(VerificationRow{name, "neighbour", "match",
                                                     n.database ? celldb::neighbour_text(*n.database, {"la"}) : "",
                                                     n.received ? celldb::neighbour_text(*n.received, {"la"}) : "",
                                                     kMatchRgb});
        }
    }
    if (db.entries().empty()) v.note = tr("The cell database is empty: every received cell is new (Learn adds them).");
    return v;
}

fs::path NetworkChecks::default_log_file() const {
    const fs::path p = project_.path();
    return p.parent_path() / (p.stem().string() + "_differences.csv");
}

Result NetworkChecks::write_log(const fs::path& file, bool not_seen) {
    if (file.empty()) return Result{false, tr("Choose the log file first.")};
    const celldb::CellDatabase db = project_.cell_database();
    const std::vector<celldb::ObservedCell> received = observed_ ? observed_() : std::vector<celldb::ObservedCell>{};
    if (received.empty()) return Result{false, tr("No cells received yet: nothing to compare.")};
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const celldb::Verification ver = celldb::verify(db, received, techs);
    celldb::LogOptions lo;
    lo.time_utc = now_utc();
    lo.hex_fields = {"la"};
    lo.include_not_seen = not_seen;
    try {
        celldb::DifferenceLog log(file);
        const std::size_t n = log.append(celldb::log_lines(ver, db, received, lo));
        return Result{true, tr("Wrote ") + std::to_string(n) + tr(" new difference lines to ") + file.string()};
    } catch (const std::exception& e) {
        return Result{false, tr("Cannot write the difference log: ") + std::string(e.what())};
    }
}

Result NetworkChecks::learn(const celldb::LearnOptions& options) {
    const std::vector<celldb::ObservedCell> received = observed_ ? observed_() : std::vector<celldb::ObservedCell>{};
    if (received.empty()) return Result{false, tr("No cells received yet: start the radio or a replay first.")};
    celldb::CellDatabase db = project_.cell_database();
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const celldb::LearnResult r = celldb::learn(db, received, techs, options);
    try {
        if (!r.added.empty() || !r.extended.empty()) project_.set_cell_database(db);
    } catch (const std::exception& e) {
        return Result{false, std::string(tr("Not stored: ")) + e.what()};
    }
    std::string msg = tr("Learned ") + std::to_string(r.added.size()) + tr(" new cells, extended ") +
                      std::to_string(r.extended.size());
    if (r.ambiguous > 0) msg += "; " + std::to_string(r.ambiguous) + tr(" received cells pair with several entries");
    return Result{true, msg};
}

std::vector<CrossCheckRow> NetworkChecks::cross_check() const {
    const celldb::CellDatabase db = project_.cell_database();
    const std::vector<celldb::ObservedCell> received = observed_ ? observed_() : std::vector<celldb::ObservedCell>{};
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    std::vector<CrossCheckRow> out;
    for (const celldb::CrossCheckFinding& f : celldb::cross_check(celldb::combined_views(db, received, techs))) {
        const std::string kind = celldb::finding_name(f.kind);
        out.push_back(CrossCheckRow{kind, f.technology, f.cell, f.other, f.text,
                                    kind == "not_mutual" ? kDiffersRgb : kMissingRgb});
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> NetworkChecks::serving_choices() const {
    std::vector<std::pair<std::string, std::string>> out;
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const celldb::CellDatabase db = project_.cell_database();
    for (const celldb::CellEntry& e : db.entries()) {
        const celldb::Technology* t = celldb::find_technology(techs, e.cell.technology_id);
        if (t != nullptr && t->api != nullptr && t->api->reselect != nullptr)
            out.emplace_back(e.cell.id, e.display_name());
    }
    return out;
}

Result NetworkChecks::choose_serving(const std::string& id) {
    const celldb::CellDatabase db = project_.cell_database();
    if (db.find(id) == nullptr) return Result{false, tr("There is no cell ") + id + tr(" in the database.")};
    form_.serving = id;
    for (const std::string& n : celldb::neighbour_entries(db, id))
        if (!form_.levels.count(n)) form_.levels[n] = kDefaultLevelDbm;
    if (!form_.levels.count(id)) form_.levels[id] = kDefaultLevelDbm;
    return Result{};
}

Result NetworkChecks::use_received_levels() {
    const std::vector<celldb::ObservedCell> received = observed_ ? observed_() : std::vector<celldb::ObservedCell>{};
    if (received.empty()) return Result{false, tr("No cells received yet: start the radio or a replay first.")};
    const celldb::CellDatabase db = project_.cell_database();
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const celldb::Verification ver = celldb::verify(db, received, techs);
    std::size_t n = 0;
    for (const celldb::CellCheck& c : ver.cells) {
        if (c.status != celldb::CellCheck::Status::Paired || c.observed >= received.size()) continue;
        form_.levels[c.entry] = received[c.observed].level;
        ++n;
    }
    return Result{n > 0, n > 0 ? tr("Took the received levels of ") + std::to_string(n) + " cells"
                               : tr("No received cell pairs with a database entry.")};
}

ReselectionView NetworkChecks::reselection() const {
    ReselectionView v;
    const celldb::CellDatabase db = project_.cell_database();
    const celldb::CellEntry* serving = db.find(form_.serving);
    if (serving == nullptr) {
        v.error = tr("Choose the serving cell.");
        return v;
    }
    const celldb::Technologies techs = technologies_ ? technologies_() : celldb::Technologies{};
    const std::vector<std::string> neighbours = celldb::neighbour_entries(db, form_.serving);
    auto level = [&](const std::string& id) {
        const auto it = form_.levels.find(id);
        return it == form_.levels.end() ? kDefaultLevelDbm : it->second;
    };
    std::map<std::string, double> levels;
    levels[form_.serving] = level(form_.serving);
    for (const std::string& n : neighbours) levels[n] = level(n);
    const celldb::ReselectionOutcome o = celldb::simulate(db, techs, form_.serving, levels, form_.ms_power_dbm);
    v.ok = o.ok;
    v.error = o.error;
    v.state = o.state;
    v.reason = o.reason;
    if (const celldb::Technology* t = celldb::find_technology(techs, serving->cell.technology_id))
        for (const plugin::EnumItem& s : t->serving_states)
            if (s.value == o.serving_state) v.state_label = s.label;
    if (o.chosen && *o.chosen < neighbours.size()) v.chosen = neighbours[*o.chosen];
    v.rows.push_back(ReselectionRow{form_.serving, serving->display_name(), levels[form_.serving], o.serving_c1, true,
                                    false});
    for (std::size_t i = 0; i < neighbours.size(); ++i) {
        const celldb::CellEntry* e = db.find(neighbours[i]);
        v.rows.push_back(ReselectionRow{neighbours[i], e != nullptr ? e->display_name() : neighbours[i],
                                        levels[neighbours[i]],
                                        i < o.neighbour_c1.size() ? o.neighbour_c1[i] : std::nullopt, false,
                                        neighbours[i] == v.chosen});
    }
    return v;
}

}  // namespace yantra::app
