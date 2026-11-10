// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cell database (DB-01..05, DB-08, TST-33): model and validation, CSV round
// trips and malformed rows, verification and the difference log,
// auto-learn, the cross-check and hand-computed reselection cases with the
// TETRA plugin. The survey-level cases (simulator walks) are in
// tests/measure_host/test_measure_celldb.cpp.

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <yantra/core/celldb/csv.h>
#include <yantra/core/celldb/database.h>
#include <yantra/core/celldb/network.h>
#include <yantra/core/celldb/reselection.h>
#include <yantra/core/celldb/technology.h>
#include <yantra/core/celldb/verify.h>
#include <yantra/core/plugin/loader.h>

namespace fs = std::filesystem;
namespace yc = yantra::celldb;
namespace yp = yantra::plugin;
namespace ym = yantra::measure;

namespace {

constexpr std::int64_t MHz = 1'000'000;

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// A technology made up for the test: cells keyed by frequency and area,
// one VERIFY parameter, a neighbour schema linked by frequency; no plugin
// rule functions (exact comparison, channels from its band).
yc::Technology fake_technology() {
    yc::Technology t;
    t.id = "faketech";
    t.label = "Fake";
    t.plugin_version = "1.2.3";
    t.cell_schema.id = "fake_cell";
    auto field = [](yp::Schema& s, std::string name, yp::FieldType type, bool key, bool verify = false) {
        yp::Field f;
        f.name = std::move(name);
        f.type = type;
        f.key = key;
        f.verify = verify;
        s.fields.push_back(f);
    };
    field(t.cell_schema, "frequency_hz", yp::FieldType::UInt, true);
    field(t.cell_schema, "la", yp::FieldType::UInt, true);
    field(t.cell_schema, "colour_code", yp::FieldType::UInt, false, true);
    field(t.cell_schema, "power_dbm", yp::FieldType::Int, false, true);
    field(t.cell_schema, "level", yp::FieldType::Double, false);
    yp::Schema n;
    n.id = "fake_neighbour";
    n.kind = yp::SchemaKind::Neighbour;
    field(n, "frequency_hz", yp::FieldType::UInt, true);
    field(n, "entry_index", yp::FieldType::UInt, true);
    field(n, "entry_count", yp::FieldType::UInt, false);
    field(n, "neighbour_frequency_hz", yp::FieldType::UInt, false);
    t.neighbour_schema = n;
    t.neighbour_schema_index = 1;
    t.links = {yp::FieldLink{"neighbour_frequency_hz", "frequency_hz"}};
    t.bands = {yp::Band{"b", "B", 400 * MHz, 401 * MHz, 25'000, 100}};
    return t;
}

// The TETRA plugin, loaded once.
struct Tetra {
    yp::PluginLoader loader{yp::PluginLoaderConfig{}};
    yc::Technology tech;
    Tetra() {
        const yp::PluginEntry e = loader.load_file(TETRA_PLUGIN_PATH);
        INFO(e.message);
        REQUIRE(e.status == yp::PluginStatus::Enabled);
        auto t = yc::Technology::from_plugin(*loader.info("yantra.tetra"), loader.descriptor("yantra.tetra"),
                                             loader.instance("yantra.tetra"));
        REQUIRE(t);
        tech = *t;
    }
};
Tetra& tetra() {
    static Tetra t;
    return t;
}

yc::CellEntry fake_entry(const std::string& id, std::int64_t mhz_khz, std::int64_t la, std::int64_t cc) {
    yc::CellEntry e;
    e.cell.id = id;
    e.cell.technology_id = "faketech";
    e.cell.schema_id = "fake_cell";
    e.set_field("frequency_hz", std::int64_t{mhz_khz * 1000});
    e.set_field("la", la);
    e.set_field("colour_code", cc);
    return e;
}

yc::NeighbourRef ref_hz(std::int64_t hz) {
    yc::NeighbourRef r;
    r.fields["frequency_hz"] = hz;
    return r;
}

// ---- synthetic survey records of the fake technology -----------------

struct Survey {
    yc::Technology tech = fake_technology();
    std::shared_ptr<const yp::Schema> cell = std::make_shared<yp::Schema>(tech.cell_schema);
    std::shared_ptr<const yp::Schema> neighbour = std::make_shared<yp::Schema>(*tech.neighbour_schema);
    std::int64_t t = 0;

    ym::MappedRecord cell_record(std::uint64_t hz, std::uint64_t la, std::uint64_t cc, double level,
                                 ym::CarrierRole role = ym::CarrierRole::Serving, std::uint32_t flags = 0) {
        ym::MappedRecord m;
        m.provenance.technology_id = "faketech";
        m.schema = cell;
        m.schema_index = 0;
        m.record.values = {hz, la, cc, std::monostate{}, level};
        m.record.time.monotonic_ns = ++t;
        m.role = role;
        m.frequency_hz = hz;
        m.level = level;
        m.flags = flags;
        return m;
    }
    ym::MappedRecord entry(std::uint64_t broadcaster, std::uint64_t index, std::uint64_t count, std::uint64_t hz) {
        ym::MappedRecord m;
        m.provenance.technology_id = "faketech";
        m.schema = neighbour;
        m.schema_index = 1;
        m.record.values = {broadcaster, index, count, hz};
        m.record.time.monotonic_ns = ++t;
        m.role = ym::CarrierRole::NeighbourEntry;
        m.frequency_hz = broadcaster;
        m.entry_index = index;
        return m;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// DB-01: the model

TEST_CASE("DB-01 entries: fields live in the project cell, ids are generated, validation refuses bad values") {
    yc::CellDatabase db;
    yc::CellEntry e = fake_entry("", 400'025, 0x12C, 2);
    e.site_name = "Tower";
    e.sector = "A";
    e.azimuth_deg = 120.0;
    e.colour = "#12ab9F";
    e.set_field("srt_db", std::int64_t{20});
    CHECK(e.cell.carrier_hz == 400'025'000);
    CHECK(e.cell.identity.at("la") == 300);
    CHECK(e.cell.identity.at("colour_code") == 2);
    CHECK(e.parameters.count("srt_db") == 1);
    CHECK(e.display_name() == "Tower A");
    const std::string id = db.add(e);
    CHECK(id == "cell-1");
    CHECK(db.add(e) == "cell-2");
    CHECK_THROWS_AS(db.add(*db.find("cell-1")), yc::CellDbError);  // the id is used

    yc::CellEntry bad = e;
    bad.azimuth_deg = 360.0;
    CHECK_FALSE(bad.validate().empty());
    bad = e;
    bad.colour = "red";
    CHECK_FALSE(bad.validate().empty());
    bad = e;
    bad.neighbours.assign(32, ref_hz(400'000'000));
    CHECK(bad.validate().find("31") != std::string::npos);
    bad = e;
    bad.parameters["ssi"] = std::int64_t{1};  // out of Yantra's scope
    CHECK_FALSE(bad.validate().empty());
    bad = e;
    bad.notes = std::string("\xff\xfe", 2);
    CHECK_FALSE(bad.validate().empty());

    // merge is all or nothing.
    const yc::CellDatabase before = db;
    std::vector<yc::CellEntry> batch = {fake_entry("x1", 400'050, 1, 1), bad};
    CHECK_THROWS_AS(db.merge(batch), yc::CellDbError);
    CHECK(db == before);

    // DB-01: cells identified by LA, by channel, or by both (match fields).
    db.set_match_fields("faketech", {"la"});
    CHECK(db.match_fields("faketech") == std::vector<std::string>{"la"});
    CHECK(fake_technology().match_fields(db.match_fields("faketech")) == std::vector<std::string>{"la"});
    CHECK(fake_technology().match_fields({}) == std::vector<std::string>{"frequency_hz", "la"});
    CHECK_THROWS_AS(db.set_match_fields("faketech", {"Bad Name"}), yc::CellDbError);
}

TEST_CASE("DB-01 values: hex and decimal integers, reals, text; equality across int and real") {
    CHECK(std::get<std::int64_t>(*yc::parse_value("0x12C")) == 300);
    CHECK(std::get<std::int64_t>(*yc::parse_value(" 300 ")) == 300);
    CHECK(std::get<std::int64_t>(*yc::parse_value("+5")) == 5);
    CHECK(std::get<double>(*yc::parse_value("2.5")) == 2.5);
    CHECK(std::get<std::string>(*yc::parse_value("abc")) == "abc");
    CHECK_FALSE(yc::parse_value("  "));
    CHECK(yc::value_text(std::int64_t{300}, true) == "0x12C");
    CHECK(yc::value_text(2.0) == "2.0");
    CHECK(yc::values_equal(std::int64_t{2}, 2.0));
    CHECK_FALSE(yc::values_equal(std::int64_t{2}, std::string("2")));
}

// ---------------------------------------------------------------------------
// DB-02 / TST-33: CSV

TEST_CASE("DB-02 CSV round trip: every column, hex LA, ';' separator, quoted notes, neighbours (TST-33)") {
    yc::CellDatabase db;
    yc::CellEntry a = fake_entry("site-a", 400'025, 0x12C, 2);
    a.cell.label = "Site A";
    a.site_name = "Tower, north";
    a.sector = "1";
    a.azimuth_deg = 45.5;
    a.colour = "#ff0000";
    a.notes = "two\r\nlines with \"quotes\"; and ;";
    a.origin = yc::Origin::Manual;
    a.plugin_version = "1.2.3";
    a.set_field("power_dbm", std::int64_t{-5});
    a.set_field("offset", 6.25);
    a.set_field("site_code", std::string("alpha"));
    a.neighbours = {ref_hz(400'050'000), ref_hz(400'075'000)};
    a.neighbours[1].fields["la"] = std::int64_t{0x12D};
    db.add(a);
    yc::CellEntry b = fake_entry("site-b", 400'050, 0x12C, 3);
    b.origin = yc::Origin::Learned;
    db.add(b);

    const yc::Technologies techs = {fake_technology()};
    for (const char sep : {',', ';'}) {
        yc::CsvOptions o;
        o.separator = sep;
        o.hex_fields = {"la"};
        o.technologies = &techs;
        const std::string csv = yc::export_csv(db, o);
        CHECK(csv.rfind("#yantra-cells", 0) == 0);
        CHECK(csv.find("0x12C") != std::string::npos);
        CHECK(csv.find("channel") != std::string::npos);
        yc::CsvOptions in;
        in.technologies = &techs;
        const yc::CsvImport imp = yc::import_csv(csv, in);
        CHECK(imp.problems.empty());
        yc::CellDatabase back;
        back.merge(imp.entries);
        CHECK(back == db);
        CHECK(yc::export_csv(back, o) == csv);  // byte-identical a second time
    }
    // Without hex and without plugins: decimal, no channel column.
    const std::string plain = yc::export_csv(db);
    CHECK(plain.find("0x") == std::string::npos);
    CHECK(plain.find(",channel") == std::string::npos);
    yc::CellDatabase back;
    back.merge(yc::import_csv(plain).entries);
    CHECK(back == db);
    CHECK(yc::fingerprint(back) == yc::fingerprint(db));
    CHECK(yc::describe(db, "site.csv").find("2 cells") != std::string::npos);

    // Files: written atomically, read back.
    const fs::path dir = scratch("csv-file");
    yc::write_csv_file(db, (dir / "cells.csv").string());
    CHECK_FALSE(fs::exists(dir / "cells.csv.part"));
    std::vector<std::string> problems;
    CHECK(yc::read_csv_file((dir / "cells.csv").string(), {}, &problems) == db);
    CHECK(problems.empty());
}

TEST_CASE("DB-02 CSV import: malformed rows are skipped and reported by line, the others are kept") {
    const yc::Technologies techs = {fake_technology()};
    yc::CsvOptions o;
    o.technologies = &techs;
    o.technology = "faketech";
    o.schema = "fake_cell";
    const std::string csv =
        "\xEF\xBB\xBF"
        "id,label,frequency_hz,channel,la,colour_code,origin,azimuth_deg,neighbours.frequency_hz,neighbours.channel,Bad Col\n"
        "a,A,400025000,,0x12C,1,,,400050000|,|102,x\n"            // line 2: ok, neighbours by Hz and channel
        "b,B,abc,,300,1,,,,,\n"                                  // 3: frequency not a number
        "c,C,,101,300,1,,,,,\n"                                  // 4: ok, carrier from the channel
        "d,D,400025000,105,300,1,,,,,\n"                          // 5: channel does not match the frequency
        "e,E,400025000,,300,1,robot,,,,\n"                        // 6: unknown origin
        "f,F,400025000,,300,1,,400,,,\n"                          // 7: azimuth out of range
        "g,G,400025000,,300,1,,,,,,,,\n"                          // 8: more values than columns
        "a,A2,400025000,,300,1,,,,,\n"                            // 9: id used by line 2
        "\n"                                                     // blank: ignored
        "h,H,,999,300,1,,,,,\n"                                  // 11: not a channel of the band
        "i,\"I, quoted\",400075000,,300,2,import,,,,\n";         // 12: ok
    const yc::CsvImport imp = yc::import_csv(csv, o);
    REQUIRE(imp.entries.size() == 3);
    CHECK(imp.entries[0].cell.id == "a");
    CHECK(imp.entries[0].cell.identity.at("la") == 300);
    CHECK(imp.entries[0].cell.technology_id == "faketech");
    REQUIRE(imp.entries[0].neighbours.size() == 2);
    CHECK(std::get<std::int64_t>(imp.entries[0].neighbours[1].fields.at("frequency_hz")) == 400'050'000);
    CHECK(imp.entries[1].cell.carrier_hz == 400'025'000);
    CHECK(imp.entries[2].cell.label == "I, quoted");
    std::string all;
    for (const std::string& p : imp.problems) all += p + "\n";
    INFO(all);
    for (const char* l : {"line 3:", "line 5:", "line 6:", "line 7:", "line 8:", "line 9:", "line 11:"})
        CHECK(all.find(l) != std::string::npos);
    CHECK(all.find("line 2:") == std::string::npos);
    CHECK(all.find("line 4:") == std::string::npos);
    CHECK(all.find("column 'Bad Col' ignored") != std::string::npos);
    CHECK(imp.problems.size() == 8);

    // A channel without the plugin cannot be read.
    const yc::CsvImport no_plugin = yc::import_csv("id,channel\nz,101\n");
    CHECK(no_plugin.entries.empty());
    CHECK(no_plugin.problems.size() == 1);
}

TEST_CASE("DB-02 CSV import: unusable files are refused as a whole") {
    CHECK_THROWS_AS(yc::import_csv(""), yc::CellDbError);
    CHECK_THROWS_AS(yc::import_csv("#yantra-cells,1\r\n"), yc::CellDbError);         // no header
    CHECK_THROWS_AS(yc::import_csv("id,la,la\na,1,2\n"), yc::CellDbError);          // a column twice
    CHECK_THROWS_AS(yc::import_csv("id,notes\na,\"open\n"), yc::CellDbError);       // open quote
    CHECK_THROWS_AS(yc::import_csv("#yantra-cells,2\r\nid\r\n"), yc::CellDbError);  // newer format
    CHECK_THROWS_AS(yc::import_csv("#yantra-cells,x\r\nid\r\n"), yc::CellDbError);
    yc::CsvOptions small;
    small.max_bytes = 8;
    CHECK_THROWS_AS(yc::import_csv("id,label\na,b\n", small), yc::CellDbError);
    // A file without the format line is version 1 (a spreadsheet).
    const yc::CsvImport ok = yc::import_csv("id;la\r\na;0x10\r\n");
    REQUIRE(ok.entries.size() == 1);
    CHECK(ok.entries[0].cell.identity.at("la") == 16);
    CHECK_THROWS_AS(yc::read_csv_file((scratch("missing") / "none.csv").string()), yc::CellDbError);
}

TEST_CASE("DB-02 CSV with the TETRA plugin: channel numbers by its raster") {
    const yc::Technology& t = tetra().tech;
    const auto ch = t.channel_of_frequency(390'025'000);
    REQUIRE(ch);
    CHECK(t.frequency_of_channel(*ch) == 390'025'000u);
    const yc::Technologies techs = {t};
    yc::CsvOptions o;
    o.technologies = &techs;
    const std::string csv = "technology,schema,channel,mcc,mnc,la\ntetra,tetra_cell," + std::to_string(*ch) + ",206,1,0x64\n";
    const yc::CsvImport imp = yc::import_csv(csv, o);
    REQUIRE(imp.entries.size() == 1);
    CHECK(imp.entries[0].cell.carrier_hz == 390'025'000);
    CHECK(imp.entries[0].cell.identity.at("la") == 100);
}

// ---------------------------------------------------------------------------
// DB-04: verification, difference log

TEST_CASE("DB-04 verification: match, differs, new, missing, not in the database, ambiguous, not seen; the log") {
    const yc::Technologies techs = {fake_technology()};
    yc::CellDatabase db;
    yc::CellEntry a = fake_entry("a", 400'025, 1, 1);
    a.set_field("power_dbm", std::int64_t{10});
    a.neighbours = {ref_hz(400'050'000), ref_hz(400'100'000)};
    db.add(a);
    yc::CellEntry b = fake_entry("b", 400'050, 1, 2);  // no power_dbm: received is New
    db.add(b);
    db.add(fake_entry("c", 400'075, 1, 3));           // never received: not seen
    db.add(fake_entry("d1", 400'200, 7, 1));
    db.add(fake_entry("d2", 400'200, 8, 1));

    Survey s;
    yc::Observations obs(techs);
    obs.add(s.cell_record(400'025'000, 1, 5, -60.0));  // colour code 5: differs
    obs.add(s.entry(400'025'000, 0, 2, 400'050'000));
    obs.add(s.entry(400'025'000, 1, 2, 400'300'000));  // new; 400.1 MHz missing
    obs.add(s.cell_record(400'050'000, 1, 2, -70.0, ym::CarrierRole::Neighbour));
    obs.add(s.cell_record(400'500'000, 9, 1, -80.0, ym::CarrierRole::Other));  // not in the database
    obs.add(s.cell_record(400'600'000, 9, 1, -80.0, ym::CarrierRole::Other, ym::sample_flag::kNoSync));  // skipped
    const auto cells = obs.cells();
    REQUIRE(cells.size() == 3);
    REQUIRE(cells[0].neighbours);
    CHECK(cells[0].neighbours->size() == 2);
    CHECK(cells[0].neighbours->at(0).inherited.count("la") == 1);  // from the broadcaster
    CHECK_FALSE(cells[1].neighbours);

    yc::Verification v = yc::verify(db, cells, techs);
    REQUIRE(v.cells.size() == 3);
    CHECK(v.cells[0].status == yc::CellCheck::Status::Paired);
    CHECK(v.cells[0].entry == "a");
    auto state_of = [](const yc::CellCheck& c, const std::string& f) {
        for (const yc::FieldCheck& x : c.fields)
            if (x.field == f) return std::string(yc::state_name(x.state));
        return std::string("absent");
    };
    CHECK(state_of(v.cells[0], "frequency_hz") == "match");
    CHECK(state_of(v.cells[0], "la") == "match");
    CHECK(state_of(v.cells[0], "colour_code") == "differs");
    CHECK(state_of(v.cells[0], "power_dbm") == "missing");
    CHECK(v.cells[0].neighbours_compared);
    REQUIRE(v.cells[0].neighbours.size() == 3);
    CHECK(v.cells[0].neighbours[0].state == yc::State::Match);
    CHECK(v.cells[0].neighbours[1].state == yc::State::New);
    CHECK(v.cells[0].neighbours[2].state == yc::State::Missing);
    CHECK(v.cells[1].entry == "b");
    CHECK(v.cells[1].all_match());
    CHECK(v.cells[2].status == yc::CellCheck::Status::NotInDatabase);
    CHECK(v.not_seen == std::vector<std::string>{"c", "d1", "d2"});
    CHECK(v.differences() == 4);

    // Paired by channel only (DB-01): d1 and d2 share 400.2 MHz, so a cell
    // received there pairs with both.
    db.set_match_fields("faketech", {"frequency_hz"});
    yc::Observations o2(techs);
    o2.add(s.cell_record(400'200'000, 7, 1, -60.0));
    const yc::Verification amb = yc::verify(db, o2.cells(), techs);
    CHECK(amb.cells[0].status == yc::CellCheck::Status::Ambiguous);
    CHECK(amb.cells[0].candidates == std::vector<std::string>{"d1", "d2"});

    // The difference log: header, one line per difference, each once.
    yc::LogOptions lo;
    lo.time_utc = "2026-11-05T10:00:00Z";
    lo.include_not_seen = true;
    const auto lines = yc::log_lines(v, db, cells, lo);
    CHECK(lines.size() == 4 + 1 + 3);  // differences, not in database, not seen
    const fs::path dir = scratch("difference-log");
    {
        yc::DifferenceLog log(dir / "diff.csv");
        CHECK(log.append(lines) == lines.size());
        CHECK(log.append(lines) == 0);
    }
    auto later = lines;
    for (auto& l : later) l.time_utc = "2026-11-05T11:00:00Z";
    yc::DifferenceLog reopened(dir / "diff.csv");
    CHECK(reopened.append(later) == 0);  // the same differences, a later time: not again
    const std::string text = read_file(dir / "diff.csv");
    CHECK(text.rfind(yc::log_header(), 0) == 0);
    CHECK(text.find("colour_code,differs,1,5") != std::string::npos);
    CHECK(text.find("neighbour,new,,frequency_hz=400300000") != std::string::npos);
    CHECK(text.find("neighbour,missing,frequency_hz=400100000,") != std::string::npos);
    CHECK(text.find("not_in_database") != std::string::npos);
}

TEST_CASE("DB-04 verification with the TETRA plugin: parameters sent as codes match the stored dBm") {
    const yc::Technology& t = tetra().tech;
    CHECK(t.values_match("rxlev_access_min_dbm", std::int64_t{-115}, std::int64_t{-113}));
    CHECK_FALSE(t.values_match("rxlev_access_min_dbm", std::int64_t{-115}, std::int64_t{-105}));
    CHECK(t.values_match("colour_code", std::int64_t{3}, std::int64_t{3}));
    CHECK_FALSE(t.values_match("colour_code", std::int64_t{3}, std::int64_t{4}));
    CHECK(t.values_match("not_a_field", std::int64_t{3}, 3.0));  // exact comparison
}

// ---------------------------------------------------------------------------
// DB-03: auto-learn

TEST_CASE("DB-03 auto-learn: new cells added, missing fields and lists filled, nothing overwritten") {
    const yc::Technologies techs = {fake_technology()};
    yc::CellDatabase db;
    db.add(fake_entry("a", 400'025, 1, 9));  // colour code 9 stays (a difference, DB-04)
    Survey s;
    yc::Observations obs(techs);
    obs.add(s.cell_record(400'025'000, 1, 5, -60.0));
    obs.add(s.entry(400'025'000, 0, 1, 400'050'000));
    obs.add(s.cell_record(400'050'000, 1, 2, -70.0, ym::CarrierRole::Neighbour));
    obs.add(s.cell_record(400'050'000, 1, 2, -71.0, ym::CarrierRole::Neighbour));
    const yc::LearnResult r = yc::learn(db, obs.cells(), techs);
    CHECK(r.extended == std::vector<std::string>{"a"});
    REQUIRE(r.added.size() == 1);
    CHECK(db.entries().size() == 2);
    const yc::CellEntry& a = *db.find("a");
    CHECK(a.cell.identity.at("colour_code") == 9);
    CHECK(a.neighbours.size() == 1);
    const yc::CellEntry& n = *db.find(r.added[0]);
    CHECK(n.origin == yc::Origin::Learned);
    CHECK(n.plugin_version == "1.2.3");
    CHECK(n.cell.carrier_hz == 400'050'000);
    CHECK(n.cell.identity.at("colour_code") == 2);
    CHECK(n.parameters.count("level") == 0);  // a measurement, not a parameter
    // Learning again changes nothing.
    const yc::CellDatabase before = db;
    const yc::LearnResult again = yc::learn(db, obs.cells(), techs);
    CHECK(again.added.empty());
    CHECK(again.extended.empty());
    CHECK(db == before);
}

// ---------------------------------------------------------------------------
// DB-05: cross-check

TEST_CASE("PLG-05 link rule (plugin API 1.2): a linked field is the entry's own value, else the broadcaster's (inherited)") {
    // The fake technology with a second, OPTIONAL linked field: the
    // neighbour's area (as TETRA's neighbour_la -> la).
    yc::Technology t = fake_technology();
    yp::Field la;
    la.name = "neighbour_la";
    la.type = yp::FieldType::UInt;
    t.neighbour_schema->fields.push_back(la);
    t.links.push_back(yp::FieldLink{"neighbour_la", "la"});
    const yc::Technologies techs = {t};
    Survey s;
    s.tech = t;
    s.neighbour = std::make_shared<yp::Schema>(*t.neighbour_schema);
    auto entry = [&](std::uint64_t index, std::uint64_t hz, yp::Value area) {
        ym::MappedRecord m = s.entry(400'025'000, index, 3, hz);
        m.record.values.push_back(std::move(area));
        return m;
    };
    yc::Observations obs(techs);
    obs.add(s.cell_record(400'025'000, 1, 5, -60.0));
    obs.add(entry(0, 400'050'000, std::uint64_t{7}));   // another area, given
    obs.add(entry(1, 400'075'000, std::monostate{}));   // not given: the broadcaster's
    // Only the optional field, no frequency: it names a cell by its own
    // area; the frequency is the broadcaster's (the rule holds for every link).
    ym::MappedRecord no_hz = entry(2, 0, std::uint64_t{9});
    no_hz.record.values[3] = std::monostate{};
    obs.add(no_hz);
    const auto cells = obs.cells();
    REQUIRE(cells.size() == 1);
    REQUIRE(cells[0].neighbours);
    REQUIRE(cells[0].neighbours->size() == 3);
    const yc::NeighbourRef& given = cells[0].neighbours->at(0);
    CHECK(given.fields.at("la") == yc::FieldValue{std::int64_t{7}});
    CHECK(given.inherited.count("la") == 0);
    const yc::NeighbourRef& taken = cells[0].neighbours->at(1);
    CHECK(taken.fields.at("frequency_hz") == yc::FieldValue{std::int64_t{400'075'000}});
    CHECK(taken.fields.at("la") == yc::FieldValue{std::int64_t{1}});
    CHECK(taken.inherited.count("la") == 1);
    const yc::NeighbourRef& area_only = cells[0].neighbours->at(2);
    CHECK(area_only.fields.at("la") == yc::FieldValue{std::int64_t{9}});
    CHECK(area_only.fields.at("frequency_hz") == yc::FieldValue{std::int64_t{400'025'000}});
    CHECK(area_only.inherited.count("frequency_hz") == 1);

    // In the database: the given area must agree (a cell of area 1 on that
    // carrier is not it), the inherited one never refuses.
    yc::CellDatabase db;
    db.add(fake_entry("a", 400'025, 1, 5));
    db.add(fake_entry("b1", 400'050, 1, 1));
    db.add(fake_entry("c", 400'075, 2, 1));
    CHECK_FALSE(yc::match_score(given, db.find("b1")->fields()));
    CHECK(yc::match_score(taken, db.find("c")->fields()));
}

TEST_CASE("DB-05 cross-check: not mutual, list unknown, not seen, ambiguous; inherited fields never refuse") {
    yc::CellDatabase db;
    yc::CellEntry a = fake_entry("a", 400'025, 1, 1);
    yc::CellEntry b = fake_entry("b", 400'050, 1, 1);
    yc::CellEntry c = fake_entry("c", 400'075, 2, 1);  // another area
    yc::CellEntry d = fake_entry("d", 400'100, 1, 1);
    yc::CellEntry e1 = fake_entry("e1", 400'200, 3, 1);
    yc::CellEntry e2 = fake_entry("e2", 400'200, 4, 1);
    // a lists b and c (c by frequency, la inherited 1: still c), b lists c only.
    yc::NeighbourRef to_c = ref_hz(400'075'000);
    to_c.fields["la"] = std::int64_t{1};
    to_c.inherited.insert("la");
    a.neighbours = {ref_hz(400'050'000), to_c};
    b.neighbours = {ref_hz(400'075'000), ref_hz(400'900'000), ref_hz(400'200'000), ref_hz(400'100'000)};
    c.neighbours = {ref_hz(400'025'000)};
    for (auto* x : {&a, &b, &c, &d, &e1, &e2}) db.add(*x);

    // Scores: a named field that differs refuses; an inherited one only ranks.
    yc::NeighbourRef named_la = ref_hz(400'075'000);
    named_la.fields["la"] = std::int64_t{1};
    CHECK_FALSE(yc::match_score(named_la, db.find("c")->fields()));
    CHECK(yc::match_score(to_c, db.find("c")->fields()) == 1);
    yc::NeighbourRef only_inherited;
    only_inherited.fields["la"] = std::int64_t{2};
    only_inherited.inherited.insert("la");
    CHECK_FALSE(yc::match_score(only_inherited, db.find("c")->fields()));

    const auto f = yc::cross_check(yc::database_views(db));
    std::vector<std::string> got;
    for (const auto& x : f) got.push_back(x.cell + ">" + x.other + ":" + yc::finding_name(x.kind));
    INFO(got.size());
    CHECK(got == std::vector<std::string>{
                     "a>b:not_mutual",       // b does not list a
                     "b>c:not_mutual",       // c lists a, not b
                     "b>:not_seen",          // 400.9 MHz: no such cell
                     "b>:ambiguous",         // 400.2 MHz: e1 and e2
                     "b>d:list_unknown",     // d has no list
                 });
    CHECK(f[0].text == "a lists b, but b does not list a");
}

// ---------------------------------------------------------------------------
// DB-08: the reselection simulator, hand-computed with the TETRA rule

namespace {
yc::FieldMap tetra_fields(std::int64_t hz, std::int64_t rxlev_min, std::int64_t txmax, bool thresholds = false) {
    yc::FieldMap m;
    m["frequency_hz"] = hz;
    m["mcc"] = std::int64_t{206};
    m["mnc"] = std::int64_t{1};
    m["la"] = std::int64_t{100};
    m["colour_code"] = std::int64_t{1};
    m["rxlev_access_min_dbm"] = rxlev_min;
    m["ms_txpwr_max_cell_dbm"] = txmax;
    if (thresholds) {
        m["srt_db"] = std::int64_t{25};
        m["srh_db"] = std::int64_t{6};
        m["frt_db"] = std::int64_t{10};
        m["frh_db"] = std::int64_t{3};
    }
    return m;
}
}  // namespace

TEST_CASE("DB-08 reselection: C1 and the serving state by hand (C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX - P))") {
    const yc::Technology& t = tetra().tech;
    auto cell = [](double level, bool thresholds = true, std::int64_t txmax = 30, std::int64_t hz = 390'000'000) {
        return yc::SimCell{"", tetra_fields(hz, -110, txmax, thresholds), level};
    };
    const yc::SimCell n_dead = cell(-130.0, false, 30, 390'100'000);  // C2 -20
    struct Case {
        const char* name;
        double serving;
        double neighbour;
        const char* state;
        bool chosen;
        double c1;
    };
    // Thresholds SRT 25, SRH 6, FRT 10, FRH 3; MS power 30 = MS_TXPWR_MAX 30.
    const Case cases[] = {
        {"strong serving, weak neighbour", -60.0, -130.0, "exclusive", false, 50.0},
        {"serving above the neighbour", -60.0, -70.0, "superior", false, 50.0},
        {"above SRT", -80.0, -78.0, "usable", false, 30.0},
        {"below SRT, C2 > C1 + SRH", -90.0, -80.0, "improvable", true, 20.0},
        {"below SRT, C2 <= C1 + SRH", -90.0, -85.0, "improvable", false, 20.0},
        {"below FRT, C2 > C1 + FRH", -105.0, -100.0, "relinquishable", true, 5.0},
        {"below FRT, C2 <= C1 + FRH", -105.0, -103.0, "relinquishable", false, 5.0},
        {"C1 < 0", -115.0, -100.0, "radio_link_failure", true, -5.0},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        const yc::ReselectionOutcome o = yc::simulate(t, cell(c.serving), {cell(c.neighbour, false, 30, 390'100'000)}, 30.0);
        REQUIRE(o.ok);
        MESSAGE(o.reason);
        CHECK(o.state == c.state);
        CHECK(o.chosen.has_value() == c.chosen);
        REQUIRE(o.serving_c1);
        CHECK(*o.serving_c1 == doctest::Approx(c.c1));
        REQUIRE(o.neighbour_c1.size() == 1);
        CHECK(*o.neighbour_c1[0] == doctest::Approx(c.neighbour + 110.0));
    }
    // MS power below MS_TXPWR_MAX: C1 lower by the difference.
    const yc::ReselectionOutcome p = yc::simulate(t, cell(-60.0, true, 35), {n_dead}, 30.0);
    CHECK(*p.serving_c1 == doctest::Approx(45.0));
    // No thresholds and a better neighbour: the plugin cannot decide.
    const yc::ReselectionOutcome u = yc::simulate(t, cell(-90.0, false), {cell(-80.0, false, 30, 390'100'000)});
    CHECK_FALSE(u.ok);
    CHECK_FALSE(u.error.empty());
    // A technology without a reselection rule.
    CHECK_FALSE(yc::simulate(fake_technology(), yc::SimCell{}, {}).ok);
}

TEST_CASE("DB-08 reselection with database entries and a walk: the serving cell follows the reselections") {
    const yc::Technologies techs = {tetra().tech};
    yc::CellDatabase db;
    auto entry = [&](const std::string& id, std::int64_t hz, std::vector<std::int64_t> ns) {
        yc::CellEntry e;
        e.cell.id = id;
        e.cell.technology_id = "tetra";
        e.cell.schema_id = "tetra_cell";
        for (const auto& [k, v] : tetra_fields(hz, -110, 30, true)) e.set_field(k, v);
        for (const std::int64_t n : ns) e.neighbours.push_back(ref_hz(n));
        db.add(e);
    };
    entry("A", 390'000'000, {390'100'000, 390'200'000});
    entry("B", 390'100'000, {390'000'000});
    CHECK(yc::neighbour_entries(db, "A") == std::vector<std::string>{"B"});  // 390.2 MHz is no entry

    const auto o = yc::simulate(db, techs, "A", {{"A", -90.0}, {"B", -80.0}});
    REQUIRE(o.ok);
    CHECK(o.state == "improvable");
    CHECK(o.chosen == std::size_t{0});

    // A walks away from A towards B; with a hold of 1 s the reselection
    // waits for the second step that chooses B.
    std::vector<yc::WalkStep> steps;
    const double a_levels[] = {-60, -80, -90, -95, -95, -95};
    const double b_levels[] = {-90, -85, -80, -75, -75, -75};
    for (int k = 0; k < 6; ++k)
        steps.push_back(yc::WalkStep{k * 1'000'000'000LL, {{"A", a_levels[k]}, {"B", b_levels[k]}}});
    yc::WalkOptions wo;
    wo.hold_s = 1.0;
    const auto walk = yc::simulate_walk(db, techs, steps, wo);
    REQUIRE(walk.size() == 6);
    CHECK(walk[0].serving == "A");
    CHECK(walk[0].outcome.state == "superior");
    CHECK(walk[2].chosen == "B");      // C1 20 < SRT 25, C2 30 > 26
    CHECK_FALSE(walk[2].reselected);   // held 0 s
    CHECK(walk[3].reselected);         // held 1 s
    CHECK(walk[4].serving == "B");
    CHECK(walk[4].outcome.state == "superior");
}
