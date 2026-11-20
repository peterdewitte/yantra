// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-gen (RAD-71, RAD-93): command line and exit codes, every sample
// format against the scenario engine's samples, SigMF metadata validity,
// ground-truth CSV, cell CSV and record file (RAD-65) against the scenario
// and the engine, sample drops in the capture segments, the automatic full
// scale, determinism and seeded golden digests (tests/yantra_gen/golden;
// rewrite on purpose with test_yantra_gen --update), bad input handling.
// Partly copied from yantra 68ea20a: tests/yantra_gen/test_cli.cpp,
// adapted for RAD-71.

#include "gen_app.hpp"
#include "golden.hpp"

#include "yantra/gen/digest.hpp"
#include "yantra/gen/scenario.hpp"
#include "yantra/gen/scenario_engine.hpp"
#include "yantra/gen/truth_records.hpp"

#include <yantra/radios/record_file.hpp>

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace yantra;
using namespace yantra::gentool;
using nlohmann::json;
using cf = std::complex<float>;

namespace {

const std::string kScenarioDir = YANTRA_SCENARIO_DIR;
const std::string kGoldenDir = YANTRA_GEN_CLI_GOLDEN_DIR;

// A scratch folder below the build tree (the test's working directory),
// removed again at the end of the test case.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::current_path() / "yantra_gen_test" / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
        fs::remove(path.parent_path(), ec);  // only when empty
    }
    std::string file(const std::string& n) const { return (path / n).string(); }
};

struct Result {
    int code = 0;
    std::string out, err;
};

Result run_gen(std::vector<std::string> args)
{
    args.insert(args.begin(), {"--scenario-dir", kScenarioDir});
    std::ostringstream out, err;
    Result r;
    r.code = run(args, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

std::string slurp(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void spit(const std::string& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> v;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
        v.push_back(l);
    return v;
}

std::vector<std::string> split_csv(const std::string& line)
{
    std::vector<std::string> v;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            v.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    v.push_back(cur);
    return v;
}

// A CSV file as rows of column name -> value.
std::vector<std::map<std::string, std::string>> read_csv(const std::string& path)
{
    const auto lines = lines_of(slurp(path));
    std::vector<std::map<std::string, std::string>> rows;
    if (lines.empty())
        return rows;
    const auto head = split_csv(lines[0]);
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto f = split_csv(lines[i]);
        REQUIRE(f.size() == head.size());
        std::map<std::string, std::string> r;
        for (size_t k = 0; k < f.size(); ++k)
            r[head[k]] = f[k];
        rows.push_back(r);
    }
    return rows;
}

// The engine's own output for a scenario (what yantra-gen must write).
std::vector<cf> engine_samples(const gen::Scenario& s)
{
    gen::ScenarioEngine e(s);
    std::vector<cf> v;
    std::vector<cf> buf(65536);
    while (!e.done()) {
        const size_t n = e.read(buf.data(), buf.size());
        v.insert(v.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n));
    }
    return v;
}

// Decodes a data file of any format back to engine units.
std::vector<cf> decode(const std::string& bytes, SampleFormat f, double amplitude)
{
    std::vector<cf> v(bytes.size() / bytes_per_sample(f));
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data());
    for (size_t i = 0; i < v.size(); ++i) {
        switch (f) {
        case SampleFormat::cf32:
            std::memcpy(&v[i], p + 8 * i, 8);
            break;
        case SampleFormat::cs16: {
            const auto re = static_cast<int16_t>(p[4 * i] | (p[4 * i + 1] << 8));
            const auto im = static_cast<int16_t>(p[4 * i + 2] | (p[4 * i + 3] << 8));
            v[i] = cf(static_cast<float>(re * amplitude / 32767.0), static_cast<float>(im * amplitude / 32767.0));
            break;
        }
        case SampleFormat::cs8:
            v[i] = cf(static_cast<float>(static_cast<int8_t>(p[2 * i]) * amplitude / 127.0),
                      static_cast<float>(static_cast<int8_t>(p[2 * i + 1]) * amplitude / 127.0));
            break;
        case SampleFormat::cu8:
            v[i] = cf(static_cast<float>((p[2 * i] - 127.5) * amplitude / 127.5),
                      static_cast<float>((p[2 * i + 1] - 127.5) * amplitude / 127.5));
            break;
        }
    }
    return v;
}

// Two cells on the command line (shared by several tests).
const std::vector<std::string> kTwoCells = {
    "--cell", "id=A,carrier=3600,la=10,cc=5,number=1,level=-60,neighbours=B",
    "--cell", "id=B,carrier=3604,la=11,cc=6,number=2,level=-75,ppm=0.5,encryption=1,neighbours=A",
    "--mcc", "901", "--mnc", "77", "--seed", "7"};

std::vector<std::string> with(std::vector<std::string> a, const std::vector<std::string>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

gen::Scenario scenario_of(const std::vector<std::string>& args)
{
    Options o = parse_args(with(args, {"--out", "x"}));
    return load_scenario(o, {kScenarioDir});
}

}  // namespace

TEST_CASE("yantra-gen: help, version, list")
{
    Result r = run_gen({"--help"});
    CHECK(r.code == kExitOk);
    CHECK(r.out.find("usage: yantra-gen") == 0);
    r = run_gen({"--version"});
    CHECK(r.code == kExitOk);
    CHECK(r.out == version_text());
    r = run_gen({"--list"});
    CHECK(r.code == kExitOk);
    for (const auto& i : gen::list_scenarios(kScenarioDir))
        CHECK(r.out.find(i.id + "\t") != std::string::npos);
    CHECK(r.out.find("(default)") != std::string::npos);
}

TEST_CASE("yantra-gen: usage errors")
{
    const std::vector<std::vector<std::string>> bad = {
        {},                                            // no --out
        {"--bogus", "--out", "x"},
        {"--rate", "10k", "--out", "x"},
        {"--rate", "abc", "--out", "x"},
        {"--duration", "-1", "--out", "x"},
        {"--duration", "2k", "--out", "x"},
        {"--duration", "0", "--out", "x"},            // endless only to stdout
        {"--centre", "100", "--out", "x"},
        {"--seed", "-3", "--out", "x"},
        {"--seed", "1.5", "--out", "x"},
        {"--format", "cs4", "--out", "x"},
        {"--full-scale-dbm", "-40", "--out", "x"},    // cf32 has no full scale
        {"--format", "cs8", "--full-scale-dbm", "99", "--out", "x"},
        {"--fading", "fast", "--out", "x"},
        {"--drops", "1,2", "--out", "x"},
        {"--drops", "1,0,5", "--out", "x"},
        {"--cell", "la=3", "--out", "x"},             // carrier missing
        {"--cell", "carrier=3600,colour=1", "--out", "x"},
        {"--cell", "carrier=99999", "--out", "x"},
        {"--cell", "carrier=3600", "--scenario", "fraunces", "--out", "x"},
        {"--scenario", "a", "--example", "b", "--out", "x"},
        {"--mcc", "901", "--out", "x"},
        {"--no-side-files", "--truth", "t.csv", "--out", "x"},
        {"--out"},
    };
    for (const auto& a : bad) {
        std::string joined;
        for (const auto& s : a)
            joined += s + " ";
        INFO(joined);
        const Result r = run_gen(a);
        CHECK(r.code == kExitUsage);
        CHECK(r.err.find("yantra-gen: error: ") == 0);
    }
    CHECK(parse_si_number("390.0125M") == doctest::Approx(390.0125e6));
    CHECK(parse_si_number("500k") == doctest::Approx(500e3));
    CHECK_FALSE(parse_si_number("k").has_value());
    CHECK_FALSE(parse_si_number("1.2.3").has_value());
    const CellOption c = parse_cell_option("carrier=3610,band=4,offset=2,la=99,cc=12,level=-81.5,neighbours=X+Y", 2);
    CHECK(c.cell.id == "C3");
    CHECK(c.cell.cell_number == 3);
    CHECK(c.cell.main_carrier == 3610);
    CHECK(c.cell.frequency_band == 4);
    CHECK(c.cell.offset == 2);
    CHECK(c.cell.location_area == 99);
    CHECK(c.cell.colour_code == 12);
    CHECK(c.cell.level_dbm == doctest::Approx(-81.5));
    CHECK(c.neighbours == std::vector<std::string>{"X", "Y"});
}

TEST_CASE("yantra-gen: bad scenario input and unwritable output")
{
    TempDir d("bad_input");
    // Unknown id, missing file, invalid JSON, invalid scenario, unknown
    // example with no default (empty folder), cell network that does not
    // validate (unknown neighbour).
    spit(d.file("broken.json"), "{ \"format\": ");
    spit(d.file("other.json"), R"({"format":"something-else","version":1})");
    spit(d.file("newer.json"), R"({"format":"yantra-radios-scenario","version":2,"id":"n","cells":[]})");
    fs::create_directories(d.path / "empty");
    struct Case {
        std::vector<std::string> args;
        std::string message;
    };
    const std::vector<Case> cases = {
        {{"--scenario", "no_such_scenario"}, "no scenario file or id \"no_such_scenario\""},
        {{"--scenario", d.file("broken.json")}, "broken.json"},
        {{"--scenario", d.file("other.json")}, "other.json"},
        {{"--scenario", d.file("newer.json")}, "newer.json"},
        {{"--cell", "carrier=3600,neighbours=Z"}, "command-line cells"},
        {{"--cell", "carrier=3600", "--cell", "id=C1,carrier=3602"}, "command-line cells"},
    };
    for (const Case& c : cases) {
        INFO(c.args[0] << " " << c.args[1]);
        const Result r = run_gen(with(c.args, {"--out", d.file("x.cf32"), "--quiet"}));
        CHECK(r.code == kExitScenario);
        CHECK(r.err.find(c.message) != std::string::npos);
    }
    {
        // No scenario for the example id and no default in the folder.
        Options o = parse_args({"--example", "nothing", "--out", "x"});
        CHECK_THROWS_AS(load_scenario(o, {(d.path / "empty").string()}), ScenarioLoadError);
    }
    // Nothing was written for the failed runs.
    for (const auto& e : fs::directory_iterator(d.path))
        CHECK((e.path().extension() == ".json" || e.is_directory()));

    // Output folder missing, and a read-only folder: exit 4, no files left.
    Result r = run_gen(with(kTwoCells, {"--duration", "0.05", "--quiet", "--out", d.file("missing/dir/x.cf32")}));
    CHECK(r.code == kExitOutput);
    fs::create_directories(d.path / "ro");
    fs::permissions(d.path / "ro", fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    // (Skipped when the folder stays writable, e.g. as root.)
    const bool writable = static_cast<bool>(std::ofstream(d.file("ro/probe")));
    fs::remove(d.file("ro/probe"));
    if (!writable) {
        r = run_gen(with(kTwoCells, {"--duration", "0.05", "--quiet", "--out", d.file("ro/x.cf32")}));
        CHECK(r.code == kExitOutput);
        CHECK(fs::is_empty(d.path / "ro"));
    }
    fs::permissions(d.path / "ro", fs::perms::owner_all, fs::perm_options::replace);
}

TEST_CASE("yantra-gen: every sample format holds the scenario engine's samples")
{
    TempDir d("formats");
    const std::vector<std::string> base = with(kTwoCells, {"--duration", "0.1", "--quiet"});
    const gen::Scenario s = scenario_of(with(base, {}));
    const std::vector<cf> ref = engine_samples(s);
    REQUIRE(ref.size() == 50000);
    const double full_scale_dbm = -40.0;
    const double amp = std::sqrt(gen::dbm_to_power(full_scale_dbm, s.receiver.reference_level_dbm));
    struct F {
        SampleFormat f;
        const char* name;
        double lsb;  // quantisation step in engine units
    };
    const std::vector<F> formats = {{SampleFormat::cf32, "cf32", 0.0},
                                    {SampleFormat::cs16, "cs16", amp / 32767.0},
                                    {SampleFormat::cs8, "cs8", amp / 127.0},
                                    {SampleFormat::cu8, "cu8", amp / 127.5}};
    for (const F& f : formats) {
        CAPTURE(f.name);
        std::vector<std::string> args = with(base, {"--format", f.name, "--out", d.file(std::string("s.") + f.name)});
        if (f.f != SampleFormat::cf32)
            args = with(args, {"--full-scale-dbm", "-40"});
        const Result r = run_gen(args);
        REQUIRE(r.code == kExitOk);
        const std::string bytes = slurp(d.file(std::string("s.") + f.name));
        REQUIRE(bytes.size() == ref.size() * bytes_per_sample(f.f));
        const std::vector<cf> got = decode(bytes, f.f, amp);
        if (f.f == SampleFormat::cf32) {
            CHECK(std::memcmp(got.data(), ref.data(), bytes.size()) == 0);
        } else {
            double worst = 0.0;
            for (size_t i = 0; i < ref.size(); ++i)
                worst = std::max({worst, static_cast<double>(std::abs(got[i].real() - ref[i].real())),
                                  static_cast<double>(std::abs(got[i].imag() - ref[i].imag()))});
            CHECK(worst <= 0.5 * f.lsb * 1.0001);
            const json m = json::parse(slurp(d.file("s.sigmf-meta")));
            CHECK(m["global"]["yantra:full_scale_dbm"].get<double>() == doctest::Approx(full_scale_dbm));
            CHECK(m["global"]["yantra:clipped_samples"].get<uint64_t>() == 0);
        }
        // The samples' level: cell A at -60 dBm dominates.
        double p = 0.0;
        for (const cf& v : got)
            p += std::norm(v);
        p /= static_cast<double>(got.size());
        CHECK(gen::power_to_dbm(p, s.receiver.reference_level_dbm) == doctest::Approx(-59.86).epsilon(0.002));
    }
    // cu8 mid-scale is 127.5: a silent run (no cells on air at that level
    // and no noise) writes codes 127/128 only.
    const Result r = run_gen({"--cell", "carrier=3600,level=-200", "--no-noise", "--duration", "0.01", "--quiet",
                              "--format", "cu8", "--full-scale-dbm", "-40", "--out", d.file("q.cu8")});
    REQUIRE(r.code == kExitOk);
    for (unsigned char c : slurp(d.file("q.cu8")))
        CHECK((c == 127 || c == 128));
}

TEST_CASE("yantra-gen: clipping is counted and reported")
{
    TempDir d("clip");
    const Result r = run_gen(with(kTwoCells, {"--duration", "0.05", "--quiet", "--format", "cs8",
                                              "--full-scale-dbm", "-70", "--out", d.file("c.cs8")}));
    REQUIRE(r.code == kExitOk);
    CHECK(r.err.find("samples clipped") != std::string::npos);
    const json m = json::parse(slurp(d.file("c.sigmf-meta")));
    CHECK(m["global"]["yantra:clipped_samples"].get<uint64_t>() > 1000);
}

TEST_CASE("yantra-gen: the automatic full scale does not clip any bundled scenario")
{
    TempDir d("autoscale");
    for (const auto& info : gen::list_scenarios(kScenarioDir)) {
        CAPTURE(info.id);
        const Result r = run_gen({"--scenario", info.id, "--duration", "0.3", "--format", "cs8", "--quiet", "--out",
                                  d.file(info.id + ".cs8")});
        REQUIRE(r.code == kExitOk);
        const json m = json::parse(slurp(d.file(info.id + ".sigmf-meta")));
        CHECK(m["global"]["yantra:clipped_samples"].get<uint64_t>() == 0);
        // Not wastefully high either: the strongest cell uses at least
        // 1/16 of the range at its RMS.
        const gen::Scenario s = gen::load_scenario_file(info.path);
        gen::ScenarioEngine e(s);
        double best = -1e9;
        for (const auto& c : e.truth_at(0.0).cells)
            if (c.on_air)
                best = std::max(best, c.level_dbm);
        if (!s.receiver.dc_dbfs)  // else the DC spike sets the scale
            CHECK(m["global"]["yantra:full_scale_dbm"].get<double>() - best < 24.0 + 1e-9);
    }
}

TEST_CASE("yantra-gen: SigMF metadata is valid")
{
    TempDir d("sigmf");
    // No extension: the SigMF pair STEM.sigmf-data + STEM.sigmf-meta.
    Result r = run_gen({"--scenario", "failed_site", "--duration", "9", "--format", "cs16", "--quiet",
                        "--drops", "1.5,2.5,300", "--out", d.file("rec")});
    REQUIRE(r.code == kExitOk);
    REQUIRE(fs::exists(d.file("rec.sigmf-data")));
    const json m = json::parse(slurp(d.file("rec.sigmf-meta")));
    REQUIRE(m.is_object());
    for (const char* k : {"global", "captures", "annotations"})
        REQUIRE(m.contains(k));
    for (auto it = m.begin(); it != m.end(); ++it)
        CHECK((it.key() == "global" || it.key() == "captures" || it.key() == "annotations"));
    const json& g = m["global"];
    // Required core fields and their types (SigMF 1.2 core namespace).
    REQUIRE(g["core:datatype"].is_string());
    CHECK(std::regex_match(g["core:datatype"].get<std::string>(),
                           std::regex("^(c|r)(f32|f64|i32|i16|u32|u16|i8|u8)(_le|_be)?$")));
    CHECK(g["core:datatype"] == "ci16_le");
    REQUIRE(g["core:version"].is_string());
    CHECK(std::regex_match(g["core:version"].get<std::string>(), std::regex("^1\\.[0-9]+\\.[0-9]+$")));
    CHECK(g["core:sample_rate"].get<double>() == 500000.0);
    CHECK(g["core:num_channels"] == 1);
    CHECK(g["yantra:simulated"] == true);
    CHECK(g["yantra:scenario_id"] == "failed_site");
    CHECK(g["yantra:seed"] == 1004);
    // Every non-core key is in a declared extension namespace.
    std::set<std::string> namespaces = {"core"};
    for (const auto& e : g["core:extensions"]) {
        REQUIRE(e["name"].is_string());
        REQUIRE(e["version"].is_string());
        REQUIRE(e["optional"].is_boolean());
        namespaces.insert(e["name"].get<std::string>());
    }
    auto check_keys = [&](const json& obj) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const std::string k = it.key();
            const size_t colon = k.find(':');
            REQUIRE(colon != std::string::npos);
            CHECK(namespaces.count(k.substr(0, colon)) == 1);
        }
    };
    check_keys(g);
    // Captures: sample_start required, strictly increasing; global_index
    // = signal sample index, so the gaps are the drops.
    const json& caps = m["captures"];
    REQUIRE(caps.is_array());
    REQUIRE(caps.size() >= 2);
    const uint64_t data_samples = fs::file_size(d.file("rec.sigmf-data")) / 4;
    const uint64_t dropped = g["yantra:dropped_samples"].get<uint64_t>();
    CHECK(data_samples + dropped == 9 * 500000);
    // Drops at 1.5, 4.0, 6.5, 9.0 (not inside the run) s.
    CHECK(caps.size() == 4);
    CHECK(dropped == 3 * 300);
    for (size_t i = 0; i < caps.size(); ++i) {
        check_keys(caps[i]);
        REQUIRE(caps[i]["core:sample_start"].is_number_unsigned());
        CHECK(caps[i]["core:frequency"].get<double>() == doctest::Approx(390.2e6));  // middle of 390.0 .. 390.4 MHz
        const uint64_t start = caps[i]["core:sample_start"], global = caps[i]["core:global_index"];
        CHECK(start < data_samples);
        if (i > 0) {
            CHECK(start > caps[i - 1]["core:sample_start"].get<uint64_t>());
            CHECK(global - start == 300 * i);  // signal index ahead by the drops so far
            CHECK(start == static_cast<uint64_t>(std::llround((1.5 + 2.5 * static_cast<double>(i - 1)) * 500000.0)) - 300 * (i - 1));
        }
    }
    // Annotations: sorted, sample_start required, frequency edges ordered;
    // one per cell, one per event.
    const json& ann = m["annotations"];
    REQUIRE(ann.is_array());
    uint64_t last = 0;
    size_t cells = 0, events = 0;
    for (const json& a : ann) {
        check_keys(a);
        REQUIRE(a["core:sample_start"].is_number_unsigned());
        CHECK(a["core:sample_start"].get<uint64_t>() >= last);
        last = a["core:sample_start"];
        if (a.contains("core:freq_lower_edge")) {
            CHECK(a["core:freq_lower_edge"].get<double>() < a["core:freq_upper_edge"].get<double>());
            CHECK(a["core:sample_count"].get<uint64_t>() == data_samples);
            ++cells;
        } else {
            ++events;
            CHECK(a["core:label"].get<std::string>().rfind("cell_o", 0) == 0);
        }
    }
    CHECK(cells == 3);
    CHECK(events == 2);  // N2 off at 6 s, on at 8 s
    // cf32 has no full scale.
    r = run_gen(with(kTwoCells, {"--duration", "0.02", "--quiet", "--out", d.file("f.cf32")}));
    REQUIRE(r.code == kExitOk);
    const json mf = json::parse(slurp(d.file("f.sigmf-meta")));
    CHECK(mf["global"]["core:datatype"] == "cf32_le");
    CHECK_FALSE(mf["global"].contains("yantra:full_scale_dbm"));
    CHECK(mf["global"]["yantra:truth_file"] == "f.truth.csv");
    CHECK(mf["global"]["yantra:records_file"] == "f.records.jsonl");
}

TEST_CASE("yantra-gen: ground-truth CSV and cell CSV match the scenario and the engine")
{
    TempDir d("truth");
    const Result r = run_gen({"--scenario", "failed_site", "--duration", "9", "--quiet", "--format", "cs8", "--out",
                              d.file("t.cs8")});
    REQUIRE(r.code == kExitOk);
    const gen::Scenario s = scenario_of({"--scenario", "failed_site", "--duration", "9"});
    gen::ScenarioEngine e(s);
    const auto rows = read_csv(d.file("t.truth.csv"));
    REQUIRE(rows.size() == 90 * s.cells.size());
    for (size_t k = 0; k < rows.size(); ++k) {
        const auto& row = rows[k];
        const double t = std::stod(row.at("t_s"));
        CHECK(t == doctest::Approx(static_cast<double>(k / s.cells.size()) * 0.1));
        const gen::TruthRow truth = e.truth_at(t);
        const gen::CellTruth& c = truth.cells[k % s.cells.size()];
        const gen::ScenarioCell& sc = s.cells[k % s.cells.size()];
        CHECK(row.at("cell") == sc.id);
        CHECK(row.at("mcc") == "901");
        CHECK(row.at("mnc") == "2");
        CHECK(std::stoi(row.at("la")) == sc.location_area);
        CHECK(std::stoi(row.at("colour_code")) == sc.colour_code);
        CHECK(std::stod(row.at("frequency_hz")) == doctest::Approx(gen::cell_frequency_hz(sc)));
        CHECK(row.at("on_air") == (c.on_air ? "1" : "0"));
        if (c.on_air)
            CHECK(std::stod(row.at("level_dbm")) == doctest::Approx(c.level_dbm).epsilon(1e-4));
        else
            CHECK(row.at("level_dbm") == "-inf");
        CHECK(std::stoull(row.at("sample")) == static_cast<uint64_t>(std::llround(t * 500000.0)));
    }
    // N2 goes off at 6 s and back at 8 s; N3 never transmits.
    auto on = [&](const std::string& cell, double t) {
        for (const auto& row : rows)
            if (row.at("cell") == cell && std::abs(std::stod(row.at("t_s")) - t) < 1e-6)
                return row.at("on_air") == "1";
        FAIL("no row");
        return false;
    };
    CHECK(on("N2", 5.9));
    CHECK_FALSE(on("N2", 6.0));
    CHECK_FALSE(on("N2", 7.9));
    CHECK(on("N2", 8.0));
    CHECK_FALSE(on("N3", 1.0));

    const auto cells = read_csv(d.file("t.cells.csv"));
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].at("cell") == "N1");
    CHECK(cells[0].at("neighbours") == "N2:3608 N3:3616");
    CHECK(cells[2].at("on_air") == "0");
    CHECK(std::stod(cells[1].at("offset_hz")) == doctest::Approx(0.0));
}

TEST_CASE("yantra-gen: the record file holds the expected measurements")
{
    TempDir d("records");
    const Result r = run_gen({"--scenario", "failed_site", "--duration", "9", "--quiet", "--format", "cu8", "--out",
                              d.file("r.cu8")});
    REQUIRE(r.code == kExitOk);
    const gen::Scenario s = scenario_of({"--scenario", "failed_site", "--duration", "9"});
    gen::ScenarioEngine e(s);
    radios::RecordFileHeader h;
    std::vector<radios::RecordFileLine> lines;
    std::string error;
    REQUIRE_MESSAGE(radios::read_record_file(d.file("r.records.jsonl"), h, lines, error), error);
    CHECK(h.plugin.id == "yantra-gen");
    CHECK(h.plugin.technology_id == "tetra");
    CHECK((h.plugin.capabilities & YR_CAP_SIMULATED) != 0);
    CHECK((h.plugin.capabilities & YR_CAP_SELF_CALIBRATED) != 0);
    CHECK(h.created_utc_ns == 0);
    REQUIRE(h.schemas.size() == 2);
    CHECK(h.schemas == gen::truth_schemas());
    REQUIRE(h.plan.has_value());
    CHECK(h.plan->carriers_hz == std::vector<uint64_t>{390000000, 390200000, 390400000});
    const radios::SchemaDef& cs = h.schemas[0];
    const radios::SchemaDef& ns = h.schemas[1];
    CHECK(cs.fields[YR_CORE_LEVEL].unit == "dBm");

    std::map<int64_t, std::vector<const radios::Record*>> cell_records, neighbour_records;
    size_t serving_events = 0, end_events = 0, ends = 0;
    for (const auto& l : lines) {
        if (l.kind == radios::RecordFileLine::Kind::record) {
            CHECK((l.record.flags & YR_RECORD_SIMULATED) != 0);
            (l.record.schema_index == 0 ? cell_records : neighbour_records)[l.record.time.monotonic_ns].push_back(&l.record);
        } else if (l.kind == radios::RecordFileLine::Kind::event) {
            serving_events += l.event.event == YR_EVENT_SERVING_CHANGED;
            end_events += l.event.event == YR_EVENT_END_OF_STREAM;
        } else {
            ++ends;
            CHECK(l.end.records == lines.size() - serving_events - end_events - 1);
        }
    }
    CHECK(serving_events == 1);  // N1 stays the strongest
    CHECK(end_events == 1);
    CHECK(ends == 1);
    REQUIRE(cell_records.size() == 90);
    for (const auto& [ns_time, recs] : cell_records) {
        const double t = static_cast<double>(ns_time) / 1e9;
        const gen::TruthRow truth = e.truth_at(t);
        size_t on_air = 0;
        for (const auto& c : truth.cells)
            on_air += c.on_air;
        REQUIRE(recs.size() == on_air);
        for (const radios::Record* rec : recs) {
            const std::string id = rec->values[static_cast<size_t>(cs.field_index("cell_id"))].str;
            const gen::CellTruth* c = nullptr;
            for (const auto& x : truth.cells)
                if (x.cell == id)
                    c = &x;
            REQUIRE(c != nullptr);
            CHECK(c->on_air);
            CHECK(rec->values[YR_CORE_LEVEL].f64 == doctest::Approx(c->level_dbm));
            CHECK(rec->values[YR_CORE_FREQUENCY_HZ].u64 == static_cast<uint64_t>(c->frequency_hz));
            CHECK(rec->values[YR_CORE_MCC].u64 == 901);
            CHECK(rec->values[YR_CORE_MNC].u64 == 2);
            CHECK(rec->values[YR_CORE_LA].u64 == c->location_area);
            CHECK(rec->values[YR_CORE_COLOUR_CODE].u64 == c->colour_code);
            CHECK(rec->values[static_cast<size_t>(cs.field_index(YR_FIELD_FREQ_ERROR_HZ))].f64 ==
                  doctest::Approx(c->cfo_hz));
            const int64_t role = rec->values[YR_CORE_ROLE].i64;
            CHECK(role == (id == "N1" ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR));
            CHECK((rec->present & YR_GROUP_SIM_POSITION) == 0);  // no floors in this scenario
        }
    }
    // N1's neighbour list (N2, N3) at the start and every second.
    CHECK(neighbour_records.size() == 9);
    for (const auto& [ns_time, recs] : neighbour_records) {
        CHECK(ns_time % 1000000000 == 0);
        REQUIRE(recs.size() == 2);
        CHECK(recs[0]->values[YR_NEIGHBOUR_FREQUENCY_HZ].u64 == 390000000);
        CHECK(recs[1]->values[YR_NEIGHBOUR_ENTRY_INDEX].u64 == 1);
        CHECK(recs[1]->values[YR_NEIGHBOUR_ENTRY_COUNT].u64 == 2);
        CHECK(recs[1]->values[static_cast<size_t>(ns.field_index("main_carrier"))].u64 == 3616);
        CHECK(recs[1]->values[static_cast<size_t>(ns.field_index("neighbour_frequency_hz"))].u64 == 390400000);
        CHECK(recs[1]->values[static_cast<size_t>(ns.field_index("neighbour_cell_id"))].str == "N3");
    }
}

TEST_CASE("yantra-gen: records carry the receiver position and planted neighbour values")
{
    // A floor scenario: positions in the sim_position group.
    TempDir d("records_floor");
    Result r = run_gen({"--scenario", "floor_change", "--duration", "1", "--quiet", "--out", d.file("f.cf32")});
    REQUIRE(r.code == kExitOk);
    radios::RecordFileHeader h;
    std::vector<radios::RecordFileLine> lines;
    std::string error;
    REQUIRE_MESSAGE(radios::read_record_file(d.file("f.records.jsonl"), h, lines, error), error);
    const gen::Scenario s = scenario_of({"--scenario", "floor_change", "--duration", "1"});
    gen::ScenarioEngine e(s);
    size_t with_position = 0;
    for (const auto& l : lines) {
        if (l.kind != radios::RecordFileLine::Kind::record || l.record.schema_index != 0)
            continue;
        const auto pos = e.receiver_at(static_cast<double>(l.record.time.monotonic_ns) / 1e9);
        REQUIRE(pos.has_value());
        REQUIRE((l.record.present & YR_GROUP_SIM_POSITION) != 0);
        CHECK(l.record.values[static_cast<size_t>(h.schemas[0].field_index(YR_FIELD_SIM_FLOOR_ID))].str == pos->floor);
        CHECK(l.record.values[static_cast<size_t>(h.schemas[0].field_index(YR_FIELD_SIM_X_M))].f64 ==
              doctest::Approx(pos->x_m));
        ++with_position;
    }
    CHECK(with_position > 0);

    // A planted neighbour carrier and LA (scenario file in the temp dir).
    std::string text = R"({"format":"yantra-radios-scenario","version":1,"id":"planted","duration_s":0.2,
      "network":{"mcc":901,"mnc":3},
      "cells":[{"id":"A","cell_number":1,"la":5,"colour_code":1,"main_carrier":3600,"level_dbm":-60,
                "neighbours":[{"cell":"B","main_carrier":3606,"la":77}]},
               {"id":"B","cell_number":2,"la":6,"colour_code":2,"main_carrier":3604,"level_dbm":-80}]})";
    spit(d.file("planted.json"), text);
    r = run_gen({"--scenario", d.file("planted.json"), "--quiet", "--out", d.file("p.cf32")});
    REQUIRE(r.code == kExitOk);
    lines.clear();
    REQUIRE_MESSAGE(radios::read_record_file(d.file("p.records.jsonl"), h, lines, error), error);
    size_t n = 0;
    for (const auto& l : lines) {
        if (l.kind != radios::RecordFileLine::Kind::record || l.record.schema_index != 1)
            continue;
        const radios::SchemaDef& ns = h.schemas[1];
        CHECK(l.record.values[static_cast<size_t>(ns.field_index("main_carrier"))].u64 == 3606);
        CHECK(l.record.values[static_cast<size_t>(ns.field_index("neighbour_la"))].u64 == 77);
        CHECK(l.record.values[static_cast<size_t>(ns.field_index("neighbour_frequency_hz"))].u64 == 390150000);
        ++n;
    }
    CHECK(n == 1);
    CHECK(read_csv(d.file("p.cells.csv"))[0].at("neighbours") == "B:3606:LA77");
}

TEST_CASE("yantra-gen: example ids select their scenario")
{
    for (const auto& [example, id] : std::vector<std::pair<std::string, std::string>>{
             {"fraunces-first-floor", "fraunces"},
             {"mak-villa-bunzl-ground", "mak_villa_bunzl"},
             {"unknown-plan", "indoor_good_das"},
             {"", "indoor_good_das"}}) {
        CAPTURE(example);
        Options o = parse_args(example.empty() ? std::vector<std::string>{"--out", "x"}
                                               : std::vector<std::string>{"--example", example, "--out", "x"});
        CHECK(load_scenario(o, {kScenarioDir}).id == id);
    }
}

TEST_CASE("yantra-gen: impairment options reach the scenario")
{
    const gen::Scenario s = scenario_of(with(
        kTwoCells, {"--noise-figure", "11", "--ppm", "1.5", "--clock-ppm", "-2", "--gain-db", "3", "--iq-gain-db",
                    "0.5", "--iq-phase-deg", "2", "--swap-iq", "--dc-dbfs", "-30", "--fading", "rician",
                    "--rician-k", "9", "--doppler", "12", "--doppler-shift", "40", "--drops", "0.5,1,100", "--rate",
                    "1M", "--centre", "390.05M", "--truth-interval", "0.05", "--duration", "3"}));
    CHECK(s.receiver.noise_figure_db == 11.0);
    CHECK(s.receiver.ppm == 1.5);
    CHECK(s.receiver.clock_ppm == -2.0);
    CHECK(s.receiver.gain_db == 3.0);
    CHECK(s.receiver.iq_gain_db == 0.5);
    CHECK(s.receiver.iq_phase_deg == 2.0);
    CHECK(s.receiver.swap_iq);
    CHECK(s.receiver.dc_dbfs == -30.0);
    CHECK(s.channel.fading == gen::FadingModel::rician);
    CHECK(s.channel.rician_k_db == 9.0);
    CHECK(s.channel.doppler_hz == 12.0);
    CHECK(s.channel.doppler_shift_hz == 40.0);
    CHECK(s.receiver.drops_first_s == 0.5);
    CHECK(s.receiver.drops_interval_s == 1.0);
    CHECK(s.receiver.drops_samples == 100);
    CHECK(s.sample_rate_hz == 1e6);
    CHECK(s.centre_frequency_hz == 390.05e6);
    CHECK(s.truth_interval_s == 0.05);
    CHECK(s.duration_s == 3.0);
    CHECK(s.mcc == 901);
    CHECK(s.mnc == 77);
    CHECK(s.cells.size() == 2);
    CHECK(s.cells[1].encryption);
    CHECK(s.cells[1].ppm == 0.5);
    CHECK(scenario_of(with(kTwoCells, {"--no-noise"})).receiver.noise == false);
}

TEST_CASE("yantra-gen: same seed, same files; another seed, other samples" * doctest::test_suite("gen-cli-golden"))
{
    TempDir d("golden");
    // Seeded cases: every format, impairments, fading, drops, a floor
    // scenario. The digest covers the samples, the truth CSV and the
    // record lines after the header (the header and the SigMF metadata
    // carry the version number).
    struct Case {
        std::string name;
        std::vector<std::string> args;
    };
    const std::vector<Case> cases = {
        {"cells_cf32", with(kTwoCells, {"--duration", "0.2"})},
        {"cells_cs16_impaired",
         with(kTwoCells, {"--duration", "0.2", "--format", "cs16", "--fading", "rayleigh", "--doppler", "20",
                          "--ppm", "1", "--clock-ppm", "3", "--iq-gain-db", "0.4", "--dc-dbfs", "-35",
                          "--drops", "0.05,0.07,50"})},
        {"failed_site_cs8", {"--scenario", "failed_site", "--duration", "0.3", "--format", "cs8"}},
        {"floor_change_cu8", {"--scenario", "floor_change", "--duration", "0.3", "--format", "cu8", "--rate", "1M"}},
        {"weak_signal_tu50", {"--scenario", "weak_signal", "--duration", "0.2", "--fading", "tu50", "--seed", "99"}},
    };
    std::string text;
    for (const Case& c : cases) {
        CAPTURE(c.name);
        std::string digest[2];
        for (int rep = 0; rep < 2; ++rep) {
            const std::string stem = d.file(c.name + std::to_string(rep));
            const Result r = run_gen(with(c.args, {"--quiet", "--out", stem + ".iq"}));
            REQUIRE(r.code == kExitOk);
            gen::Sha256 h;
            for (const std::string& f : {stem + ".iq", stem + ".truth.csv"}) {
                const std::string b = slurp(f);
                h.update(b.data(), b.size());
            }
            const auto rec = lines_of(slurp(stem + ".records.jsonl"));
            for (size_t i = 1; i < rec.size(); ++i)
                h.update(rec[i].data(), rec[i].size());
            digest[rep] = h.hex();
        }
        CHECK(digest[0] == digest[1]);
        text += c.name + " " + digest[0] + "\n";
    }
    // Another seed changes the samples.
    REQUIRE(run_gen(with(kTwoCells, {"--duration", "0.2", "--quiet", "--seed", "8", "--out", d.file("s8.iq")})).code == 0);
    CHECK(slurp(d.file("s8.iq")) != slurp(d.file("cells_cf320.iq")));

    const std::string path = kGoldenDir + "/digests.txt";
    if (yantra::tests::update_golden()) {
        spit(path, text);
        MESSAGE("rewrote " << path);
    } else {
        REQUIRE_MESSAGE(fs::exists(path), "golden file missing: run test_yantra_gen --update");
        CHECK(slurp(path) == text);
    }
}
