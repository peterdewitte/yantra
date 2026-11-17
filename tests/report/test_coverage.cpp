// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The indoor coverage and acceptance report (EXP-05, EXP-06, IND-30,
// IND-43, TST-34; docs/REPORT.md "Report content"): on a hand-made
// two-floor project the statistics, per-cell rows, serving-cell changes
// and the acceptance profiles on both bases (measured points and floor
// area of the interpolated map) against hand-computed values; stored and
// default profiles; the verification and cross-check tables; PDF and HTML
// written in one call. TST-34: for generated projects with many floors
// and dense walks, each floor section's points (count, order, plan
// position, value, colour) equal the project window's list, every point
// is drawn, and the point table of the HTML report lists them one for one.

#include "yantra/core/export/image.h"
#include "yantra/core/project/project.h"
#include "yantra/core/report/coverage.h"
#include "yantra/core/survey/acceptance.h"
#include "yantra/core/survey/interpolation.h"
#include "yantra/core/survey/points.h"
#include "yantra/core/survey/sample.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/survey/walk_test.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../survey/survey_helpers.h"

namespace yr = yantra::report;
namespace yx = yantra::exports;
using B = ys::AcceptanceProfile::Basis;

namespace {

const fs::path kData = fs::path(YANTRA_SOURCE_DIR) / "data";

bm::Point2 pu(double x_m, double y_m) { return {x_m * 20.0, y_m * 20.0}; }

// Manual points at `metres` with `levels` (dBm, 390 MHz) served by `cells`.
std::int64_t add_points(pj::Project& p, const std::string& floor, const std::vector<bm::Point2>& metres,
                        const std::vector<double>& levels, const std::vector<int>& cells) {
    pj::Session s;
    s.floor_id = floor;
    s.kind = "manual";
    s.name = "Rooms";
    s.radio = "test radio (fake)";
    const std::int64_t id = p.add_session(s);
    std::vector<pj::StoredPoint> pts;
    for (std::size_t i = 0; i < metres.size(); ++i) {
        pj::StoredPoint sp;
        sp.session_id = id;
        sp.sequence = static_cast<std::int64_t>(i + 1);
        sp.time_ns = static_cast<std::int64_t>(i + 1) * kS;
        sp.utc_ns = kUtc0 + sp.time_ns;
        sp.plan = pu(metres[i].x, metres[i].y);
        sp.kind = "manual";
        sp.group = static_cast<std::uint32_t>(i + 1);
        sp.sample = ys::sample_json(cycle(sp.time_ns, {Carrier{}.w_level(levels[i]).w_cell(cells[i])})).dump();
        pts.push_back(sp);
    }
    p.add_points(id, pts);
    return id;
}

// The ground floor: 10 points, 5 at or above -95 dBm, cell 1 then cell 2
// (one change), an antenna and a stored map; the first floor: 4 points at
// -80 dBm, no map.
struct ReportFixture : Fixture {
    std::vector<double> ground_levels{-60, -65, -70, -80, -90, -96, -100, -105, -110, -120};
    explicit ReportFixture(const std::string& name) : Fixture(name) {
        auto b = p->building();
        bm::Cell c;
        c.id = "cell-1";
        c.label = "Cell 1";
        c.technology_id = "tetra";
        c.schema_id = "tetra_cell";
        c.carrier_hz = 390'000'000;
        b.cells.push_back(c);
        auto& g = *std::find_if(b.floors.begin(), b.floors.end(), [&](const bm::Floor& f) { return f.id == ground; });
        bm::Antenna a;
        a.id = "antenna-1";
        a.name = "DAS 1";
        a.position = pu(4, 5);
        a.height_m = 2.7;
        a.cell = "cell-1";
        a.eirp_dbm = 10.0;
        g.antennas.push_back(a);
        p->replace_building(b);
        std::vector<bm::Point2> pts;
        for (int i = 0; i < 10; ++i) pts.push_back({1.0 + 1.8 * i, 2.0 + (i % 3) * 3.0});
        add_points(*p, ground, pts, ground_levels, {1, 1, 1, 1, 1, 1, 2, 2, 2, 2});
        add_points(*p, first, {{2, 2}, {6, 2}, {10, 5}, {14, 8}}, {-80, -80, -80, -80}, {1, 1, 1, 1});
        ys::InterpolationRequest q;
        q.floor_id = ground;
        q.metric.value = ys::Metric::Value::Level;
        const auto map = ys::interpolate(*p, q);
        REQUIRE(map);
        ys::store(*p, *map);
    }
    yr::CoverageOptions options() const {
        yr::CoverageOptions o;
        o.data_folders = {kData};
        o.threshold = -95.0;
        o.more_thresholds = {-85.0};
        o.created_utc_ns = 1793000000LL * kS;
        return o;
    }
};

const yr::AcceptanceRow* row(const std::vector<yr::AcceptanceRow>& rows, const std::string& profile, B basis) {
    for (const auto& r : rows)
        if (r.profile == profile && r.basis == basis) return &r;
    return nullptr;
}

std::string text_of(const yr::Document& d) {
    std::string s;
    for (const auto& [k, v] : d.cover.rows) s += k + ": " + v + "\n";
    for (const yr::Block& b : d.blocks) {
        s += b.heading.text + "\n" + b.paragraph.text + "\n";
        for (const auto& [k, v] : b.kv.rows) s += k + ": " + v + "\n";
        for (const auto& h : b.table.header) s += h + "|";
        for (const auto& r : b.table.rows)
            for (const auto& c : r) s += c + "|";
        for (const auto& e : b.legend.entries) s += e.label + "=" + e.count + ";";
        s += b.image.caption + b.chart.summary + "\n";
    }
    return s;
}

// CrossCheckFinding has no operator==.
bool same(const std::vector<yantra::celldb::CrossCheckFinding>& a, const std::vector<yantra::celldb::CrossCheckFinding>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].technology != b[i].technology || a[i].cell != b[i].cell ||
            a[i].entry != b[i].entry || a[i].other != b[i].other || a[i].text != b[i].text)
            return false;
    return true;
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string file_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// The cells of the rows of the HTML table that follows the element with
// id `anchor`.
std::vector<std::vector<std::string>> html_table(const std::string& html, const std::string& anchor) {
    std::vector<std::vector<std::string>> out;
    std::size_t at = html.find("id=\"" + anchor + "\"");
    if (at == std::string::npos) return out;
    at = html.find("<tbody>", at);
    const std::size_t end = html.find("</table>", at);
    while (true) {
        const std::size_t tr = html.find("<tr", at);
        if (tr == std::string::npos || tr > end) break;
        const std::size_t tr_end = html.find("</tr>", tr);
        std::vector<std::string> cells;
        std::size_t td = tr;
        while ((td = html.find("<td", td)) != std::string::npos && td < tr_end) {
            const std::size_t open = html.find('>', td) + 1;
            const std::size_t close = html.find("</td>", open);
            cells.push_back(html.substr(open, close - open));
            td = close;
        }
        out.push_back(cells);
        at = tr_end;
    }
    return out;
}

}  // namespace

TEST_CASE("EXP-05/06 report content: statistics, cells, changes and acceptance on both bases against hand-computed values") {
    ReportFixture fx("coverage-content");
    using C = ys::AcceptanceCriterion;
    ys::AcceptanceProfile walk;
    walk.name = "Walk";
    walk.criteria = {C{-95.0, 50.0}, C{-110.0, 95.0}};
    ys::AcceptanceProfile area;
    area.name = "DAS area";
    area.basis = B::FloorArea;
    area.criteria = {C{-95.0, 50.0}};
    ys::set_acceptance_profiles(*fx.p, {walk, area});

    const yr::CoverageReport rep = yr::coverage_report(*fx.p, fx.options());
    CHECK_FALSE(rep.default_profile);
    REQUIRE(rep.profiles.size() == 2);
    REQUIRE(rep.floors.size() == 2);
    const yr::FloorSection& g = rep.floors[0];
    const yr::FloorSection& f1 = rep.floors[1];
    CHECK(g.floor_id == fx.ground);
    CHECK(g.points.count() == 10);
    CHECK(g.drawn_points == 10);
    CHECK(f1.points.count() == 4);

    // Measured points: 5 of 10 >= -95 dBm, 4 of 10 >= -85 dBm.
    REQUIRE(g.stats.all.thresholds.size() == 2);
    CHECK(g.stats.all.thresholds[0].percent_at_or_above == doctest::Approx(50.0));
    CHECK(g.stats.all.thresholds[1].percent_at_or_above == doctest::Approx(40.0));
    CHECK(*g.stats.all.median == doctest::Approx(-93.0));
    // The building: 9 of 14.
    CHECK(rep.building.all.points == 14);
    CHECK(rep.building.all.thresholds[0].percent_at_or_above == doctest::Approx(900.0 / 14.0));

    // Per cell: cell 1 serves 6 points (4 at or above -95), cell 2 serves 4 (none).
    REQUIRE(g.cells.size() == 2);
    CHECK(g.cells[0].points == 6);
    CHECK(g.cells[0].percent == doctest::Approx(60.0));
    CHECK(g.cells[0].stats.thresholds[0].at_or_above == 5);
    CHECK(*g.cells[0].stats.max == doctest::Approx(-60.0));
    CHECK(g.cells[1].points == 4);
    CHECK(g.cells[1].stats.thresholds[0].at_or_above == 0);
    CHECK(*g.cells[1].stats.min == doctest::Approx(-120.0));
    // One serving-cell change, cell 1 -> cell 2.
    CHECK(g.stats.all.serving_cell_changes == 1);
    REQUIRE(g.changes.size() == 1);
    CHECK(g.changes[0].count == 1);
    CHECK(g.changes[0].from == g.cells[0].cell);
    CHECK(g.changes[0].to == g.cells[1].cell);
    CHECK(f1.changes.empty());

    // The map of the ground floor and its figures (IND-43).
    REQUIRE(g.map);
    CHECK(g.map->label() == "interpolated");
    REQUIRE(g.summary);
    CHECK(g.map->points.size() == 10);
    const auto area_stats = ys::area_stats(*g.map, {-95.0, -85.0}, area.min_confidence);
    CHECK(g.summary->interpolated.thresholds[0].percent == doctest::Approx(area_stats.thresholds[0].percent));
    CHECK_FALSE(f1.map);

    // Acceptance: "Walk" on the points decides (50 % >= -95 passes, 90 % >= -110 fails 95 %).
    const auto* wp = row(g.acceptance, "Walk", B::Points);
    REQUIRE(wp);
    CHECK(wp->decides);
    REQUIRE(wp->result.criteria.size() == 2);
    CHECK(wp->result.criteria[0].percent == doctest::Approx(50.0));
    CHECK(wp->result.criteria[0].pass);
    CHECK(wp->result.criteria[1].percent == doctest::Approx(90.0));
    CHECK_FALSE(wp->result.criteria[1].pass);
    CHECK_FALSE(wp->result.pass);
    // ... and on the floor area, shown but not deciding.
    const auto* wa = row(g.acceptance, "Walk", B::FloorArea);
    REQUIRE(wa);
    CHECK_FALSE(wa->decides);
    const auto want = ys::evaluate_area(
        [&] {
            auto q = walk;
            q.basis = B::FloorArea;
            return q;
        }(),
        {&*g.map});
    CHECK(wa->result == want);
    CHECK(wa->result.cells > 0);
    // "DAS area": the floor area decides; without a map the first floor is not assessed.
    const auto* da = row(g.acceptance, "DAS area", B::FloorArea);
    REQUIRE(da);
    CHECK(da->decides);
    CHECK(da->result.criteria[0].percent == doctest::Approx(area_stats.thresholds[0].percent));
    const auto* dn = row(f1.acceptance, "DAS area", B::FloorArea);
    REQUIRE(dn);
    CHECK_FALSE(dn->result.pass);
    CHECK(!dn->result.note.empty());
    CHECK(row(f1.acceptance, "DAS area", B::Points)->result.criteria[0].percent == doctest::Approx(100.0));
    // The building: points of both floors; the floor area of the one map.
    const auto* bp = row(rep.acceptance, "Walk", B::Points);
    REQUIRE(bp);
    CHECK(bp->result.points == 14);
    CHECK(bp->result.criteria[0].percent == doctest::Approx(900.0 / 14.0));
    CHECK(row(rep.acceptance, "DAS area", B::FloorArea)->result == row(g.acceptance, "DAS area", B::FloorArea)->result);
    CHECK_FALSE(row(rep.acceptance, "Walk", B::TestedArea));

    // The document says it.
    const std::string t = text_of(rep.document);
    CHECK(contains(t, "10 measured points"));
    CHECK(contains(t, "4 measured points"));
    CHECK(contains(t, "Coverage map (interpolated)"));
    CHECK(contains(t, "No interpolated map is stored for this floor."));
    CHECK(contains(t, "Inverse-distance weighting of 10 measured points, power 2.0, search radius 10.0 m"));
    CHECK(contains(t, "50.0 % of the measured points are at or above -95.0 dBm"));
    CHECK(contains(t, "Walk: FAIL"));
    CHECK(contains(t, "Share of measured points (%)"));  // IND-43: both shares, side by side
    CHECK(contains(t, "Share of floor area (interpolated) (%)"));
    CHECK(contains(t, "1 serving-cell changes on this floor."));
    CHECK(contains(t, "test radio (fake)"));
    CHECK(contains(t, "Every neighbour relation is mutual."));  // the building's one cell
    std::size_t charts = 0;
    for (const yr::Block& b : rep.document.blocks) charts += b.type == yr::Block::Type::Chart ? 1 : 0;
    CHECK(charts == 4);  // histogram and CDF per floor
}

TEST_CASE("EXP-06 the report uses the default profile when none is stored, and the given ones before the stored") {
    ReportFixture fx("coverage-profiles");
    auto rep = yr::coverage_report(*fx.p, fx.options());
    CHECK(rep.default_profile);
    REQUIRE(rep.profiles.size() == 1);
    CHECK(rep.profiles[0].name == "Indoor acceptance");
    CHECK(contains(text_of(rep.document), "the default profile \"Indoor acceptance\" is used"));
    // Points (decides) and floor area, per floor and for the building.
    CHECK(rep.acceptance.size() == 2);
    CHECK(rep.floors[0].acceptance.size() == 2);

    ys::AcceptanceProfile tested;
    tested.name = "Squares";
    tested.basis = B::TestedArea;
    tested.square_m = 4.0;
    tested.criteria = {{-95.0, 40.0}};
    auto o = fx.options();
    o.profiles = std::vector<ys::AcceptanceProfile>{tested};
    rep = yr::coverage_report(*fx.p, o);
    CHECK_FALSE(rep.default_profile);
    // Points, tested area (decides) and floor area.
    REQUIRE(rep.floors[0].acceptance.size() == 3);
    const auto* ta = row(rep.floors[0].acceptance, "Squares", B::TestedArea);
    REQUIRE(ta);
    CHECK(ta->decides);
    CHECK(ta->result == ys::evaluate(tested, rep.floors[0].points.points));
    CHECK(ta->result.squares > 0);

    o.profiles = std::vector<ys::AcceptanceProfile>{ys::AcceptanceProfile{}};
    CHECK_THROWS_AS(yr::coverage_report(*fx.p, o), yr::ReportError);
    o = fx.options();
    o.floors = {"no-such-floor"};
    CHECK_THROWS_AS(yr::coverage_report(*fx.p, o), yr::ReportError);
    o = fx.options();
    o.floors = {fx.first};
    rep = yr::coverage_report(*fx.p, o);
    REQUIRE(rep.floors.size() == 1);
    CHECK(rep.floors[0].floor_id == fx.first);
}

TEST_CASE("DB-04/DB-05 report tables: received cells against the cell database, and the cross-check") {
    ReportFixture fx("coverage-verify");
    namespace db = yantra::celldb;
    db::CellDatabase d;
    for (int i : {1, 2}) {
        db::CellEntry e;
        e.cell.id = i == 1 ? "cell-1" : "c2";  // cell-1 stays: the fixture's antenna serves it
        e.cell.technology_id = "test_tech";
        e.cell.schema_id = "test_cell";
        e.set_field("frequency_hz", static_cast<std::int64_t>(390'000'000 + 200'000 * (i - 1)));
        e.set_field("cell_id", static_cast<std::int64_t>(i));
        if (i == 1) {
            db::NeighbourRef r;
            r.fields["frequency_hz"] = static_cast<std::int64_t>(390'200'000);
            e.neighbours.push_back(r);  // c1 lists c2, c2 does not list c1
        }
        d.add(e);
    }
    fx.p->set_cell_database(d);
    auto o = fx.options();
    auto rep = yr::coverage_report(*fx.p, o);
    CHECK_FALSE(rep.verification);
    CHECK(same(rep.cross_check, db::cross_check(db::database_views(d))));
    CHECK(contains(text_of(rep.document), "No received cells were given"));

    db::ObservedCell seen;
    seen.technology = "test_tech";
    seen.key = "frequency_hz=390000000;cell_id=1";
    seen.fields["frequency_hz"] = static_cast<std::int64_t>(390'000'000);
    seen.fields["cell_id"] = static_cast<std::int64_t>(1);
    seen.serving = true;
    seen.records = 3;
    db::ObservedCell unknown = seen;
    unknown.key = "frequency_hz=391000000;cell_id=9";
    unknown.fields["frequency_hz"] = static_cast<std::int64_t>(391'000'000);
    unknown.fields["cell_id"] = static_cast<std::int64_t>(9);
    o.observed = std::vector<db::ObservedCell>{seen, unknown};
    rep = yr::coverage_report(*fx.p, o);
    REQUIRE(rep.verification);
    db::LogOptions lo;
    lo.hex_fields = {"la"};
    lo.include_not_seen = true;
    CHECK(rep.verification_lines == db::log_lines(*rep.verification, d, *o.observed, lo));
    bool not_in_db = false, not_seen = false;
    for (const auto& l : rep.verification_lines) {
        not_in_db = not_in_db || l.state == "not_in_database";
        not_seen = not_seen || l.state == "not_seen";
    }
    CHECK(not_in_db);
    CHECK(not_seen);  // c2 was not received
    CHECK(same(rep.cross_check, db::cross_check(db::combined_views(d, *o.observed, {}))));
    // The tables in the document hold every line and finding.
    std::size_t verify_rows = 0, cross_rows = 0;
    bool in_verify = false;
    for (const yr::Block& b : rep.document.blocks) {
        if (b.type == yr::Block::Type::Heading) in_verify = b.heading.anchor == "verification";
        if (b.type == yr::Block::Type::Table && in_verify) verify_rows = b.table.rows.size();
        if (b.type == yr::Block::Type::Table && b.table.header.size() == 4 && b.table.header[0] == "Finding")
            cross_rows = b.table.rows.size();
    }
    CHECK(verify_rows == rep.verification_lines.size());
    CHECK(cross_rows == rep.cross_check.size());
    CHECK(!rep.cross_check.empty());
}

TEST_CASE("EXP-05 write_coverage_report: PDF and HTML of the same report in one call, translated, the operator field") {
    ReportFixture fx("coverage-files");
    const fs::path dir = scratch("coverage-files-out");
    auto o = fx.options();
    o.surveyor = "Survey team B";
    o.demo_label = "DEMO - simulated data";
    o.translate = [](const std::string& s) { return s == "Building summary" ? std::string("Gebouwoverzicht") : s; };
    const auto rep = yr::write_coverage_report(*fx.p, o, dir / "r.pdf", dir / "r.html");
    const std::string pdf = file_text(dir / "r.pdf");
    const auto check = yr::check_pdf(std::vector<std::uint8_t>(pdf.begin(), pdf.end()));
    CHECK(check.ok);
    CHECK(check.pages >= 4);  // cover, metadata, summary, two floors
    const std::string html = file_text(dir / "r.html");
    CHECK(contains(html, "Gebouwoverzicht"));
    CHECK(contains(html, "Survey team B"));
    CHECK(contains(html, "DEMO - simulated data"));
    CHECK(contains(html, "Ground floor"));
    CHECK(contains(html, "First floor"));
    CHECK(contains(html, "Made with Yantra - curtat.be"));
    // The same content as the composed document.
    CHECK(html == yr::render_html(rep.document, yr::find_assets({kData})));
    // The same options give the same bytes (no clock in the composer).
    yr::write_coverage_report(*fx.p, o, dir / "again.pdf", {});
    CHECK(file_text(dir / "again.pdf") == pdf);
    CHECK_THROWS_AS(yr::write_coverage_report(*fx.p, o, dir / "r.html", {}), yr::ReportError);
    CHECK_THROWS_AS(yr::write_coverage_report(*fx.p, o, {}, dir / "r.pdf"), yr::ReportError);
}

TEST_CASE("TST-34 generated projects with many floors and dense walks: every floor section shows the window's points one for one") {
    const fs::path dir = scratch("coverage-tst34");
    constexpr int kFloors = 5;
    constexpr int kCycles = 900;
    auto p = std::shared_ptr<pj::Project>(pj::Project::create(dir / "m.yantra", pj::ProjectInfo{"Tower", "", "", ""}));
    std::vector<std::string> floors;
    for (int f = 0; f < kFloors; ++f) {
        bm::Floor fl;
        fl.name = "Floor " + std::to_string(f);
        fl.level = f;
        fl.plan_width = 400;
        fl.plan_height = 200;
        floors.push_back(p->add_floor(fl));
        p->set_scale(floors.back(), *bm::factor_scale(0.05));
    }
    for (int f = 0; f < kFloors; ++f) {
        ys::WalkTestOptions o;
        o.project = p;
        o.floor_id = floors[f];
        o.mode = ys::WalkMode::Continuous;
        auto w = std::make_shared<ys::WalkTest>(o);
        w->start(0);
        w->click(0, {0, 0});
        for (int i = 0; i <= kCycles; ++i) {
            const std::int64_t t = i * kS / 10;
            // Every few cycles a point without a serving level (a gap), and a dBFS one.
            std::vector<Carrier> cs{Carrier{}.w_hz(391'000'000).w_level(-62.0 - (i % 17)).w_cell(9).w_role(ym::CarrierRole::Neighbour)};
            if (i % 37 != 0) cs.push_back(Carrier{}.w_level(-55.0 - ((i * 7 + f) % 61) * 0.41).w_cell(1 + (i / 250)).w_c1(20.0 - (i % 11)));
            w->on_cycle(cycle(t, cs));
            if (i % 30 == 0 && i > 0) w->click(t, {static_cast<double>((i * 13) % 400), static_cast<double>((i / 30) % 200)});
        }
        w->finish(kCycles * kS / 10 + 1);
        o.kind = "manual";
        auto m = std::make_shared<ys::WalkTest>(o);
        m->start(0);
        for (int k = 0; k < 15; ++k) {
            m->point(k * 3 * kS, {10.0 + k * 15, 50.0 + k}, 2.0, "Room " + std::to_string(k));
            for (int c = 1; c <= 3; ++c) m->on_cycle(cycle(k * 3 * kS + c * kS, {Carrier{}.w_level(-70.0 - k - c).w_c1(10.0)}));
        }
        m->finish(46 * kS);
    }
    std::vector<ys::Metric> metrics(3);
    metrics[1].carrier = ys::Metric::Carrier::Strongest;
    metrics[2].value = ys::Metric::Value::C1;
    for (std::size_t k = 0; k < metrics.size(); ++k) {
        CAPTURE(k);
        yr::CoverageOptions o;
        o.data_folders = {kData};
        o.metric = metrics[k];
        o.point_table = true;
        o.max_plan_px = 400;
        const fs::path html = dir / ("r" + std::to_string(k) + ".html");
        const yr::CoverageReport rep = yr::write_coverage_report(*p, o, {}, html);
        const std::string text = file_text(html);
        REQUIRE(rep.floors.size() == static_cast<std::size_t>(kFloors));
        const auto counts = ys::point_counts(*p);
        for (int f = 0; f < kFloors; ++f) {
            CAPTURE(f);
            const yr::FloorSection& s = rep.floors[f];
            // The project window's list of the floor.
            const ys::PointList window = ys::point_list(*p, ys::PointQuery{floors[f], {}, metrics[k], std::nullopt});
            CHECK(window.count() == counts[f].second);
            CHECK(window.count() == static_cast<std::size_t>(kCycles + 1 + 15));
            CHECK(s.points == window);  // count, order, plan position, value, colour
            REQUIRE(s.table.rows.size() == window.count());
            for (std::size_t i = 0; i < window.count(); ++i) {
                REQUIRE(s.table.rows[i].plan == window.points[i].plan);
                REQUIRE(s.table.rows[i].value == window.points[i].value);
                REQUIRE(s.table.rows[i].rgb == window.points[i].rgb);
            }
            CHECK(s.drawn_points == window.count());  // dense: smaller markers, never dropped
            // The legend counts every point once.
            std::size_t legend = 0;
            for (const auto& e : yx::legend_entries(s.table)) legend += e.count;
            CHECK(legend == window.count());
            CHECK(s.stats.all.points == window.count());
            CHECK(contains(text, std::to_string(window.count()) + " measured points: every point of the project window"));
            // The point table of the HTML report, one row per point in the window's order.
            const auto rows = html_table(text, "points-" + floors[f]);
            REQUIRE(rows.size() == window.count());
            for (std::size_t i = 0; i < window.count(); ++i) {
                const ys::SurveyPoint& w = window.points[i];
                REQUIRE(rows[i].size() == 7);
                REQUIRE(rows[i][1] == std::to_string(w.session_id));
                REQUIRE(rows[i][2] == std::to_string(w.sequence));
                REQUIRE(rows[i][3] == yx::number_text(w.plan.x));
                REQUIRE(rows[i][4] == yx::number_text(w.plan.y));
                REQUIRE(rows[i][5] == (w.value ? yx::number_text(*w.value) + (w.unit.empty() ? "" : " " + w.unit) : "-"));
            }
        }
        // The building summary counts them all.
        std::size_t total = 0;
        for (const auto& [id, n] : counts) total += n;
        CHECK(rep.building.all.points == total);
    }
}
