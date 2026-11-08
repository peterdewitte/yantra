// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The indoor coverage and acceptance report of the Fraunces demo (EXP-05,
// EXP-06, IND-30, IND-43, DB-04, DB-05, TST-34; docs/REPORT.md "Report
// content"): every floor's points are the project window's list, the
// stored interpolated maps with their fit and "% of floor area" equal
// the map statistics, the default acceptance profile on both bases, the
// recordings replayed through the TETRA plugin for the verification
// against the demo's cell database, the DEMO label, and the PDF and HTML
// written in one call. Skips without a built demo.

#include <doctest/doctest.h>

#include <yantra/core/celldb/technology.h>
#include <yantra/core/report/coverage.h>
#include <yantra/core/survey/interpolation.h>

#include "demo_helpers.h"

using namespace demo_test;
namespace yr = yantra::report;
namespace db = yantra::celldb;
namespace yp = yantra::plugin;
using B = ys::AcceptanceProfile::Basis;

namespace {

const fs::path kDescription = fs::path(DEMO_DATA_DIR) / "fraunces" / "fraunces.json";

bool demo_built() {
    if (std::string(DEMO_FRAUNCES_DIR).empty() || !fs::exists(fs::path(DEMO_FRAUNCES_DIR) / "fraunces.yantra")) {
        MESSAGE("no demo project built (no radio tools): skipped");
        return false;
    }
    return true;
}

std::string text_of(const yr::Document& d) {
    std::string s;
    for (const auto& [k, v] : d.cover.rows) s += k + ": " + v + "\n";
    for (const yr::Block& b : d.blocks) {
        s += b.heading.text + "\n" + b.paragraph.text + "\n";
        for (const auto& [k, v] : b.kv.rows) s += k + ": " + v + "\n";
        for (const auto& r : b.table.rows)
            for (const auto& c : r) s += c + "|";
        s += b.image.caption + "\n";
    }
    return s;
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("EXP-05/06 TST-34 the Fraunces demo report: window points, maps, acceptance, verification, PDF and HTML") {
    if (!demo_built()) return;
    const fs::path dir = scratch("report_demo");
    fs::copy(DEMO_FRAUNCES_DIR, dir / "demo", fs::copy_options::recursive);
    const yd::DemoDescription d = yd::read_description(kDescription);
    auto demo = yj::Project::open(dir / "demo/fraunces.yantra");
    const auto before = file_sha256(dir / "demo/fraunces.yantra");

    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    const auto catalog = tetra_catalog(loader);
    const auto tech = db::Technology::from_plugin(*loader.info("yantra.tetra"), loader.descriptor("yantra.tetra"),
                                                  loader.instance("yantra.tetra"));
    REQUIRE(tech);
    const db::Technologies techs{*tech};
    std::vector<std::string> warnings;
    const auto observed = yr::observed_cells(*demo, catalog, techs, &warnings);
    CHECK(warnings.empty());
    REQUIRE(!observed.empty());

    yr::CoverageOptions o;
    o.data_folders = {fs::path(YANTRA_SOURCE_DIR) / "data"};
    o.metric = yd::demo_metric(*demo, d);
    o.technologies = &techs;
    o.observed = observed;
    o.demo_label = "DEMO - simulated data";
    o.surveyor = "Demo team";
    o.created_utc_ns = 1793000000LL * 1000000000LL;
    const yr::CoverageReport rep = yr::write_coverage_report(*demo, o, dir / "demo.pdf", dir / "demo.html");

    // Every floor: the window's points one for one, all drawn.
    REQUIRE(rep.floors.size() == demo->floors().size());
    std::size_t total = 0;
    for (const yr::FloorSection& s : rep.floors) {
        CAPTURE(s.name);
        const ys::PointList window = points_of(*demo, d, s.floor_id);
        CHECK(window.count() > 0);
        CHECK(s.points == window);
        CHECK(s.drawn_points == window.count());
        CHECK(s.table.rows.size() == window.count());
        total += window.count();
        // The stored map of the floor, its fit and floor-area shares.
        REQUIRE(s.map);
        CHECK(s.map->request.metric == o.metric);
        REQUIRE(s.summary);
        CHECK_FALSE(s.summary->stale);
        CHECK(s.summary->fit.points > 0);
        const auto a = ys::area_stats(*s.map, {o.threshold}, rep.profiles.front().min_confidence);
        REQUIRE(s.summary->interpolated.thresholds.size() == 1);
        CHECK(s.summary->interpolated.thresholds[0].percent == doctest::Approx(a.thresholds[0].percent));
        // The default profile on the measured points (decides) and the floor area.
        REQUIRE(s.acceptance.size() == 2);
        CHECK(s.acceptance[0].basis == B::Points);
        CHECK(s.acceptance[0].decides);
        CHECK(s.acceptance[0].result == ys::evaluate(rep.profiles.front(), window.points));
        CHECK(s.acceptance[1].basis == B::FloorArea);
        CHECK(s.acceptance[1].result.cells > 0);
        CHECK(s.acceptance[1].result.criteria[0].percent == doctest::Approx(a.thresholds[0].percent));
        CHECK_FALSE(s.cells.empty());
    }
    CHECK(rep.building.all.points == total);
    CHECK(rep.default_profile);

    // Verification: the demo's received cells against its cell database.
    REQUIRE(rep.verification);
    std::size_t paired = 0;
    for (const db::CellCheck& c : rep.verification->cells) paired += c.status == db::CellCheck::Status::Paired ? 1 : 0;
    CHECK(paired == demo->cell_database().entries().size());

    const std::string t = text_of(rep.document);
    CHECK(contains(t, "Demo team"));
    CHECK(contains(t, "yantra-gen"));  // the walk tests' radio
    CHECK(contains(t, "interpolated"));
    CHECK(contains(t, "Network verification"));
    CHECK(contains(t, std::to_string(total)));
    const std::string pdf = file_bytes(dir / "demo.pdf");
    const auto check = yr::check_pdf(std::vector<std::uint8_t>(pdf.begin(), pdf.end()));
    CHECK(check.ok);
    CHECK(check.pages >= 3 + static_cast<int>(rep.floors.size()));
    const std::string html = file_bytes(dir / "demo.html");
    CHECK(contains(html, "DEMO - simulated data"));
    CHECK(contains(html, "Made with Yantra - curtat.be"));
    // The report does not change the project.
    demo.reset();
    CHECK(file_sha256(dir / "demo/fraunces.yantra") == before);
}
