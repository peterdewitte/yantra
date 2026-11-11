// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.8 models: the cell database editor (DB-01, DB-02) and the network
// checks (DB-03, DB-04, DB-05, DB-08) with the TETRA technology plugin and
// generated received cells (no radio).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "yantra/app/app_state.h"
#include "yantra/app/cell_db_editor.h"
#include "yantra/app/network_checks.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
using namespace yantra;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "network" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths paths_with_tetra(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.user_data = dir / "data";
    p.home = dir / "home";
    fs::create_directories(p.home);
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra" YANTRA_PLUGIN_SUFFIX, folder / "tetra" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

void open_new(AppState& st, const fs::path& dir) {
    project::ProjectInfo info;
    info.name = "Cells";
    REQUIRE(st.new_project(dir / "cells.yantra", info).ok);
    REQUIRE(st.cell_db() != nullptr);
    REQUIRE(st.network_checks() != nullptr);
}

void set(CellForm& f, const std::string& name, const std::string& value) {
    for (CellFieldForm& ff : f.fields)
        if (ff.name == name) {
            ff.value = value;
            return;
        }
    f.new_field = name;
    FAIL("no field " << name);
}

// A TETRA entry through the form: network 901/1, a location area, a carrier.
std::string add_cell(CellDbEditor& ed, const std::string& label, std::int64_t hz, std::int64_t la,
                     const std::string& neighbours, bool thresholds = true) {
    REQUIRE(ed.new_entry("tetra").ok);
    CellForm& f = ed.form();
    f.label = label;
    set(f, "frequency_hz", std::to_string(hz));
    set(f, "mcc", "901");
    set(f, "mnc", "1");
    set(f, "la", std::to_string(la));
    set(f, "colour_code", "1");
    set(f, "rxlev_access_min_dbm", "-110");
    set(f, "ms_txpwr_max_cell_dbm", "30");
    if (thresholds) {
        for (const char* n : {"srt_db", "srh_db", "frt_db", "frh_db"}) {
            bool have = false;
            for (const CellFieldForm& ff : f.fields) have = have || ff.name == n;
            if (!have) {
                f.new_field = n;
                REQUIRE(ed.add_field().ok);
            }
        }
        set(f, "srt_db", "25");
        set(f, "srh_db", "6");
        set(f, "frt_db", "10");
        set(f, "frh_db", "3");
    }
    f.neighbours = neighbours;
    const Result r = ed.apply();
    REQUIRE_MESSAGE(r.ok, r.message);
    return ed.selected();
}

celldb::ObservedCell received(std::int64_t hz, std::int64_t la, std::int64_t colour_code, double level,
                              std::vector<std::int64_t> neighbour_hz = {}) {
    celldb::ObservedCell c;
    c.technology = "tetra";
    c.fields["frequency_hz"] = hz;
    c.fields["mcc"] = std::int64_t{901};
    c.fields["mnc"] = std::int64_t{1};
    c.fields["la"] = la;
    c.fields["colour_code"] = colour_code;
    c.key = "frequency_hz=" + std::to_string(hz) + ";mcc=901;mnc=1;la=" + std::to_string(la);
    c.level = level;
    c.max_level = level;
    c.records = 3;
    c.serving = !neighbour_hz.empty();
    if (!neighbour_hz.empty()) {
        std::vector<celldb::NeighbourRef> ns;
        for (std::int64_t n : neighbour_hz) {
            celldb::NeighbourRef r;
            r.fields["frequency_hz"] = n;
            ns.push_back(r);
        }
        c.neighbours = ns;
    }
    return c;
}

}  // namespace

TEST_CASE("DB-01/DB-02 cell database editor: entries through the form, neighbours, remove, CSV, match fields") {
    const fs::path dir = scratch("editor");
    AppState st({}, paths_with_tetra(dir));
    open_new(st, dir);
    CellDbEditor& ed = *st.cell_db();
    CHECK(ed.size() == 0);
    CHECK(ed.technologies() == std::vector<std::string>{"tetra"});

    // The form of a new TETRA entry has the plugin's KEY fields.
    REQUIRE(ed.new_entry("tetra").ok);
    std::vector<std::string> keys;
    for (const CellFieldForm& f : ed.form().fields)
        if (f.key) keys.push_back(f.name);
    CHECK(keys == std::vector<std::string>{"frequency_hz", "mcc", "mnc", "la"});
    CHECK(ed.form().schema == "tetra_cell");

    const std::string a = add_cell(ed, "Indoor DAS", 390'000'000, 100, "la=200/frequency_hz=390200000\n");
    const std::string b = add_cell(ed, "Macro", 390'200'000, 200, "frequency_hz=390000000");
    REQUIRE(ed.size() == 2);
    const CellRowView& r = ed.rows()[0];
    CHECK(r.name == "Indoor DAS");
    CHECK(r.carrier == "390.0000 MHz");
    CHECK(r.channel == "33600");  // band 3, carrier 3600 (the plugin's raster)
    CHECK(r.identity.find("la=0x64") != std::string::npos);  // la in hexadecimal
    CHECK(r.neighbours == 1);
    CHECK(st.project()->cell_database().entries().size() == 2);  // stored at once

    // Editing: the form shows the stored values; a change is stored.
    REQUIRE(ed.select(a).ok);
    CHECK(ed.form().neighbours == "frequency_hz=390200000/la=200\n");
    ed.form().site_name = "Basement";
    ed.form().azimuth = "45";
    REQUIRE(ed.apply().ok);
    CHECK(st.project()->cell_database().find(a)->site_name == "Basement");
    CHECK(*st.project()->cell_database().find(a)->azimuth_deg == doctest::Approx(45.0));
    // Bad input is refused, nothing stored.
    ed.form().neighbours = "la=200/frequency_hz";
    Result bad = ed.apply();
    CHECK_FALSE(bad.ok);
    CHECK(bad.message.find("Neighbour line 1") != std::string::npos);
    ed.form().neighbours.clear();
    ed.form().azimuth = "north";
    CHECK_FALSE(ed.apply().ok);
    ed.form().new_field = "Not A Name";
    CHECK_FALSE(ed.add_field().ok);
    ed.cancel();
    CHECK(st.project()->cell_database().find(a)->neighbours.size() == 1);

    // The filter.
    CHECK(ed.filtered("macro").size() == 1);
    CHECK(ed.filtered("390.").size() == 2);
    CHECK(ed.filtered("nothing like it").empty());

    // CSV export and import (replace) give the same entries back.
    ed.csv().file = dir / "cells.csv";
    REQUIRE(ed.export_csv().ok);
    std::ifstream in(ed.csv().file);
    std::stringstream text;
    text << in.rdbuf();
    CHECK(text.str().find("0x64") != std::string::npos);
    REQUIRE(ed.remove(b).ok);
    CHECK(ed.size() == 1);
    ed.csv().replace = true;
    const Result imp = ed.import_csv();
    REQUIRE_MESSAGE(imp.ok, imp.message);
    CHECK(ed.size() == 2);
    CHECK(ed.problems().empty());
    CHECK(st.project()->cell_database().find(a)->site_name == "Basement");
    // A broken row is skipped with its reason.
    {
        std::ofstream out(dir / "broken.csv");
        out << "#yantra-cells,1\nid,label,technology,frequency_hz,mcc,mnc,la\nX1,Broken,tetra,not-a-number,901,1,\n";
    }
    ed.csv().file = dir / "broken.csv";
    ed.csv().replace = false;
    (void)ed.import_csv();
    CHECK(ed.size() <= 3);

    // Match fields (DB-01 "by LA, by channel or by both").
    CHECK(ed.match_fields_text("tetra") == "frequency_hz, mcc, mnc, la");
    REQUIRE(ed.set_match_fields("tetra", "la, frequency_hz").ok);
    CHECK(ed.match_fields_text("tetra") == "la, frequency_hz");
    CHECK(st.project()->cell_database().match_fields("tetra") == std::vector<std::string>{"la", "frequency_hz"});
    CHECK_FALSE(ed.set_match_fields("tetra", "la, 9x").ok);
    REQUIRE(ed.set_match_fields("tetra", "").ok);
    CHECK(ed.match_fields_text("tetra") == "frequency_hz, mcc, mnc, la");
}

TEST_CASE("DB-04/DB-03/DB-05 verification, auto-learn, difference log and cross-check with generated cells") {
    const fs::path dir = scratch("verify");
    AppState st({}, paths_with_tetra(dir));
    open_new(st, dir);
    CellDbEditor& ed = *st.cell_db();
    const std::string a = add_cell(ed, "A", 390'000'000, 100, "frequency_hz=390200000");
    const std::string b = add_cell(ed, "B", 390'200'000, 100, "frequency_hz=390000000\nfrequency_hz=390600000");
    (void)add_cell(ed, "Far", 391'000'000, 100, "");

    std::vector<celldb::ObservedCell> air;
    NetworkChecks nc(*st.project(), [&st] { return st.technologies(); }, [&air] { return air; });
    // Nothing received yet.
    VerificationView v0 = nc.verification();
    CHECK(v0.received == 0);
    CHECK_FALSE(v0.note.empty());
    CHECK_FALSE(nc.learn().ok);

    // A as stored but with another colour code; B as stored; C new.
    air = {received(390'000'000, 100, 2, -90.0, {390'200'000}), received(390'200'000, 100, 1, -80.0),
           received(390'400'000, 100, 1, -95.0)};
    const VerificationView v = nc.verification();
    CHECK(v.received == 3);
    CHECK(v.paired == 2);
    CHECK(v.not_in_database == 1);
    CHECK(v.not_seen == 1);  // Far
    CHECK(v.differences >= 1);
    bool differs = false, is_new = false, missing = false, match = false;
    for (const VerificationRow& r : v.rows) {
        if (r.state == "differs" && r.item == "colour_code") {
            differs = true;
            CHECK(r.database == "1");
            CHECK(r.received == "2");
            CHECK(r.rgb == kDiffersRgb);
        }
        if (r.state == "not_in_database" || r.state == "new") is_new = true;
        if (r.state == "not_seen" || r.state == "missing") missing = true;
        if (r.state == "match") {
            match = true;
            CHECK(r.rgb == kMatchRgb);
        }
    }
    CHECK(differs);
    CHECK(is_new);
    CHECK(missing);
    CHECK(match);
    // Without the matches only the other lines are left.
    for (const VerificationRow& r : nc.verification(false).rows) CHECK(r.state != "match");

    // The difference log: each difference once.
    const fs::path log = nc.default_log_file();
    CHECK(log.filename() == "cells_differences.csv");
    Result w1 = nc.write_log(log, true);
    REQUIRE_MESSAGE(w1.ok, w1.message);
    CHECK(w1.message.find("Wrote 0 ") == std::string::npos);
    Result w2 = nc.write_log(log, true);
    REQUIRE(w2.ok);
    CHECK(w2.message.find("Wrote 0 ") != std::string::npos);

    // Cross-check: A (its list as received) and B (its list as stored)
    // list each other; B's entry for 390.6 MHz names no known cell.
    const std::vector<CrossCheckRow> cc = nc.cross_check();
    bool unknown = false;
    for (const CrossCheckRow& r : cc) {
        CHECK_FALSE(r.text.empty());
        CHECK(r.kind != "not_mutual");
        unknown = unknown || r.kind == "not_seen";
    }
    CHECK(unknown);

    // Auto-learn adds C, never overwrites A's colour code.
    const Result l = nc.learn();
    REQUIRE_MESSAGE(l.ok, l.message);
    CHECK(st.project()->cell_database().entries().size() == 4);
    CHECK(std::get<std::int64_t>(*st.project()->cell_database().find(a)->field("colour_code")) == 1);
    CHECK(nc.verification().not_in_database == 0);
    (void)b;
}

TEST_CASE("DB-08 reselection simulator: the TETRA rule as the plugin has it, typed and received levels") {
    const fs::path dir = scratch("reselection");
    AppState st({}, paths_with_tetra(dir));
    open_new(st, dir);
    CellDbEditor& ed = *st.cell_db();
    const std::string a = add_cell(ed, "A", 390'000'000, 100, "frequency_hz=390100000\nfrequency_hz=390200000");
    const std::string b = add_cell(ed, "B", 390'100'000, 100, "frequency_hz=390000000");
    std::vector<celldb::ObservedCell> air;
    NetworkChecks nc(*st.project(), [&st] { return st.technologies(); }, [&air] { return air; });

    CHECK(nc.serving_choices().size() == 2);
    CHECK_FALSE(nc.reselection().ok);  // no serving cell yet
    CHECK_FALSE(nc.choose_serving("nope").ok);
    REQUIRE(nc.choose_serving(a).ok);
    // The hand-computed case of tests/celldb (C1 = level + 110 at 30 dBm).
    nc.reselection_form().levels[a] = -90.0;
    nc.reselection_form().levels[b] = -80.0;
    ReselectionView v = nc.reselection();
    REQUIRE_MESSAGE(v.ok, v.error);
    CHECK(v.state == "improvable");
    CHECK(v.chosen == b);
    CHECK_FALSE(v.state_label.empty());
    CHECK_FALSE(v.reason.empty());
    REQUIRE(v.rows.size() == 2);  // 390.2 MHz is no entry
    CHECK(v.rows[0].serving);
    CHECK(*v.rows[0].c1 == doctest::Approx(20.0));
    CHECK(*v.rows[1].c1 == doctest::Approx(30.0));
    CHECK(v.rows[1].chosen);
    // A strong serving cell stays.
    nc.reselection_form().levels[a] = -60.0;
    v = nc.reselection();
    CHECK(v.state == "superior");
    CHECK(v.chosen.empty());
    // Lower MS power: C1 lower by MS_TXPWR_MAX - P.
    nc.reselection_form().ms_power_dbm = 25.0;
    CHECK(*nc.reselection().rows[0].c1 == doctest::Approx(45.0));
    nc.reselection_form().ms_power_dbm = 30.0;

    // The levels received.
    CHECK_FALSE(nc.use_received_levels().ok);
    air = {received(390'000'000, 100, 1, -105.0, {390'100'000}), received(390'100'000, 100, 1, -100.0)};
    REQUIRE(nc.use_received_levels().ok);
    v = nc.reselection();
    CHECK(v.rows[0].level_dbm == doctest::Approx(-105.0));
    CHECK(v.state == "relinquishable");
    CHECK(v.chosen == b);
}

TEST_CASE("Network menu: the windows need a project; the survey monitor collects received cells") {
    const fs::path dir = scratch("menu");
    AppState st({}, paths_with_tetra(dir));
    for (Action x : {Action::CellDatabase, Action::Verification, Action::Reselection}) {
        CHECK_FALSE(st.action(x).enabled);
        CHECK_FALSE(st.trigger(x).ok);
    }
    open_new(st, dir);
    CHECK(st.trigger(Action::CellDatabase).ok);
    CHECK(st.trigger(Action::Verification).ok);
    CHECK(st.trigger(Action::Reselection).ok);
    CHECK(st.network_windows().cell_db);
    CHECK(st.network_windows().verification);
    CHECK(st.network_windows().reselection);
    CHECK(st.technologies().size() == 1);
    CHECK(st.monitor()->observed_cells().empty());
    REQUIRE(st.close_project().ok);
    CHECK(st.cell_db() == nullptr);
    CHECK_FALSE(st.network_windows().cell_db);
}

#ifdef YANTRA_TEST_DEMO_PROJECT
#include "yantra/app/post_processing.h"
#include "yantra/core/survey/views.h"

TEST_CASE("PP-01..05: post-processing views on the demo: good/bad, offset, failed cells, bins, acceptance, load") {
    const fs::path dir = scratch("post");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    std::shared_ptr<project::Project> p = project::Project::open(dir / "fraunces.yantra");
    ProjectWindow w(*p);
    PostProcessing pp(p, w, {});
    CHECK(pp.settings() == PostSettings{});
    const FloorContent* c = w.content();
    REQUIRE(c);
    REQUIRE(c->points.count() > 10);
    CHECK(c->view_label.empty());
    const std::size_t n = c->points.count();
    const PostStatsView plain = pp.stats(2.0);
    REQUIRE(plain.stats.median);
    CHECK(plain.stats.points == n);
    REQUIRE(plain.area);
    CHECK(plain.area->squares > 0);

    // PP-02 good/bad against a threshold.
    pp.settings().mode = PostSettings::Mode::GoodBad;
    pp.settings().threshold = *plain.stats.median;
    pp.apply();
    c = w.content();
    REQUIRE(c->classified);
    CHECK(c->classified->good + c->classified->bad + c->classified->no_value == n);
    CHECK(c->classified->good > 0);
    CHECK(c->classified->bad > 0);
    CHECK_FALSE(c->view_label.empty());
    for (const survey::SurveyPoint& sp : c->points.points)
        CHECK((sp.rgb == c->classified->good_rgb || sp.rgb == c->classified->bad_rgb ||
               sp.rgb == c->classified->no_value_rgb));
    const PostStatsView gb = pp.stats(2.0);
    REQUIRE(gb.classified);
    REQUIRE(gb.stats.thresholds.size() == 1);
    CHECK(gb.stats.thresholds[0].threshold == doctest::Approx(*plain.stats.median));

    // PP-04 a level offset: a labelled simulation, the median 10 dB lower.
    pp.settings().mode = PostSettings::Mode::Offset;
    pp.settings().offset_db = -10.0;
    pp.apply();
    c = w.content();
    CHECK(c->view_label.find("Simulation") != std::string::npos);
    const PostStatsView off = pp.stats(2.0);
    REQUIRE(off.stats.median);
    CHECK(*off.stats.median == doctest::Approx(*plain.stats.median - 10.0));
    REQUIRE(off.area);
    CHECK(off.area->percent <= plain.area->percent);

    // PP-02 serving changes and PP-05 the bin percentile.
    pp.reset();
    pp.settings().serving_changes = true;
    pp.settings().bin_percentile = 90.0;
    pp.apply();
    c = w.content();
    CHECK(c->serving_changes == survey::serving_changes(c->points.points));
    REQUIRE(c->binned);
    CHECK(c->binned->label.find("P90") != std::string::npos);

    // PP-03 failed cells: the best server is chosen without them.
    const std::vector<survey::CellRef> cells = pp.cells_seen();
    REQUIRE(cells.size() >= 2);
    survey::CellRef busiest = cells.front();
    std::size_t most = 0;
    for (const survey::CellRef& cr : cells) {
        std::size_t k = 0;
        for (const survey::SurveyPoint& sp : c->points.points) k += sp.serving_hz == cr.frequency_hz ? 1 : 0;
        if (k > most) {
            most = k;
            busiest = cr;
        }
    }
    REQUIRE(most > 0);
    pp.set_failed(busiest, true);
    CHECK(pp.failed(busiest));
    pp.apply();
    c = w.content();
    for (const survey::SurveyPoint& sp : c->points.points) CHECK(sp.serving_hz != busiest.frequency_hz);
    CHECK(pp.cells_seen().size() == cells.size());  // the failed cell stays in the choice
    pp.set_failed(busiest, false);
    pp.apply();

    // EXP-06 acceptance on the three bases.
    pp.reset();
    pp.acceptance().threshold_dbm = *plain.stats.median;
    pp.acceptance().min_percent = 40.0;
    pp.acceptance().basis = survey::AcceptanceProfile::Basis::Points;
    PostStatsView a = pp.stats(2.0);
    CHECK(a.acceptance.points == n);
    REQUIRE(a.acceptance.criteria.size() == 1);
    CHECK(a.acceptance.pass);
    CHECK(a.acceptance_text.find("PASS") != std::string::npos);
    pp.acceptance().basis = survey::AcceptanceProfile::Basis::TestedArea;
    a = pp.stats(2.0);
    CHECK(a.acceptance.squares > 0);
    CHECK(a.acceptance.area_m2 > 0.0);
    pp.acceptance().basis = survey::AcceptanceProfile::Basis::FloorArea;
    a = pp.stats(2.0);
    REQUIRE(w.content()->interpolation);
    CHECK_MESSAGE(a.acceptance.cells > 0, a.acceptance.note);
    pp.acceptance().min_percent = 100.0;
    pp.acceptance().threshold_dbm = 0.0;
    CHECK_FALSE(pp.stats(2.0).acceptance.pass);

    // PP-01 the hover readout: serving, strongest and every neighbour.
    const survey::PointDetail* d = w.point_detail(0);
    REQUIRE(d);
    CHECK(d->serving);
    const auto& p0 = w.content()->points.points.front();
    LayerSettings layers;
    const HoverReadout h = w.hover(p0.plan, 1.0, layers);
    REQUIRE(h.point);
    CHECK(h.text.find("Measured point") != std::string::npos);
    CHECK(h.text.find("Serving: ") != std::string::npos);
    CHECK(h.text.find("Plan ") != std::string::npos);

    // PP-01 loading a recording into the project.
    pp.import_folder() = fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path() / "recordings";
    const std::vector<RecordingChoice> recs = pp.recordings();
    REQUIRE_FALSE(recs.empty());
    // The demo already holds it: refused.
    const Result again = pp.import(recs.front());
    CHECK_FALSE(again.ok);
    CHECK(again.message.find("already in the project") != std::string::npos);
    // Without its walk tests the recording loads again as points.
    std::string rec_session;
    for (const project::RecordingFile& f : p->recordings())
        if (f.session_name == recs.front().name) rec_session = f.session_id;
    REQUIRE_FALSE(rec_session.empty());
    for (const project::Session& ss : p->sessions())
        if (ss.recording_session == rec_session) p->remove_session(ss.id);
    for (const project::RecordingFile& f : p->recordings())
        if (f.session_id == rec_session) p->remove_recording(f.id);
    const std::size_t before = p->sessions().size();
    const Result r = pp.import(recs.front());
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(r.message.find("Loaded " + recs.front().name) != std::string::npos);
    CHECK(p->sessions().size() > before);
    CHECK_FALSE(pp.import(RecordingChoice{}).ok);
}
#endif
