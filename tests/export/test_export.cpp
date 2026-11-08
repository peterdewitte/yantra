// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Export (docs/EXPORT.md): the rows of a floor are its shared point list
// (IND-30); CSV with selectable columns, separator, UTC/local time, N
// neighbours, markers, antenna correction and -999 (EXP-01); DXF of a
// floor with points, walls and antennas in metres (EXP-03); TST-33 CSV
// and DXF round trips (read back what was written, plan positions
// unchanged); the floor image with legend (EXP-08); the C test exporter
// plugin through the public ABI (PLG-02, offered in batch export).

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "survey_helpers.h"
#include "yantra/core/building/model.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/export/csv.h"
#include "yantra/core/export/dxf.h"
#include "yantra/core/export/image.h"
#include "yantra/core/export/plugin_export.h"
#include "yantra/core/export/table.h"
#include "yantra/core/plugin/loader.h"

using namespace survey_test;
namespace yx = yantra::exports;
namespace yc = yantra::celldb;

namespace {

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

double to_double(const std::string& s) {
    std::size_t used = 0;
    const double v = std::stod(s, &used);
    REQUIRE(used == s.size());
    return v;
}

// A cycle with a serving carrier (cell 1, MER), two measured neighbours
// (cells 2 and 3; cell 3 without C1) and a neighbour-list entry.
ym::MeasurementCycle rich_cycle(std::int64_t t, double serving_level) {
    ym::MeasurementCycle c =
        cycle(t, {Carrier{}.w_level(serving_level).w_c1(serving_level + 110),
                  Carrier{}.w_role(ym::CarrierRole::Neighbour).w_cell(2).w_hz(390'025'000).w_level(-85).w_c1(25),
                  Carrier{}.w_role(ym::CarrierRole::Neighbour).w_cell(3).w_hz(390'050'000).w_level(-80),
                  Carrier{}.w_role(ym::CarrierRole::NeighbourEntry).w_cell(4).w_hz(390'075'000).w_entry(1)});
    c.samples[0].latest.quality = {{"mer_db", "dB", 21.5 + static_cast<double>(t / kS)}};
    return c;
}

// A floor with a walk of `n` points from (13.37, 7.1) to (391.9, 177.3)
// plan units, a wall and an antenna; and a manual point with a label that
// needs quoting.
struct ExportFixture : Fixture {
    explicit ExportFixture(const std::string& name, int n = 5) : Fixture(name) {
        auto w = walk();
        w->start(0);
        w->click(0, {13.37, 7.1});
        for (int i = 1; i <= n; ++i) w->on_cycle(rich_cycle(i * kS, -60.0 - 3.3 * i));
        w->click((n + 1) * kS, {391.9, 177.3});
        w->finish((n + 2) * kS);
        auto m = this->walk("manual");
        m->start(100 * kS);
        m->point(100 * kS, {200.25, 100.125}, 2.0, "Room 1, \"bar\"");
        m->on_cycle(rich_cycle(101 * kS, -101));
        m->on_cycle(rich_cycle(102 * kS, -101));
        m->on_cycle(rich_cycle(103 * kS, -101));
        m->finish(104 * kS);
        bm::Wall wall;
        wall.wall_class = "brick";
        wall.points = {{10.5, 20.25}, {300.125, 20.25}, {300.125, 150.0}};
        p->add_wall(ground, wall);
        bm::Antenna a;
        a.name = "DAS 1";
        a.position = {123.4, 56.7};
        p->add_antenna(ground, a);
    }
};

}  // namespace

TEST_CASE("EXP-01 IND-30 the rows of a floor are its point list: count, order, positions, values, colours") {
    ExportFixture fx("export_rows");
    const ys::PointList list = ys::point_list(*fx.p, ys::PointQuery{fx.ground, {}, {}, std::nullopt});
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    REQUIRE(t.rows.size() == list.count());
    REQUIRE(list.count() == 6);
    for (std::size_t i = 0; i < list.count(); ++i) {
        CAPTURE(i);
        CHECK(t.rows[i].plan == list.points[i].plan);
        CHECK(t.rows[i].metres == list.points[i].metres);
        CHECK(t.rows[i].value == list.points[i].value);
        CHECK(t.rows[i].rgb == list.points[i].rgb);
        CHECK(t.rows[i].session_id == list.points[i].session_id);
        CHECK(t.rows[i].sequence == list.points[i].sequence);
    }
    const yx::Row& r = t.rows[1];
    REQUIRE(r.serving);
    CHECK(r.serving->level == doctest::Approx(-66.6));
    CHECK(r.serving->unit == "dBm");
    CHECK(r.serving->c1 == doctest::Approx(43.4));
    CHECK(r.serving->quality_value("mer_db") == doctest::Approx(23.5));
    REQUIRE(r.serving->cell_value("cell_id") != nullptr);
    CHECK(*r.serving->cell_value("cell_id") == "1");
    // Measured neighbours by level, then the list entry.
    REQUIRE(r.neighbours.size() == 3);
    CHECK(*r.neighbours[0].cell_value("cell_id") == "3");
    CHECK(*r.neighbours[1].cell_value("cell_id") == "2");
    CHECK(r.neighbours[2].role == "neighbour_entry");
    CHECK(t.rows.back().label == "Room 1, \"bar\"");
    CHECK(t.rows.back().kind == "manual");
    CHECK_THROWS_AS(yx::floor_table(*fx.p, "nope"), yx::ExportError);
}

TEST_CASE("EXP-01 CSV: columns, separator, -999, neighbours, channel numbers, UTC and local time") {
    ExportFixture fx("export_csv");
    yc::Technology tech;
    tech.id = "test";
    tech.bands = {yp::Band{"b", "Band", 390'000'000, 399'987'500, 12'500, 3600}};
    const yc::Technologies techs{tech};
    yx::TableOptions to;
    to.technologies = &techs;
    const yx::Table t = yx::floor_table(*fx.p, fx.ground, to);

    yx::CsvOptions o;
    o.separator = ';';
    o.neighbours = 2;
    const std::string text = yx::csv_text(t.rows, o);
    std::istringstream in(text);
    std::string header, first, second;
    std::getline(in, header);
    std::getline(in, first);
    std::getline(in, second);
    CHECK(header ==
          "time_utc;floor;x_m;y_m;cell_id;channel;rssi;rssi_unit;c1_db;mer_db;serving_change;"
          "nb1_cell_id;nb1_channel;nb1_rssi;nb1_c1_db;nb2_cell_id;nb2_channel;nb2_rssi;nb2_c1_db;marker;"
          "antenna_correction_db");
    const yx::Row& r0 = t.rows[0];
    CHECK(first == "2026-10-26T07:33:21.000Z;Ground floor;" + yx::number_text(r0.metres->x) + ";" +
                       yx::number_text(r0.metres->y) + ";1;3600;-63.3;dBm;46.7;22.5;0;3;3604;-80;-999;2;3602;-85;25;;-999");
    CHECK(second.rfind("2026-10-26T07:33:22.000Z;", 0) == 0);
    CHECK(std::count(text.begin(), text.end(), '\n') == static_cast<long>(t.rows.size() + 1));

    // Selected columns in a chosen order, tab separated, no header.
    yx::CsvOptions sel;
    sel.columns = {yx::Column::Level, yx::Column::PlanPosition, yx::Column::Frequency, yx::Column::Kind};
    sel.separator = '\t';
    sel.header = false;
    sel.neighbours = 0;
    const yx::CsvTable back = yx::read_csv(yx::csv_text(t.rows, sel), '\t');
    CHECK(back.header == std::vector<std::string>{"-63.3", "dBm", yx::number_text(r0.plan->x), yx::number_text(r0.plan->y),
                                                  "390000000", "walk"});
    CHECK(back.rows.size() == t.rows.size() - 1);
    CHECK(yx::csv_layout(t.rows, sel).header ==
          std::vector<std::string>{"rssi", "rssi_unit", "x_plan", "y_plan", "frequency_hz", "kind"});

    // Local time with its offset.
    const char* old_tz = std::getenv("TZ");
    const std::string saved = old_tz != nullptr ? old_tz : "";
    setenv("TZ", "CET-1", 1);
    tzset();
    CHECK(yx::format_time(r0.utc_ns, true) == "2026-10-26T08:33:21.000+01:00");
    yx::CsvOptions local;
    local.local_time = true;
    CHECK(yx::csv_layout(t.rows, local).header.front() == "time_local");
    if (old_tz != nullptr) setenv("TZ", saved.c_str(), 1);
    else unsetenv("TZ");
    tzset();

    // A row without any value: -999 everywhere but the flags and the marker.
    yx::Row empty;
    empty.floor_id = "f";
    empty.floor_name = "F";
    const yx::CsvLayout l = yx::csv_layout({empty}, o);
    const auto cells = yx::csv_values(empty, l, o);
    REQUIRE(cells.size() == l.header.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        CAPTURE(l.header[i]);
        if (l.header[i] == "time_utc") CHECK(cells[i] == "1970-01-01T00:00:00.000Z");
        else if (l.header[i] == "floor") CHECK(cells[i] == "F");
        else if (l.header[i] == "serving_change") CHECK(cells[i] == "0");
        else if (l.header[i] == "marker") CHECK(cells[i].empty());
        else CHECK(cells[i] == yx::kInvalidValue);
    }

    // Bad options are refused.
    yx::CsvOptions bad;
    bad.columns = {yx::Column::Time, yx::Column::Time};
    CHECK_THROWS_AS(yx::csv_text(t.rows, bad), yx::ExportError);
    bad = {};
    bad.separator = '.';
    CHECK_THROWS_AS(yx::csv_text(t.rows, bad), yx::ExportError);
    bad = {};
    bad.neighbours = 33;
    CHECK(!yx::validate(bad).empty());
    for (yx::Column c : yx::all_columns()) CHECK(yx::column_from_id(yx::column_id(c)) == c);
}

TEST_CASE("EXP-01 markers go to the next row, the antenna correction is the serving radio's") {
    std::vector<yx::Row> rows(3);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].time_ns = static_cast<std::int64_t>(i + 1) * kS;
        rows[i].serving = yx::CarrierValues{};
        rows[i].serving->device = i == 2 ? "other:0" : "fake:0";
    }
    yx::RecordingExtras x;
    x.markers = {{kS / 2, 1, "entrance", yantra::recording::MarkerAction::Mark},
                 {kS, 2, "", yantra::recording::MarkerAction::Mark},
                 {kS + 1, 3, "stairs", yantra::recording::MarkerAction::Mark},
                 {9 * kS, 4, "end", yantra::recording::MarkerAction::Mark}};
    x.antenna = {{"", "", 2.0, 1.5, 0.0}, {"fake", "fake:0", 5.0, 3.0, 10.0}};
    yx::apply_extras(rows, x);
    CHECK(rows[0].marker == "M1 entrance; M2");
    CHECK(rows[1].marker == "M3 stairs");
    CHECK(rows[2].marker == "M4 end");
    CHECK(rows[0].antenna_correction_db == doctest::Approx(8.0));   // -5 + 3 + 10
    CHECK(rows[2].antenna_correction_db == doctest::Approx(-0.5));  // the all-devices entry
    CHECK_FALSE(yx::antenna_offset({}, "fake:0"));
}

TEST_CASE("TST-33 CSV round trip: what is read back is what was written, plan positions unchanged") {
    ExportFixture fx("export_csv_round_trip");
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    for (char sep : {',', ';', '\t', '|'}) {
        CAPTURE(sep);
        yx::CsvOptions o;
        o.columns = yx::all_columns();
        o.separator = sep;
        const fs::path file = scratch("export_csv_round_trip_files") / "floor.csv";
        yx::write_csv(file, t.rows, o);
        const yx::CsvTable back = yx::read_csv(read_text(file), sep);
        const yx::CsvLayout l = yx::csv_layout(t.rows, o);
        CHECK(back.header == l.header);
        REQUIRE(back.rows.size() == t.rows.size());
        const std::size_t xm = *back.column("x_m"), ym = *back.column("y_m");
        const std::size_t xp = *back.column("x_plan"), yp_ = *back.column("y_plan");
        for (std::size_t i = 0; i < t.rows.size(); ++i) {
            CAPTURE(i);
            CHECK(back.rows[i] == yx::csv_values(t.rows[i], l, o));
            CHECK(to_double(back.rows[i][xm]) == t.rows[i].metres->x);
            CHECK(to_double(back.rows[i][ym]) == t.rows[i].metres->y);
            CHECK(to_double(back.rows[i][xp]) == t.rows[i].plan->x);
            CHECK(to_double(back.rows[i][yp_]) == t.rows[i].plan->y);
        }
        CHECK(back.rows.back()[*back.column("label")] == "Room 1, \"bar\"");
    }
    CHECK_THROWS_AS(yx::read_csv("a,b\n\"x,1\n"), yx::ExportError);
    CHECK_THROWS_AS(yx::read_csv("a,b\n1\n"), yx::ExportError);
    CHECK(yx::read_csv("a,b\r\n1,\"2\"\"\"\r\n").rows == std::vector<std::vector<std::string>>{{"1", "2\""}});
}

TEST_CASE("EXP-03 TST-33 DXF of a floor: points, walls and antennas in metres; read back unchanged") {
    ExportFixture fx("export_dxf");
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    std::size_t skipped = 99;
    const yx::DxfDrawing d = yx::floor_drawing(*fx.p, t, {}, &skipped);
    CHECK(skipped == 0);
    REQUIRE(d.points.size() == t.rows.size() + 1);  // + the antenna
    for (std::size_t i = 0; i < t.rows.size(); ++i) {
        CAPTURE(i);
        CHECK(yx::plan_metres(d.points[i]) == *t.rows[i].metres);
        CHECK(d.points[i].layer == yx::point_layer(t.scale, t.rows[i].value));
    }
    CHECK(d.points.back().layer == "ANTENNAS");
    CHECK(yx::plan_metres(d.points.back()) == bm::Point2{123.4 * 0.05, 56.7 * 0.05});
    REQUIRE(d.polylines.size() == 1);
    CHECK(d.polylines[0].layer == "WALLS_BRICK");
    REQUIRE(d.polylines[0].vertices.size() == 3);
    CHECK(yx::plan_metres(d.polylines[0].vertices[1]) == bm::Point2{300.125 * 0.05, 20.25 * 0.05});
    // -63.3 dBm is in the -70 band (4 of 4, ascending), -101 below every band.
    CHECK(d.points[0].layer == "POINTS_4");
    CHECK(d.points[t.rows.size() - 1].layer == "POINTS_OUTSIDE");

    const fs::path file = scratch("export_dxf_files") / "ground.dxf";
    yx::write_dxf(file, d);
    const std::string text = read_text(file);
    CHECK(text.rfind("999\n", 0) == 0);
    CHECK(text.find("AC1009") != std::string::npos);
    CHECK(text.substr(text.size() - 6) == "0\nEOF\n");
    const yx::DxfDrawing back = yx::read_dxf(text);
    CHECK(back == d);

    yx::DxfOptions plain;
    plain.value_text = false;
    plain.walls = false;
    plain.antennas = false;
    plain.label = "DEMO - simulated data";
    const yx::DxfDrawing p2 = yx::floor_drawing(*fx.p, t, plain);
    CHECK(p2.polylines.empty());
    CHECK(p2.points.size() == t.rows.size());
    REQUIRE(p2.texts.size() == 1);
    CHECK(p2.texts[0].layer == "LABEL");
    CHECK(yx::read_dxf(yx::dxf_text(p2)) == p2);

    // DXF is in metres: a floor without a scale is refused.
    bm::Floor f;
    f.name = "Attic";
    f.level = 2;
    f.plan_width = 100;
    f.plan_height = 100;
    const std::string attic = fx.p->add_floor(f);
    CHECK_THROWS_AS(yx::floor_drawing(*fx.p, yx::floor_table(*fx.p, attic)), yx::ExportError);
    CHECK_THROWS_AS(yx::read_dxf("0\nSECTION\n2"), yx::ExportError);
    CHECK_THROWS_AS(yx::read_dxf("x\nPOINT\n"), yx::ExportError);
}

TEST_CASE("EXP-08 floor image: every point in its colour, a legend with counts, PNG and JPEG") {
    ExportFixture fx("export_image");
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    yx::ImageOptions o;
    o.font_file = fs::path(YANTRA_SOURCE_DIR) / "data/fonts/Inter-Regular.ttf";
    o.banner = "DEMO - simulated data";
    const yx::ImageResult res = yx::floor_image(*fx.p, t, o);
    CHECK(res.warnings.empty());
    CHECK(res.points == t.rows.size());
    // The plan is 400 x 200 units drawn 1600 x 800, beside the legend.
    CHECK(res.canvas.width() > 1600 + 250);
    CHECK(res.canvas.height() >= 800);
    const auto legend = yx::legend_entries(t);
    REQUIRE(legend.size() == 6);
    std::size_t total = 0;
    for (const auto& e : legend) total += e.count;
    CHECK(total == t.rows.size());
    CHECK(legend[0].label == ">= -70 dBm");
    CHECK(legend[0].count == 3);  // -63.3, -66.6, -69.9
    CHECK(legend[4].label == "< -100 dBm");
    CHECK(legend[4].count == 1);

    // The colour of each point at its centre (margins 12 px, banner 34, title 28).
    const double scale = 1600.0 / 400.0;
    for (const yx::Row& r : t.rows) {
        const int x = static_cast<int>(12 + r.plan->x * scale), y = static_cast<int>(34 + 28 + 12 + r.plan->y * scale);
        CHECK(res.canvas.at(x, y) == yx::rgb(r.rgb));
    }

    const fs::path dir = scratch("export_image_files");
    yx::write_image_file(dir / "ground.png", res.canvas);
    yx::write_image_file(dir / "ground.JPG", res.canvas, 80);
    CHECK(read_text(dir / "ground.png").substr(0, 8) == "\x89PNG\r\n\x1a\n");
    CHECK(read_text(dir / "ground.JPG").substr(0, 2) == "\xff\xd8");
    CHECK_THROWS_AS(yx::write_image_file(dir / "ground.gif", res.canvas), yx::ExportError);

    yx::ImageOptions no_font;
    no_font.legend = false;
    const yx::ImageResult bare = yx::floor_image(*fx.p, t, no_font);
    CHECK(bare.warnings.size() == 1);
    CHECK(bare.canvas.width() == 1600 + 24);
    no_font.max_plan_px = 50;
    CHECK_THROWS_AS(yx::floor_image(*fx.p, t, no_font), yx::ExportError);
}

TEST_CASE("IND-43 floor image with a map layer: flat cells in their colours over the plan, points left out") {
    ExportFixture fx("export_image_map");
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    yx::ImageOptions o;
    o.legend = false;
    o.walls = false;
    o.antennas = false;
    o.points = false;
    // Two opaque cells over the whole 400 x 200 plan: red left, blue right.
    o.map = yx::MapLayer{{0, 0}, {400, 200}, 2, 1, {0xFFFF0000u, 0xFF0000FFu}};
    const yx::ImageResult res = yx::floor_image(*fx.p, t, o);
    CHECK(res.points == 0);
    const int mid_y = res.canvas.height() - 12 - 400;  // the plan is 1600 x 800 px, margin 12
    CHECK(res.canvas.at(12 + 400, mid_y) == yx::rgb(0xFF0000));
    CHECK(res.canvas.at(12 + 1200, mid_y) == yx::rgb(0x0000FF));
    // A fully transparent cell leaves the plan as it is.
    o.map->argb = {0x00FF0000u, 0xFF0000FFu};
    const yx::ImageResult clear = yx::floor_image(*fx.p, t, o);
    CHECK(clear.canvas.at(12 + 400, mid_y) != yx::rgb(0xFF0000));
    CHECK(clear.canvas.at(12 + 1200, mid_y) == yx::rgb(0x0000FF));
    o.map->argb = {0xFFFF0000u};
    CHECK_THROWS_AS(yx::floor_image(*fx.p, t, o), yx::ExportError);
}

TEST_CASE("PLG-02 EXP-04 an exporter plugin through the public C ABI: offered in batch, gets the rows table") {
    ExportFixture fx("export_plugin");
    const yx::Table t = yx::floor_table(*fx.p, fx.ground);
    yp::PluginLoader loader(yp::PluginLoaderConfig{});
    const yp::PluginEntry e = loader.load_file(ROWS_EXPORTER_PATH);
    REQUIRE_MESSAGE(e.status == yp::PluginStatus::Enabled, e.message);
    const auto batch = yx::exporter_formats(loader);
    REQUIRE(batch.size() == 1);
    CHECK(batch[0].format.id == "rows_text");
    CHECK(batch[0].plugin_id == "org.example.rows_exporter");
    CHECK(yx::exporter_formats(loader, false).size() == 2);

    yx::CsvOptions cols;
    cols.neighbours = 1;
    const fs::path file = scratch("export_plugin_files") / "rows.txt";
    yx::run_exporter(batch[0], file, t.rows, cols, "detail=1");
    std::istringstream in(read_text(file));
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    REQUIRE(lines.size() == 4 + t.rows.size() + 1);
    CHECK(lines[0] == "format rows_text");
    CHECK(lines[1] == "options detail=1");
    CHECK(lines[2] == "table yantra yantra_export_rows yantra_export_rows 1");
    const yx::CsvLayout l = yx::csv_layout(t.rows, cols);
    std::string fields;
    const yp::Schema schema = yx::rows_schema(l, cols, t.rows);
    REQUIRE(schema.fields.size() == l.header.size());
    for (std::size_t i = 0; i < schema.fields.size(); ++i) CHECK(schema.fields[i].name == l.header[i]);
    CHECK(lines[3].rfind("time_utc:string:\tfloor:string:\tx_m:double:m\ty_m:double:m\tcell_id:string:\t", 0) == 0);
    CHECK(lines[3].find("rssi:double:dBm") != std::string::npos);
    CHECK(lines[3].find("mer_db:double:dB") != std::string::npos);
    CHECK(lines[3].find("serving_change:bool:") != std::string::npos);
    // The first row: the CSV values typed; -999 is absent ("-").
    std::vector<std::string> v;
    std::istringstream row(lines[4]);
    for (std::string c; std::getline(row, c, '\t');) v.push_back(c);
    REQUIRE(v.size() == l.header.size() + 1);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", t.rows[0].metres->x);
    CHECK(v[2] == buf);
    CHECK(v[1] == "Ground floor");
    CHECK(v[static_cast<std::size_t>(std::find(l.header.begin(), l.header.end(), "nb1_c1_db") - l.header.begin())] == "-");
    CHECK(v.back() == "@" + std::to_string(t.rows[0].utc_ns));
    CHECK(lines.back() == "rows " + std::to_string(t.rows.size()));

    // A format the plugin does not know is refused (begin fails): nothing written.
    yx::ExporterFormat wrong = batch[0];
    wrong.format.id = "unknown";
    CHECK_THROWS_AS(yx::run_exporter(wrong, file.parent_path() / "x.txt", t.rows, cols), yx::ExportError);
    CHECK_FALSE(fs::exists(file.parent_path() / "x.txt"));
    loader.unload_all();
}
