// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// decode-iq (RAD-63) end to end: yantra-gen (RAD-71) writes IQ and its
// ground-truth record file; decode-iq runs the IQ through the measurement
// engine (RAD-102, RAD-110..113); the decoded records must match the
// truth: identity, role, level (calibrated from SigMF, or dBFS plus the
// full scale for raw files), C1 and its inputs, broadcast values,
// frequency error and the neighbour list; events where the scenario
// switches cells. Every format (cf32, cs16, cs8, cu8; raw and SigMF), a
// failed site and impairments (LO and clock error, IQ imbalance, DC,
// sample drops).

#include <doctest/doctest.h>

#include "decode_iq.hpp"
#include "gen_app.hpp"
#include "iq_file.hpp"

#include <yantra/radios/record_file.hpp>

#include "test_platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
namespace radios = yantra::radios;
using namespace yantra::radio_host;

namespace {

const std::string kScenarioDir = YANTRA_SCENARIO_DIR;

struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::current_path() / "decode_iq_test" / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::remove(path.parent_path(), ec);  // only when empty
    }
    std::string file(const std::string& n) const { return (path / n).string(); }
};

void generate(std::vector<std::string> args)
{
    args.insert(args.begin(), {"--scenario-dir", kScenarioDir, "--quiet"});
    std::ostringstream out, err;
    const int code = yantra::gentool::run(args, out, err);
    INFO(err.str());
    REQUIRE(code == 0);
}

struct File {
    radios::RecordFileHeader h;
    std::vector<radios::RecordFileLine> lines;
    explicit File(const std::string& path)
    {
        std::string error;
        REQUIRE_MESSAGE(radios::read_record_file(path, h, lines, error), error);
    }
    const radios::SchemaDef& schema(const radios::Record& r) const { return h.schemas[r.schema_index]; }
    bool is(const radios::Record& r, const char* id) const { return schema(r).id == id; }
    std::optional<double> num(const radios::Record& r, const char* name) const
    {
        const int i = schema(r).field_index(name);
        if (i < 0)
            return std::nullopt;
        const radios::Value& v = r.values[static_cast<std::size_t>(i)];
        switch (v.type) {
        case YR_TYPE_DOUBLE: return v.f64;
        case YR_TYPE_UINT: return static_cast<double>(v.u64);
        case YR_TYPE_INT:
        case YR_TYPE_ENUM:
        case YR_TYPE_BOOL: return static_cast<double>(v.i64);
        default: return std::nullopt;
        }
    }
    std::vector<const radios::Record*> records(const char* id) const
    {
        std::vector<const radios::Record*> v;
        for (const radios::RecordFileLine& l : lines)
            if (l.kind == radios::RecordFileLine::Kind::record && is(l.record, id))
                v.push_back(&l.record);
        return v;
    }
    std::vector<radios::Event> events(uint32_t kind) const
    {
        std::vector<radios::Event> v;
        for (const radios::RecordFileLine& l : lines)
            if (l.kind == radios::RecordFileLine::Kind::event && l.event.event == kind)
                v.push_back(l.event);
        return v;
    }
};

struct Compared {
    int records = 0;    ///< decoded synchronised records compared
    int with_quality = 0;  ///< of those, with the quality group
    int neighbour_lists = 0;
    std::set<uint64_t> frequencies;
};

/// Compares every synchronised decoded tetra_cell record with the truth
/// rows of its interval (+/- half an interval around its time) for which
/// the cell was on the air throughout; `dbfs_to_dbm` converts decoded dBFS
/// levels (0 for calibrated files).
Compared compare(const File& truth, const File& dec, double dbfs_to_dbm, double level_tol_db)
{
    Compared c;
    std::map<uint64_t, std::vector<const radios::Record*>> by_freq;
    for (const radios::Record* r : truth.records("tetra_cell"))
        by_freq[static_cast<uint64_t>(*truth.num(*r, "frequency_hz"))].push_back(r);
    // Neighbour roles come from the serving cell's D-NWRK-BROADCAST: not
    // before the decoder received the list.
    int64_t first_list_ns = INT64_MAX;
    for (const radios::Record* r : dec.records("tetra_neighbour"))
        first_list_ns = std::min(first_list_ns, r->time.monotonic_ns);
    for (const radios::Record* d : dec.records("tetra_cell")) {
        if ((d->flags & YR_RECORD_NO_SYNC) != 0)
            continue;
        const auto f = static_cast<uint64_t>(*dec.num(*d, "frequency_hz"));
        const double t = static_cast<double>(d->time.monotonic_ns) * 1e-9;
        CAPTURE(f);
        CAPTURE(t);
        REQUIRE_MESSAGE(by_freq.count(f) == 1, "decoded a carrier that is not in the truth");
        std::vector<const radios::Record*> rows;
        for (const radios::Record* r : by_freq[f]) {
            const double rt = static_cast<double>(r->time.monotonic_ns) * 1e-9;
            if (rt >= t - 0.5 - 1e-6 && rt < t + 0.5 - 1e-6)
                rows.push_back(r);
        }
        if (rows.size() < 10)
            continue;  // off the air during part of the interval
        double p = 0.0;
        for (const radios::Record* r : rows)
            p += std::pow(10.0, *truth.num(*r, "level") / 10.0);
        const double truth_level = 10.0 * std::log10(p / static_cast<double>(rows.size()));
        const radios::Record& tr = *rows[rows.size() / 2];
        const double level = *dec.num(*d, "level") + dbfs_to_dbm;
        CHECK(std::fabs(level - truth_level) < level_tol_db);
        for (const char* k : {"mcc", "mnc", "la", "colour_code", "channel", "role", "encryption", "ms_txpwr_max_cell",
                              "rxlev_access_min", "ms_txpwr_max_cell_dbm", "rxlev_access_min_dbm", "main_carrier",
                              "frequency_band"}) {
            const std::string key = k;
            CAPTURE(key);
            if (key == "role" && *truth.num(tr, k) != YR_ROLE_SERVING && d->time.monotonic_ns < first_list_ns)
                continue;
            REQUIRE(truth.num(tr, k));
            REQUIRE(dec.num(*d, k));
            CHECK(*dec.num(*d, k) == *truth.num(tr, k));
        }
        if (dbfs_to_dbm == 0.0) {
            REQUIRE(dec.num(*d, "c1_db"));
            // C1 - level depends on the broadcast values only.
            CHECK(*dec.num(*d, "c1_db") - level ==
                  doctest::Approx(*truth.num(tr, "c1_db") - *truth.num(tr, "level")));
        } else {
            CHECK_FALSE(dec.num(*d, "c1_db"));
        }
        // Quality from the decoder's first full window on.
        if (const auto fe = dec.num(*d, "freq_error_hz")) {
            CHECK(std::fabs(*fe - *truth.num(tr, "freq_error_hz")) < 15.0);
            ++c.with_quality;
        }
        ++c.records;
        c.frequencies.insert(f);
    }
    // Neighbour lists: the same entries as the truth's of that carrier.
    using Entry = std::tuple<double, double, double, std::optional<double>>;
    auto entries = [](const File& file, uint64_t from) {
        std::set<Entry> s;
        for (const radios::Record* r : file.records("tetra_neighbour"))
            if (static_cast<uint64_t>(*file.num(*r, "frequency_hz")) == from)
                s.insert({*file.num(*r, "cell_number"), *file.num(*r, "main_carrier"),
                          *file.num(*r, "neighbour_frequency_hz"), file.num(*r, "neighbour_la")});
        return s;
    };
    std::set<uint64_t> lists;
    for (const radios::Record* r : dec.records("tetra_neighbour"))
        lists.insert(static_cast<uint64_t>(*dec.num(*r, "frequency_hz")));
    for (uint64_t from : lists) {
        CAPTURE(from);
        CHECK(entries(dec, from) == entries(truth, from));
        ++c.neighbour_lists;
    }
    return c;
}

std::string stem_of(const std::string& data) { return fs::path(data).replace_extension("").string(); }

}  // namespace

TEST_CASE("decode-iq: failed site, cs8 SigMF, calibrated, against the ground truth (RAD-63, RAD-71)")
{
    TempDir d("failed_site");
    const std::string data = d.file("fs.sigmf-data");
    generate({"--scenario", "failed_site", "--format", "cs8", "--out", data});
    DecodeIqOptions o;
    o.iq = stem_of(data) + ".sigmf-meta";
    o.format = "sigmf";
    o.out = d.file("decoded.jsonl");
    DecodeIqResult r;
    std::string error;
    REQUIRE_MESSAGE(decode_iq(o, r, error), error);
    CHECK(r.calibrated);
    CHECK(r.samples == 5'000'000);
    CHECK(r.segments == 1);
    CHECK(r.clipped_blocks == 0);
    const File truth(stem_of(data) + ".records.jsonl");
    const File dec(o.out);
    CHECK(dec.h.plugin.capabilities == (YR_CAP_SELF_CALIBRATED | YR_CAP_SIMULATED));
    CHECK(dec.h.schemas[0].id == truth.h.schemas[0].id);
    CHECK(dec.h.schemas[1].id == truth.h.schemas[1].id);
    const Compared c = compare(truth, dec, 0.0, 0.5);
    CHECK(c.records >= 15);
    CHECK(c.with_quality >= c.records - 2);
    // N1 and N2; N3 is announced but off the air.
    CHECK(c.frequencies == std::set<uint64_t>{390000000, 390200000});
    for (const radios::Record* x : dec.records("tetra_cell"))
        CHECK(static_cast<uint64_t>(*dec.num(*x, "frequency_hz")) != 390400000);
    CHECK(c.neighbour_lists == 1);  // N1 serves throughout
    // N2 goes off at 6 s and comes back at 8 s.
    bool lost = false, found_again = false;
    for (const radios::Event& e : dec.events(YR_EVENT_SYNC_LOST))
        lost = lost || (e.value == 390200000 && e.monotonic_ns > 6'000'000'000 && e.monotonic_ns < 7'200'000'000);
    for (const radios::Event& e : dec.events(YR_EVENT_SYNC_FOUND))
        found_again = found_again || (e.value == 390200000 && e.monotonic_ns > 8'000'000'000);
    CHECK(lost);
    CHECK(found_again);
    const auto serving = dec.events(YR_EVENT_SERVING_CHANGED);
    REQUIRE(serving.size() == 1);
    CHECK(serving[0].value == 390000000);
    CHECK(dec.lines.back().kind == radios::RecordFileLine::Kind::end);
}

TEST_CASE("decode-iq: every format, raw files with --rate and --center, levels in dBFS")
{
    const std::vector<std::string> cells = {
        "--cell", "id=A,carrier=3600,la=10,cc=5,number=1,level=-60,neighbours=B",
        "--cell", "id=B,carrier=3604,la=11,cc=6,number=2,level=-75,ppm=0.5,encryption=1,neighbours=A",
        "--mcc", "901", "--mnc", "77", "--seed", "7", "--duration", "5"};
    for (const char* format : {"cf32", "cs16", "cs8", "cu8"}) {
        CAPTURE(format);
        TempDir d(std::string("format_") + format);
        const std::string data = d.file("x.sigmf-data");
        std::vector<std::string> args = cells;
        args.insert(args.end(), {"--format", format, "--out", data});
        generate(args);
        IqMeta meta;
        std::string error;
        REQUIRE_MESSAGE(read_sigmf_meta(data, meta, error), error);
        REQUIRE(meta.full_scale_dbm);
        DecodeIqOptions o;
        o.iq = data;  // as a raw file
        o.format = format;
        o.rate_hz = meta.sample_rate_hz;
        o.centre_hz = meta.centre_hz;
        o.out = d.file("decoded.jsonl");
        DecodeIqResult r;
        REQUIRE_MESSAGE(decode_iq(o, r, error), error);
        CHECK_FALSE(r.calibrated);
        CHECK(r.synced == 2);
        const File truth(stem_of(data) + ".records.jsonl");
        const File dec(o.out);
        CHECK(dec.h.schemas[0].fields[YR_CORE_LEVEL].unit == "dBFS");
        const Compared c = compare(truth, dec, *meta.full_scale_dbm, 0.5);
        CHECK(c.records >= 4);
        CHECK(c.with_quality >= c.records - 2);
        CHECK(c.frequencies.size() == 2);
        CHECK(c.neighbour_lists == 1);
    }
}

TEST_CASE("decode-iq: impairments: LO and clock error, IQ imbalance, DC, sample drops (RAD-72)")
{
    TempDir d("impaired");
    const std::string data = d.file("x.sigmf-data");
    generate({"--cell", "id=A,carrier=3600,la=10,cc=5,number=1,level=-60,neighbours=B", "--cell",
              "id=B,carrier=3604,la=11,cc=6,number=2,level=-70,neighbours=A", "--mcc", "901", "--mnc", "77",
              "--seed", "11", "--duration", "8", "--format", "cs16", "--ppm", "1", "--clock-ppm", "3",
              "--iq-gain-db", "0.4", "--dc-dbfs", "-35", "--drops", "2.0,3.0,50", "--out", data});
    DecodeIqOptions o;
    o.iq = data;
    o.format = "sigmf";
    o.out = d.file("decoded.jsonl");
    o.threads = 2;
    DecodeIqResult r;
    std::string error;
    REQUIRE_MESSAGE(decode_iq(o, r, error), error);
    CHECK(r.segments == 3);  // drops at 2 s and 5 s
    CHECK(r.synced == 2);
    const File truth(stem_of(data) + ".records.jsonl");
    const File dec(o.out);
    const Compared c = compare(truth, dec, 0.0, 1.0);
    CHECK(c.records >= 4);
    CHECK(c.frequencies.size() == 2);
    // Each drop restarts the carriers: two more SYNC_FOUND per carrier.
    CHECK(dec.events(YR_EVENT_SYNC_FOUND).size() >= 4);
}

TEST_CASE("decode-iq: --gsmtap sends the broadcast blocks to a UDP port (RAD-115)")
{
    // A receiver on an ephemeral loopback port, closed at the end.
    std::optional<yantra::test::LoopbackUdpReceiver> rx(std::in_place);

    TempDir d("gsmtap");
    const std::string data = d.file("x.sigmf-data");
    generate({"--cell", "id=A,carrier=3600,la=10,cc=5,number=1,level=-60", "--mcc", "901", "--mnc", "77", "--seed",
              "3", "--duration", "1.5", "--format", "cs16", "--out", data});
    DecodeIqOptions o;
    o.iq = data;
    o.format = "sigmf";
    o.out = "";
    o.gsmtap_host = "127.0.0.1";
    o.gsmtap_port = rx->port();
    DecodeIqResult r;
    std::string error;
    const bool ok = decode_iq(o, r, error);
    // The whole run is queued at the socket (up to its buffer): read it.
    std::size_t got = 0;
    bool headers_ok = true;
    while (auto buf = rx->receive(512)) {
        ++got;
        headers_ok = headers_ok && buf->size() > 16 && (*buf)[0] == 2 && (*buf)[2] == 0x05;
    }
    rx.reset();
    REQUIRE_MESSAGE(ok, error);
    CHECK(r.gsmtap_sent > 0);
    CHECK(got > 0);
    CHECK(got <= r.gsmtap_sent);
    CHECK(headers_ok);

    o.gsmtap_host = "localhost";  // names are never looked up
    CHECK_FALSE(decode_iq(o, r, error));
    CHECK(error.find("numeric") != std::string::npos);
}

TEST_CASE("decode-iq: file and option errors")
{
    DecodeIqOptions o;
    o.iq = "no_such_file.cf32";
    o.format = "cf32";
    DecodeIqResult r;
    std::string error;
    CHECK_FALSE(decode_iq(o, r, error));
    CHECK(error.find("--rate") != std::string::npos);
    o.rate_hz = 500000.0;
    CHECK_FALSE(decode_iq(o, r, error));
    CHECK(error.find("--center") != std::string::npos);
    o.centre_hz = 390e6;
    o.out = "";
    CHECK_FALSE(decode_iq(o, r, error));
    CHECK(error.find("cannot open") != std::string::npos);
    o.format = "ci16_le";
    CHECK_FALSE(decode_iq(o, r, error));
    o.format = "sigmf";
    CHECK_FALSE(decode_iq(o, r, error));
    CHECK(error.find("cannot open") != std::string::npos);
}
