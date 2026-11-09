// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-fuzz-seeds (TST-12): writes the seed corpus of every fuzz target
// at test time, from Yantra's own writers and the repository's own test
// data (no third-party or customer files):
//   yantra-fuzz-seeds <corpus folder> <source folder>
// <corpus>/<target>/ gets valid inputs of that target's format: project
// files (every schema version and a full current one), recordings (plain
// and zstd), record files, cell database CSV, calibration tables,
// buildings, plan images in every stb_image format and PDFs, export CSV
// and DXF, command lines and acceptance profiles. The fuzz ctests replay
// them with seeded mutations (replay_main.cpp).

#include "plan_fixtures.h"

#include "yantra/core/building/model_json.h"
#include "yantra/core/celldb/csv.h"
#include "yantra/core/measure/record_file.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/recording/reader.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/control/command.h"
#include "yantra/core/export/dxf.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/project/project.h"
#include "yantra/core/recording/writer.h"
#include "yantra/core/survey/acceptance.h"

#include <yantra/radio.h>

#include <cmath>
#include <sstream>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <span>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace rec = yantra::recording;
namespace pj = yantra::project;
namespace fx = yantra::plan::fixtures;

namespace {

constexpr std::int64_t kSecond = 1'000'000'000;

void write(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + file.string());
}

void write(const fs::path& file, const std::vector<std::uint8_t>& bytes) {
    write(file, std::string(bytes.begin(), bytes.end()));
}

void copy_seed(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

// -- project ---------------------------------------------------------------

void project_seeds(const fs::path& dir) {
    fs::create_directories(dir);
    for (int v = 1; v <= pj::kSchemaVersion; ++v)
        pj::testing::create_at_version(dir / ("v" + std::to_string(v) + ".yantra"), v, {"Seed", "Site", "", ""});
    const fs::path work = dir.parent_path() / "work-project";
    fs::remove_all(work);
    fs::create_directories(work);
    const fs::path png = work / "plan.png";
    write(png, fx::png(fx::quadrants(64, 48)));
    {
        auto p = pj::Project::create(work / "full.yantra", {"Seed project", "Site", "Operator", "notes"});
        const auto src = p->add_source(png);
        yantra::building::Floor f;
        f.name = "Ground floor";
        f.level = 0;
        f.source = src.id;
        const std::string fid = p->add_floor(f);
        yantra::building::Floor g;
        g.name = "First floor";
        g.level = 1;
        (void)p->add_floor(g);
        const auto classes = p->wall_classes();
        if (!classes.empty()) (void)p->add_wall(fid, {"", classes.front().id, {{1, 1}, {30, 1}, {30, 20}}, {}});
        (void)p->add_wall(fid, {"", "", {{5, 5}, {5, 40}}, 7.5});
        yantra::building::Antenna a;
        a.position = {10, 10};
        (void)p->add_antenna(fid, a);
        p->set_acceptance_profiles({});
        p->save();
        p->close();
    }
    copy_seed(work / "full.yantra", dir / "full.yantra");
    fs::remove_all(work);
}

// -- recording -------------------------------------------------------------

const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"}, {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"}};
const yr_field_def f_freq = {sizeof(yr_field_def), YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, YR_FIELD_FREQUENCY_HZ,
                             "Frequency", "Hz", "Carrier", nullptr, 0, 0};
const yr_field_def f_role = {sizeof(yr_field_def), YR_TYPE_ENUM, YR_GROUP_CORE, 0, YR_FIELD_ROLE,
                             "Role", "", "Role", kRoles, 2, 0};
const yr_field_def f_level = {sizeof(yr_field_def), YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, YR_FIELD_LEVEL,
                              "Level", "dBFS", "Level", nullptr, 0, 0};
const yr_field_def f_la = {sizeof(yr_field_def), YR_TYPE_UINT, YR_GROUP_CORE, 0, YR_FIELD_LA,
                           "LA", "", "LA", nullptr, 0, 0};
const yr_field_def* const kFields[] = {&f_freq, &f_role, &f_level, &f_la};
const yr_schema kCell = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 4, kFields, 0, 0};

ym::RadioRecord cell(std::int64_t t, std::uint64_t hz, double level, std::uint32_t role) {
    ym::RadioRecord r;
    r.plugin_id = "seed.radio";
    r.plugin_version = "1.0.0";
    r.technology_id = "xtech";
    r.device_key = "dev:0";
    r.stream_id = 1;
    r.schema_index = 0;
    r.schema_kind = kCell.kind;
    r.radio_schema = &kCell;
    r.record.schema_id = kCell.id;
    r.record.schema_version = kCell.version;
    r.record.time.monotonic_ns = t;
    r.record.time.utc_ns = t + 1'790'000'000 * kSecond;
    r.record.time.utc_source = 1;
    r.record.values.resize(4);
    r.record.values[0] = hz;
    r.record.values[1] = yp::EnumValue{role};
    r.record.values[2] = level;
    r.record.values[3] = std::uint64_t{17};
    return r;
}

void recording_seeds(const fs::path& dir) {
    for (const rec::Compression comp : {rec::Compression::None, rec::Compression::Zstd}) {
        const fs::path work = dir.parent_path() / "work-recording";
        fs::remove_all(work);
        auto clock = std::make_shared<yantra::platform::ManualClock>(100 * kSecond, 1'790'000'000 * kSecond);
        ym::MeasurementPipeline::Options o;
        o.clock = clock;
        auto p = std::make_shared<ym::MeasurementPipeline>(std::move(o));
        rec::RecorderConfig c;
        c.folder = work;
        c.compression = comp;
        c.keyframe_s = 2;
        c.flush_thread = false;
        c.session_id = "00112233445566ff";
        c.info.notes = "fuzz seed";
        auto r = rec::Recorder::create(p, c);
        std::string error;
        if (!r->start("seed", error)) throw std::runtime_error(error);
        r->set_floor("f1", "Ground floor");
        std::int64_t t = 100 * kSecond;
        for (int i = 0; i < 24; ++i) {
            t += kSecond / 4;
            clock->set(t);
            p->on_radio_record(cell(t - 1'000'000, 390'012'500, -55.0 - i, YR_ROLE_SERVING));
            if (i % 2 == 0) p->on_radio_record(cell(t - 500'000, 390'037'500, -80.0 + i * 0.5, YR_ROLE_NEIGHBOUR));
            if (i % 5 == 0) r->add_position({0, "f1", 1.0 * i, 2.0 * i, std::nullopt, std::nullopt, "click", ""});
            if (i == 10) (void)r->marker("door");
            while (p->next_cycle_ns() <= t) {
                clock->set(p->next_cycle_ns());
                p->tick();
            }
            clock->set(t);
        }
        if (!r->flush(error)) throw std::runtime_error(error);
        r->stop();
        r->detach();
        const std::string name = rec::file_name("seed", 0, comp);
        copy_seed(work / name, dir / name);
        fs::remove_all(work);
    }
}

// -- small text formats ----------------------------------------------------

void calibration_seeds(const fs::path& dir) {
    yantra::calibration::Table t;
    yantra::calibration::Entry e;
    e.key = {"hackrf", "0000000000000001", {{"gain_amp", "0"}, {"gain_lna", "24"}}};
    e.points = {{380e6, 12.5, -60.0, -72.5, 0.3}, {400e6, 13.25, -60.0, -73.25, 0.4}, {420e6, 14.0, -60, -74, 0.2}};
    e.created_unix = 1'790'000'000;
    e.radio_plugin_id = "hackrf";
    t.put(e);
    yantra::calibration::Entry s = e;
    s.key = {"simulator", "", {}};
    s.points = {{390e6, 0.0, -50.0, -50.0, 0.0}};
    t.put(s);
    write(dir / "table.json", t.to_json());
    write(dir / "empty.json", yantra::calibration::Table{}.to_json());
}

void command_seeds(const fs::path& dir) {
    std::string text, json;
    for (const auto& info : yantra::control::command_table()) {
        yantra::control::Command c;
        c.id = info.id;
        for (std::size_t i = 0; i < info.args.size(); ++i) c.args[info.args[i]] = i == 0 ? "a b \"c\"" : "1";
        text += c.to_text() + "\n";
        json += c.to_json().dump() + "\n";
    }
    write(dir / "text.txt", text);
    write(dir / "json.txt", json);
    write(dir / "words.txt", "record start walk\nrecord.start name=\"a b\"\nstatus\nquit\n");
}

void acceptance_seeds(const fs::path& dir) {
    write(dir / "default.json", yantra::survey::profile_json(yantra::survey::default_acceptance_profile()).dump(2));
}

void export_seeds(const fs::path& dir) {
    yantra::exports::DxfDrawing d;
    d.comments = {"Yantra fuzz seed"};
    d.layers = {{"WALLS", 7, "walls"}, {"POINTS", 3, "-80 dBm"}};
    d.points = {{"POINTS", 1.5, -2.5}, {"POINTS", 3.0, -4.0}};
    d.texts = {{"POINTS", 1.6, -2.4, 0.2, "-80.5"}};
    d.polylines = {{"WALLS", {{"WALLS", 0, 0}, {"WALLS", 10, 0}, {"WALLS", 10, -5}}}};
    write(dir / "floor.dxf", yantra::exports::dxf_text(d));
    write(dir / "points.csv", "time,floor,x_m,y_m,level_dbm\n2026-11-02T10:00:00Z,Ground,1.5,2.5,-80.5\n"
                              "2026-11-02T10:00:01Z,\"Ground, east\",3,4,-81\n");
    write(dir / "points_semicolon.csv", "time;floor;x_m;y_m;level_dbm\n2026-11-02T10:00:00Z;Ground;1,5;2,5;-80,5\n");
}

void plan_seeds(const fs::path& dir) {
    const yantra::plan::Raster r = fx::quadrants(24, 16);
    write(dir / "plan.png", fx::png(r));
    write(dir / "plan_dpi.png", fx::png_with_dpi(r, 150));
    write(dir / "plan.bmp", fx::bmp(r));
    write(dir / "plan.tga", fx::tga(r, 4, true));
    write(dir / "plan.jpg", fx::jpeg(r));
    write(dir / "plan_exif.jpg", fx::jpeg_with_exif(r, 6));
    write(dir / "plan.hdr", fx::hdr(r));
    write(dir / "plan.pgm", fx::pnm(r, 1));
    write(dir / "plan.psd", fx::psd(r));
    write(dir / "plan.gif", fx::gif(r));
    fx::PdfPage a, b;
    a.rects = {{50, 50, 200, 100, 0, 0, 0}};
    b.width = 842;
    b.height = 595;
    b.rotate = 90;
    write(dir / "plan.pdf", fx::pdf({a, b}));
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Every seed must be accepted by its parser, so the mutations start from
// valid inputs (not only from the error paths).
void check_seeds(const fs::path& out) {
    auto each = [&](const std::string& target, const std::function<bool(const fs::path&)>& ok) {
        std::size_t n = 0;
        for (const auto& e : fs::directory_iterator(out / target)) {
            if (!e.is_regular_file()) continue;
            if (!ok(e.path())) throw std::runtime_error("seed refused by its parser: " + e.path().string());
            ++n;
        }
        if (n == 0) throw std::runtime_error("no seeds for " + target);
    };
    each("project", [](const fs::path& p) { return pj::Project::open(p) != nullptr; });
    each("recording", [](const fs::path& p) { return rec::parse_bytes(slurp(p), p.filename().string()).ok(); });
    each("record_file", [](const fs::path& p) {
        std::istringstream in(slurp(p));
        return ym::parse_record_file(in, p.filename().string()).ok();
    });
    each("celldb_csv", [](const fs::path& p) {
        yantra::celldb::CsvOptions o;
        o.hex_fields = {"la"};
        return !yantra::celldb::import_csv(slurp(p), o).entries.empty();
    });
    each("calibration", [](const fs::path& p) {
        yantra::calibration::Table t;
        std::string e;
        return yantra::calibration::Table::parse(slurp(p), t, e);
    });
    each("building_scene", [](const fs::path& p) {
        yantra::building::Building b;
        std::string e;
        return yantra::building::parse_building(slurp(p), b, e);
    });
    each("plan_source", [](const fs::path& p) {
        const std::string b = slurp(p);
        if (p.extension() == ".pdf" && !yantra::plan::pdf_supported()) return true;
        const auto* d = reinterpret_cast<const std::uint8_t*>(b.data());
        return yantra::plan::open_plan_source(std::span<const std::uint8_t>(d, b.size()), p.filename().string()) !=
               nullptr;
    });
    each("command", [](const fs::path& p) {
        std::istringstream in(slurp(p));
        std::string line;
        while (std::getline(in, line))
            if (!yantra::control::parse_command_any(line)) return false;
        return true;
    });
    each("acceptance", [](const fs::path& p) {
        (void)yantra::survey::profile_from_json("seed", nlohmann::json::parse(slurp(p)));
        return true;
    });
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: yantra-fuzz-seeds <corpus folder> <source folder>\n";
        return 2;
    }
    try {
        const fs::path out = argv[1];
        const fs::path src = argv[2];
        fs::remove_all(out);
        project_seeds(out / "project");
        recording_seeds(out / "recording");
        copy_seed(src / "tests/plugin_kit/data/ctest.records.jsonl", out / "record_file/ctest.records.jsonl");
        copy_seed(src / "tests/plugin_kit/data/rogue_field.records.jsonl", out / "record_file/rogue_field.records.jsonl");
        copy_seed(src / "data/demo/fraunces/cells.csv", out / "celldb_csv/fraunces.csv");
        copy_seed(src / "data/demo/mak/cells.csv", out / "celldb_csv/mak.csv");
        calibration_seeds(out / "calibration");
        copy_seed(src / "tests/fixtures/building/example_office.building.json",
             out / "building_scene/example_office.building.json");
        plan_seeds(out / "plan_source");
        export_seeds(out / "export_readers");
        command_seeds(out / "command");
        acceptance_seeds(out / "acceptance");
        check_seeds(out);
        std::cout << "seeds written to " << out.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "yantra-fuzz-seeds: " << e.what() << "\n";
        return 1;
    }
}
