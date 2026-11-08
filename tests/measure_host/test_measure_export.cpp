// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Batch export (EXP-04, docs/EXPORT.md) of the Fraunces demo recordings
// with the TETRA plugin: placed through a scratch copy of the demo
// project, the rows of each recording are the rows the project window
// gives for its walk (IND-30); merged and separate files; without a
// project, one row per replayed cycle; the C test exporter plugin
// (PLG-02) in batch. Also Project::write_copy and the recording header
// line parser used by the export.

#include <doctest/doctest.h>

#include <yantra/core/export/batch.h>
#include <yantra/core/export/csv.h>
#include <yantra/core/export/table.h>
#include <yantra/core/recording/reader.h>

#include "demo_helpers.h"

using namespace demo_test;
namespace yx = yantra::exports;
namespace yp = yantra::plugin;

namespace {

const fs::path kDescription = fs::path(DEMO_DATA_DIR) / "fraunces" / "fraunces.json";

bool demo_built() {
    if (std::string(DEMO_FRAUNCES_DIR).empty() || !fs::exists(fs::path(DEMO_FRAUNCES_DIR) / "fraunces.yantra")) {
        MESSAGE("no demo project built (no radio tools): skipped");
        return false;
    }
    return true;
}

std::size_t line_count(const std::string& s) { return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n')); }

}  // namespace

TEST_CASE("EXP-04 IND-30 batch export of the demo recordings: each recording's rows are the project window's rows") {
    if (!demo_built()) return;
    const fs::path dir = scratch("export_batch");
    fs::copy(DEMO_FRAUNCES_DIR, dir / "demo", fs::copy_options::recursive);
    // A folder tree: one recording in a sub-folder, plus a file that is not a recording.
    fs::create_directories(dir / "tree" / "sub");
    fs::copy_file(dir / "demo/recordings/first-floor_walk_0000.yrec", dir / "tree/first-floor_walk_0000.yrec");
    fs::copy_file(dir / "demo/recordings/second-floor_walk_0000.yrec", dir / "tree/sub/second-floor_walk_0000.yrec");
    { std::ofstream(dir / "tree/sub/broken.yrec") << "not a recording\n"; }

    const yd::DemoDescription d = yd::read_description(kDescription);
    auto demo = yj::Project::open(dir / "demo/fraunces.yantra");
    const auto before = file_sha256(dir / "demo/fraunces.yantra");
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    yx::BatchOptions o;
    o.root = dir / "tree";
    o.output = dir / "out";
    o.mode = yx::BatchMode::Separate;
    o.project = demo.get();
    o.catalog = tetra_catalog(loader);
    o.metric = yd::demo_metric(*demo, d);
    o.scratch = dir / "scratch";
    std::vector<std::size_t> progress;
    o.progress = [&](std::size_t done, std::size_t total) {
        CHECK(total == 2);
        progress.push_back(done);
        return true;
    };
    const yx::BatchResult res = yx::batch_export(o);
    CHECK(progress == std::vector<std::size_t>{1, 2});
    CHECK(res.recordings == 2);
    REQUIRE(res.warnings.size() == 1);
    CHECK(res.warnings[0].find("broken.yrec") != std::string::npos);
    REQUIRE(res.files.size() == 2);
    // The scratch copy is gone; the project is not changed.
    CHECK(fs::is_empty(dir / "scratch"));
    CHECK(file_sha256(dir / "demo/fraunces.yantra") == before);

    std::size_t total = 0;
    for (const yx::BatchFile& f : res.files) {
        CAPTURE(f.path);
        REQUIRE(f.recordings.size() == 1);
        // Its floor: the demo walks are named after their floor (one walk per floor).
        std::string floor;
        for (const yd::DemoFloor& df : d.floors)
            if (f.path.stem().string().rfind(df.id + "_", 0) == 0) floor = df.id;
        REQUIRE(!floor.empty());
        yx::TableOptions to;
        to.metric = o.metric;
        const yx::Table want = yx::floor_table(*demo, floor, to);
        REQUIRE(want.rows.size() > 10);
        CHECK(f.rows == want.rows.size());
        const std::string got = file_bytes(f.path);
        CHECK(got == yx::csv_text(want.rows, o.csv));
        total += f.rows;
    }
    CHECK(res.rows == total);

    // Merged: one file, the recordings in folder order.
    o.mode = yx::BatchMode::Merged;
    o.output = dir / "merged.csv";
    o.progress = {};
    const yx::BatchResult merged = yx::batch_export(o);
    REQUIRE(merged.files.size() == 1);
    CHECK(merged.files[0].recordings.size() == 2);
    CHECK(merged.rows == total);
    const yx::CsvTable back = yx::read_csv(file_bytes(dir / "merged.csv"));
    CHECK(back.rows.size() == total);
    // Positions in metres, never -999 for a placed recording.
    const std::size_t xm = *back.column("x_m");
    for (const auto& r : back.rows) CHECK(r[xm] != yx::kInvalidValue);

    // Cancel after the first recording.
    o.progress = [](std::size_t, std::size_t) { return false; };
    CHECK_THROWS_AS(yx::batch_export(o), yx::ExportError);
    CHECK(fs::is_empty(dir / "scratch"));
    loader.unload_all();
}

TEST_CASE("EXP-04 batch export without a project: one row per replayed cycle, no position; an exporter plugin's format") {
    if (!demo_built()) return;
    const fs::path dir = scratch("export_batch_cycles");
    fs::copy(fs::path(DEMO_FRAUNCES_DIR) / "recordings", dir / "tree", fs::copy_options::recursive);
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    yx::BatchOptions o;
    o.root = dir / "tree";
    o.output = dir / "out";
    o.catalog = tetra_catalog(loader);
    const yx::FoundRecordings found = yx::find_recordings(o.root);
    REQUIRE(found.groups.size() == 2);
    CHECK(found.warnings.empty());
    const yx::BatchResult res = yx::batch_export(o);
    REQUIRE(res.files.size() == 2);
    for (std::size_t i = 0; i < res.files.size(); ++i) {
        const yx::BatchFile& f = res.files[i];
        CAPTURE(f.path);
        const auto rows = yx::cycle_rows(found.groups[i], o.catalog);
        REQUIRE(rows.size() == f.rows);
        REQUIRE(rows.size() > 10);
        for (const yx::Row& r : rows) {
            CHECK(r.kind == "cycle");
            CHECK_FALSE(r.plan);
            CHECK(r.serving);
            CHECK(r.utc_ns > 0);
        }
        const yx::CsvTable back = yx::read_csv(file_bytes(f.path));
        CHECK(back.rows.size() == f.rows);
        for (const auto& r : back.rows) CHECK(r[*back.column("x_m")] == yx::kInvalidValue);
        // The floor of the walk is known from the recording.
        CHECK(back.rows.front()[*back.column("floor")] != "");
    }

    // The test exporter plugin (PLG-02) offered in batch, in place of CSV.
    const yp::PluginEntry e = loader.load_file(ROWS_EXPORTER_PATH);
    REQUIRE(e.status == yp::PluginStatus::Enabled);
    const auto formats = yx::exporter_formats(loader);
    REQUIRE(formats.size() == 1);
    o.exporter = formats[0];
    o.mode = yx::BatchMode::Merged;
    o.output = dir / "all.txt";
    const yx::BatchResult plug = yx::batch_export(o);
    const std::string text = file_bytes(dir / "all.txt");
    CHECK(text.rfind("format rows_text\n", 0) == 0);
    CHECK(line_count(text) == 4 + plug.rows + 1);
    CHECK(text.find("rows " + std::to_string(plug.rows) + "\n") != std::string::npos);

    // Bad options.
    yx::BatchOptions bad = o;
    bad.catalog = nullptr;
    CHECK_THROWS_AS(yx::batch_export(bad), yx::ExportError);
    bad = o;
    bad.root = dir / "missing";
    CHECK_THROWS_AS(yx::batch_export(bad), yx::ExportError);
    loader.unload_all();
}

TEST_CASE("EXP-04 Project::write_copy keeps the open project and refuses an existing file; the header line parses") {
    if (!demo_built()) return;
    const fs::path dir = scratch("export_write_copy");
    fs::copy(DEMO_FRAUNCES_DIR, dir / "demo", fs::copy_options::recursive);
    auto demo = yj::Project::open(dir / "demo/fraunces.yantra");
    demo->write_copy(dir / "copy.yantra");
    CHECK(demo->path() == dir / "demo/fraunces.yantra");
    CHECK_THROWS_AS(demo->write_copy(dir / "copy.yantra"), yj::ProjectError);
    demo->write_copy(dir / "copy.yantra", true);
    {
        auto copy = yj::Project::open(dir / "copy.yantra");
        CHECK(copy->floors().size() == demo->floors().size());
        CHECK(copy->sessions().size() == demo->sessions().size());
        CHECK(copy->recordings().size() == demo->recordings().size());
    }
    for (const auto& r : demo->recordings()) {
        const auto h = yantra::recording::parse_header_line(r.header);
        REQUIRE(h.value);
        CHECK(h.value->session_id == r.session_id);
    }
    CHECK_FALSE(yantra::recording::parse_header_line("{}").value);
}
