// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cell database against the radios' scenarios (DB-03/04/05/08): the
// yantra-gen record files of floor_change and failed_site replayed through
// the pipeline and the TETRA plugin, the cells and neighbour lists of the
// survey verified against databases that match or differ from the
// scenario's cells (STEM.cells.csv), cross-checked, learned, and the
// reselection simulator walked with the replayed levels against the
// scenario's ground truth (STEM.truth.csv). Cases skip when the radio
// tools are not built.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <yantra/core/celldb/csv.h>
#include <yantra/core/celldb/database.h>
#include <yantra/core/celldb/network.h>
#include <yantra/core/celldb/reselection.h>
#include <yantra/core/celldb/technology.h>
#include <yantra/core/celldb/verify.h>

#include "e2e_helpers.h"
#include "measure_rig.h"

using namespace mh;
namespace db = yantra::celldb;

namespace {

db::Technologies technologies(FileRun& run) {
    auto t = db::Technology::from_plugin(*run.tetra.info, run.tetra.loader.descriptor("yantra.tetra"),
                                         run.tetra.loader.instance("yantra.tetra"));
    REQUIRE(t);
    return {*t};
}

// The scenario's cells as a cell database: ids are the scenario's cell
// ids, neighbours named by frequency and LA (what a planner's sheet has).
db::CellDatabase database_of(const Truth& truth, std::int64_t rxlev_min = -115, std::int64_t txmax = 35) {
    db::CellDatabase out;
    for (const auto& [id, c] : truth.cells) {
        db::CellEntry e;
        e.cell.id = id;
        e.cell.technology_id = "tetra";
        e.cell.schema_id = "tetra_cell";
        e.set_field("frequency_hz", static_cast<std::int64_t>(c.frequency_hz));
        e.set_field("mcc", static_cast<std::int64_t>(c.mcc));
        e.set_field("mnc", static_cast<std::int64_t>(c.mnc));
        e.set_field("la", static_cast<std::int64_t>(c.la));
        e.set_field("colour_code", static_cast<std::int64_t>(c.colour_code));
        e.set_field("rxlev_access_min_dbm", rxlev_min);
        e.set_field("ms_txpwr_max_cell_dbm", txmax);
        for (const std::string& n : c.neighbours) {
            const Truth::Cell& o = truth.cells.at(n);
            db::NeighbourRef r;
            r.fields["frequency_hz"] = static_cast<std::int64_t>(o.frequency_hz);
            r.fields["la"] = static_cast<std::int64_t>(o.la);
            e.neighbours.push_back(r);
        }
        out.add(e);
    }
    return out;
}

// The survey of a replayed record file.
struct Survey {
    FileRun run;
    db::Technologies techs;
    std::vector<db::ObservedCell> cells;
    explicit Survey(const std::string& scenario) {
        run.run(load(scenario + ".records.jsonl"));
        techs = technologies(run);
        db::Observations obs(techs);
        for (const ym::MappedRecord& m : run.records()) obs.add(m);
        cells = obs.cells();
    }
    const db::ObservedCell* cell(std::uint64_t hz) const {
        for (const db::ObservedCell& c : cells)
            if (std::get<std::int64_t>(c.fields.at("frequency_hz")) == static_cast<std::int64_t>(hz)) return &c;
        return nullptr;
    }
};

std::string log_text(const db::Verification& v, const db::CellDatabase& d, const std::vector<db::ObservedCell>& cells) {
    db::LogOptions o;
    o.include_not_seen = true;
    std::string s;
    for (const db::LogLine& l : db::log_lines(v, d, cells, o)) s += db::log_csv(l);
    return s;
}

std::vector<std::string> findings(const std::vector<db::CellView>& views) {
    std::vector<std::string> out;
    auto name = [&](const std::string& id) {
        for (const db::CellView& v : views)
            if (v.id == id) return v.in_database ? v.id : "f" + std::to_string(std::get<std::int64_t>(v.fields.at("frequency_hz")));
        return std::string();
    };
    for (const db::CrossCheckFinding& f : db::cross_check(views))
        out.push_back(name(f.cell) + ">" + name(f.other) + ":" + db::finding_name(f.kind));
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("DB-04 floor_change: the survey verified against the scenario's cells matches; wrong colour code, missing and unknown cells and a short list are found") {
    if (!have_records("")) return;
    const Truth truth("floor_change");
    Survey s("floor_change");
    // Every cell on air was received with its identity; D0 and D1 served.
    REQUIRE(s.cells.size() == 3);
    const db::ObservedCell* d0 = s.cell(truth.cells.at("D0").frequency_hz);
    const db::ObservedCell* d1 = s.cell(truth.cells.at("D1").frequency_hz);
    const db::ObservedCell* m1 = s.cell(truth.cells.at("M1").frequency_hz);
    REQUIRE((d0 && d1 && m1));
    CHECK(d0->serving);
    CHECK(d1->serving);
    CHECK_FALSE(m1->serving);
    REQUIRE(d0->neighbours);
    CHECK(d0->neighbours->size() == truth.cells.at("D0").neighbours.size());
    CHECK_FALSE(m1->neighbours);  // never served: its list is not broadcast to us

    SUBCASE("the database matches the scenario") {
        const db::CellDatabase d = database_of(truth);
        const db::Verification v = db::verify(d, s.cells, s.techs);
        INFO(log_text(v, d, s.cells));
        CHECK(v.differences() == 0);
        CHECK(v.not_seen.empty());
        for (const db::CellCheck& c : v.cells) {
            CHECK(c.status == db::CellCheck::Status::Paired);
            CHECK(c.all_match());
            CHECK_FALSE(c.fields.empty());
        }
        // The broadcast parameters received match the stored dBm (by code).
        CHECK(v.cells[0].fields.size() >= 7);
    }
    SUBCASE("wrong colour code, missing cell, unknown cell, short neighbour list") {
        db::CellDatabase d = database_of(truth);
        db::CellEntry e1 = *d.find("D1");
        e1.set_field("colour_code", std::int64_t{7});
        d.update(e1);
        d.remove("M1");
        db::CellEntry x = *d.find("D0");
        x.cell.id = "X9";
        x.set_field("frequency_hz", std::int64_t{390'500'000});
        x.neighbours.clear();
        d.add(x);
        db::CellEntry e0 = *d.find("D0");
        e0.neighbours.pop_back();  // M1 left out of D0's stored list
        d.update(e0);
        const db::Verification v = db::verify(d, s.cells, s.techs);
        const std::string log = log_text(v, d, s.cells);
        INFO(log);
        CHECK(log.find(",colour_code,differs,7," + std::to_string(truth.cells.at("D1").colour_code)) != std::string::npos);
        CHECK(log.find(",cell,not_in_database,") != std::string::npos);                  // M1
        CHECK(log.find("X9,X9,cell,not_seen") != std::string::npos);                     // X9
        CHECK(log.find(",neighbour,new,,frequency_hz=" + std::to_string(truth.cells.at("M1").frequency_hz)) !=
              std::string::npos);
        CHECK(v.not_seen == std::vector<std::string>{"X9"});
        CHECK(v.differences() == 2);
    }
}

TEST_CASE("DB-05 floor_change: M1 does not list D1; the survey alone cannot tell, with the database it can") {
    if (!have_records("")) return;
    const Truth truth("floor_change");
    Survey s("floor_change");
    // A survey knows the lists of the cells that served: D0 and D1 list
    // each other (mutual) and M1, whose list is not known.
    CHECK(findings(db::observed_views(s.cells)) ==
          std::vector<std::string>{"f390000000>f390300000:list_unknown", "f390100000>f390300000:list_unknown"});
    // With the database (M1's list from the scenario: D0 only), D1 -> M1
    // is not mutual; D0 -> M1 is.
    const db::CellDatabase d = database_of(truth);
    CHECK(findings(db::combined_views(d, s.cells, s.techs)) == std::vector<std::string>{"D1>M1:not_mutual"});
    CHECK(findings(db::database_views(d)) == std::vector<std::string>{"D1>M1:not_mutual"});
}

TEST_CASE("PLG-05 network_border: B1's neighbour B2 of network 901-2 is named by its own MCC and MNC (link rule, plugin API 1.2, RAD-22)") {
    if (!have_records("")) return;
    const Truth truth("network_border");
    Truth::Cell b1 = truth.cells.at("B1");
    Truth::Cell b2 = truth.cells.at("B2");
    Survey s("network_border");
    const db::ObservedCell* home = s.cell(b1.frequency_hz);
    REQUIRE(home);
    // Each cell's network as it broadcasts it (its tetra_cell records; the
    // generator's cells.csv lists one network for every cell).
    const db::ObservedCell* other = s.cell(b2.frequency_hz);
    REQUIRE(other);
    b1.mcc = static_cast<std::uint64_t>(std::get<std::int64_t>(home->fields.at("mcc")));
    b1.mnc = static_cast<std::uint64_t>(std::get<std::int64_t>(home->fields.at("mnc")));
    b2.mcc = static_cast<std::uint64_t>(std::get<std::int64_t>(other->fields.at("mcc")));
    b2.mnc = static_cast<std::uint64_t>(std::get<std::int64_t>(other->fields.at("mnc")));
    REQUIRE(b1.mnc != b2.mnc);  // two networks (901-1 and 901-2)
    REQUIRE(home->neighbours);
    const db::NeighbourRef* to_b2 = nullptr;
    for (const db::NeighbourRef& r : *home->neighbours)
        if (r.fields.count("frequency_hz") != 0 &&
            std::get<std::int64_t>(r.fields.at("frequency_hz")) == static_cast<std::int64_t>(b2.frequency_hz))
            to_b2 = &r;
    REQUIRE(to_b2);
    // The entry's own MCC and MNC (not inherited from B1's network).
    CHECK(std::get<std::int64_t>(to_b2->fields.at("mcc")) == static_cast<std::int64_t>(b2.mcc));
    CHECK(std::get<std::int64_t>(to_b2->fields.at("mnc")) == static_cast<std::int64_t>(b2.mnc));
    CHECK(to_b2->inherited.count("mcc") == 0);
    CHECK(to_b2->inherited.count("mnc") == 0);

    // In a database with a cell of B1's network on B2's carrier: the
    // given network refuses it, B2 matches (with the frequency link alone
    // and the network inherited, both would match).
    db::CellDatabase d;
    for (const Truth::Cell& c : {b1, b2}) {
        const std::string& id = c.id;
        db::CellEntry e;
        e.cell.id = id;
        e.cell.technology_id = "tetra";
        e.cell.schema_id = "tetra_cell";
        e.set_field("frequency_hz", static_cast<std::int64_t>(c.frequency_hz));
        e.set_field("mcc", static_cast<std::int64_t>(c.mcc));
        e.set_field("mnc", static_cast<std::int64_t>(c.mnc));
        e.set_field("la", static_cast<std::int64_t>(c.la));
        d.add(e);
    }
    db::CellEntry decoy;
    decoy.cell.id = "X";
    decoy.cell.technology_id = "tetra";
    decoy.cell.schema_id = "tetra_cell";
    decoy.set_field("frequency_hz", static_cast<std::int64_t>(b2.frequency_hz));
    decoy.set_field("mcc", static_cast<std::int64_t>(b1.mcc));
    decoy.set_field("mnc", static_cast<std::int64_t>(b1.mnc));
    decoy.set_field("la", static_cast<std::int64_t>(b2.la));
    d.add(decoy);
    CHECK(db::match_score(*to_b2, d.find("B2")->fields()));
    CHECK_FALSE(db::match_score(*to_b2, d.find("X")->fields()));
    db::NeighbourRef old_rule = *to_b2;  // as plugin API 1.1 resolved it
    old_rule.fields["mcc"] = static_cast<std::int64_t>(b1.mcc);
    old_rule.fields["mnc"] = static_cast<std::int64_t>(b1.mnc);
    old_rule.inherited.insert("mcc");
    old_rule.inherited.insert("mnc");
    old_rule.fields.erase("la");
    CHECK(db::match_score(old_rule, d.find("X")->fields()));
}

TEST_CASE("DB-05 failed_site: N1 lists N3, which is never on air: not seen; N2 is seen but its list is not") {
    if (!have_records("")) return;
    const Truth truth("failed_site");
    Survey s("failed_site");
    CHECK_FALSE(truth.cells.at("N3").on_air);
    CHECK(s.cell(truth.cells.at("N3").frequency_hz) == nullptr);
    CHECK(s.cell(truth.cells.at("N2").frequency_hz) != nullptr);
    const std::string n1 = "f" + std::to_string(truth.cells.at("N1").frequency_hz);
    const std::string n2 = "f" + std::to_string(truth.cells.at("N2").frequency_hz);
    CHECK(findings(db::observed_views(s.cells)) == std::vector<std::string>{n1 + ">:not_seen", n1 + ">" + n2 + ":list_unknown"});
}

TEST_CASE("DB-03 floor_change: auto-learn builds a database the same survey then verifies without differences; its CSV round-trips (TST-33)") {
    if (!have_records("")) return;
    const Truth truth("floor_change");
    Survey s("floor_change");
    db::CellDatabase d;
    const db::LearnResult r = db::learn(d, s.cells, s.techs);
    CHECK(r.added.size() == 3);
    for (const db::CellEntry& e : d.entries()) {
        CHECK(e.origin == db::Origin::Learned);
        CHECK(e.cell.technology_id == "tetra");
        CHECK(truth.by_frequency.count(e.cell.carrier_hz) == 1);
        CHECK(e.cell.identity.at("la") == static_cast<std::int64_t>(truth.cells.at(truth.by_frequency.at(e.cell.carrier_hz)).la));
    }
    const db::Verification v = db::verify(d, s.cells, s.techs);
    CHECK(v.differences() == 0);
    CHECK(v.not_seen.empty());

    db::CsvOptions o;
    o.hex_fields = {"la"};
    o.technologies = &s.techs;
    const std::string csv = db::export_csv(d, o);
    const db::CsvImport imp = db::import_csv(csv, o);
    CHECK(imp.problems.empty());
    db::CellDatabase back;
    back.merge(imp.entries);
    // Inherited neighbour fields are not written: the round trip keeps the
    // named ones and verification still matches.
    CHECK(back.entries().size() == d.entries().size());
    CHECK(db::export_csv(back, o) == csv);
    CHECK(db::verify(back, s.cells, s.techs).differences() == 0);
}

namespace {

// The TETRA rule by hand (docs/plugins/TETRA.md), for the expected states.
struct Hand {
    double rxlev_min, srt, srh, frt, frh;
    double c1(double level) const { return level - rxlev_min; }  // MS_TXPWR_MAX = MS power
    std::string state(double serving, const std::vector<double>& neighbours, bool* reselect) const {
        const double c = c1(serving);
        double best = 0.0;
        bool any = false;
        for (const double n : neighbours)
            if (c1(n) > 0.0 && (!any || c1(n) > best)) {
                best = c1(n);
                any = true;
            }
        *reselect = false;
        if (c < 0.0) {
            *reselect = any;
            return "radio_link_failure";
        }
        if (!any) return "exclusive";
        if (c >= best) return "superior";
        if (c < frt) {
            *reselect = best > c + frh;
            return "relinquishable";
        }
        if (c < srt) {
            *reselect = best > c + srh;
            return "improvable";
        }
        return "usable";
    }
};

db::CellDatabase with_thresholds(db::CellDatabase d, std::int64_t rxlev, std::int64_t srt, std::int64_t srh,
                                 std::int64_t frt, std::int64_t frh) {
    for (db::CellEntry e : d.entries()) {
        e.set_field("rxlev_access_min_dbm", rxlev);
        e.set_field("ms_txpwr_max_cell_dbm", std::int64_t{30});
        e.set_field("srt_db", srt);
        e.set_field("srh_db", srh);
        e.set_field("frt_db", frt);
        e.set_field("frh_db", frh);
        d.update(e);
    }
    return d;
}

}  // namespace

TEST_CASE("DB-08 floor_change: the simulator walks the replayed levels and reselects to D1 after the stairs, as the scenario serves") {
    if (!have_records("")) return;
    const Truth truth("floor_change");
    Survey s("floor_change");
    // Indoor levels give C1 of 80 dB with the broadcast RXLEV_ACCESS_MIN:
    // the database's planning values make the walk interesting (C1 = level + 50).
    const Hand hand{-50, 20, 6, 0, 3};
    const db::CellDatabase d = with_thresholds(database_of(truth), -50, 20, 6, 0, 3);
    const std::vector<db::WalkStep> steps = db::walk_steps(s.run.cycles(), d, s.techs);
    REQUIRE(steps.size() == s.run.cycles().size());
    const auto walk = db::simulate_walk(d, s.techs, steps);
    REQUIRE(walk.size() == steps.size());
    CHECK(walk.front().serving == "D0");
    std::int64_t switched = -1;
    for (std::size_t k = 0; k < walk.size(); ++k) {
        const db::WalkEvent& ev = walk[k];
        CAPTURE(ev.time_ns);
        REQUIRE_MESSAGE(ev.outcome.ok, ev.outcome.error);
        // The state by hand from the step's levels.
        const auto lv = [&](const std::string& id) {
            const auto it = steps[k].levels.find(id);
            return it == steps[k].levels.end() ? db::kAbsentLevelDbm : it->second;
        };
        std::vector<double> ns;
        for (const std::string& n : db::neighbour_entries(d, ev.serving)) ns.push_back(lv(n));
        bool reselect = false;
        CHECK(ev.outcome.state == hand.state(lv(ev.serving), ns, &reselect));
        CHECK(ev.reselected == reselect);
        if (ev.reselected) {
            CHECK(switched < 0);  // once
            CHECK(ev.chosen == "D1");
            switched = ev.time_ns;
        }
        // The simulated serving cell is the one the scenario serves, away
        // from the stairs (6..8 s).
        const ym::CarrierSample* srv = s.run.cycles()[k].serving();
        if (srv != nullptr && (ev.time_ns <= 6 * kSecond || ev.time_ns >= 8 * kSecond)) {
            const std::string after = ev.reselected ? ev.chosen : ev.serving;
            CHECK(truth.by_frequency.at(srv->latest.frequency_hz) == after);
        }
    }
    CHECK(switched > 6 * kSecond);
    CHECK(switched <= 8 * kSecond);
    CHECK(walk.back().serving == "D1");
}

TEST_CASE("DB-08 failed_site: N1 stays serving; superior while N2 is on air, exclusive while it is off; N3 is never chosen") {
    if (!have_records("")) return;
    const Truth truth("failed_site");
    Survey s("failed_site");
    const Hand hand{-100, 20, 6, 0, 3};
    const db::CellDatabase d = with_thresholds(database_of(truth), -100, 20, 6, 0, 3);
    CHECK(db::neighbour_entries(d, "N1") == std::vector<std::string>{"N2", "N3"});
    const std::vector<db::WalkStep> steps = db::walk_steps(s.run.cycles(), d, s.techs);
    db::WalkOptions o;
    o.start = "N1";
    const auto walk = db::simulate_walk(d, s.techs, steps, o);
    REQUIRE(walk.size() == steps.size());
    std::size_t superior = 0, exclusive = 0;
    for (std::size_t k = 0; k < walk.size(); ++k) {
        const db::WalkEvent& ev = walk[k];
        CAPTURE(ev.time_ns);
        CHECK(ev.serving == "N1");
        CHECK_FALSE(ev.reselected);
        CHECK(steps[k].levels.count("N3") == 0);
        const bool n2 = steps[k].levels.count("N2") != 0;
        // N2's level is there exactly when the truth has it on air in the cycle's period.
        if (ev.time_ns >= 6 * kSecond + kSecond && ev.time_ns <= 8 * kSecond) CHECK_FALSE(n2);
        if (ev.time_ns >= kSecond && ev.time_ns <= 6 * kSecond) CHECK(n2);
        bool reselect = false;
        const auto at = [&](const char* id) {
            const auto it = steps[k].levels.find(id);
            return it == steps[k].levels.end() ? db::kAbsentLevelDbm : it->second;
        };
        if (!steps[k].levels.count("N1")) continue;
        CHECK(ev.outcome.state == hand.state(at("N1"), {at("N2"), at("N3")}, &reselect));
        CHECK_FALSE(reselect);
        superior += ev.outcome.state == "superior" ? 1 : 0;
        exclusive += ev.outcome.state == "exclusive" ? 1 : 0;
    }
    CHECK(superior > 0);
    CHECK(exclusive > 0);
}
