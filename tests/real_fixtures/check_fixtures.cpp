// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-check-fixtures ROOT WORK: the real-signal fixture check (RAD-121,
// REAL_WORLD_CHECKLIST.md). Every ROOT/<radio>/NAME.expected.json names a
// recording NAME.sigmf-meta/-data next to it; the recording is decoded
// with decode-iq (the TETRA measurement engine) into WORK/ and the
// decoded cells are compared with the expected ones: every expected cell
// must be decoded (downlink frequency, MCC, MNC, colour code, location
// area, and the encryption flag when given); with "complete": true no
// other cell may be. Recordings over 30 s or 32 MiB or below 250 ksps
// are refused. Without fixtures it says so and passes (RAD-121 BLOCKED).
// Exit 0 when every fixture passes, 1 otherwise.

#include "decode_iq.hpp"

#include <yantra/radio.h>
#include <yantra/radios/record_file.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
namespace radios = yantra::radios;
using yantra::radio_host::DecodeIqOptions;
using yantra::radio_host::DecodeIqResult;

namespace {

constexpr double kMaxSeconds = 30.5;
constexpr std::uintmax_t kMaxBytes = 32u * 1024u * 1024u;
constexpr double kMinRate = 250000.0;
constexpr double kMatchHz = 3000.0;  // measured carrier vs SYSINFO downlink

struct Cell {
    double downlink_hz = 0.0;
    int64_t mcc = -1, mnc = -1, colour_code = -1, la = -1;
    std::optional<bool> encryption;
    bool operator<(const Cell& o) const
    {
        return std::tie(downlink_hz, mcc, mnc, colour_code, la, encryption) <
               std::tie(o.downlink_hz, o.mcc, o.mnc, o.colour_code, o.la, o.encryption);
    }
    std::string text() const
    {
        char b[160];
        std::snprintf(b, sizeof b, "%.0f Hz MCC %lld MNC %lld CC %lld LA %lld%s", downlink_hz,
                      static_cast<long long>(mcc), static_cast<long long>(mnc), static_cast<long long>(colour_code),
                      static_cast<long long>(la), encryption ? (*encryption ? " encrypted" : " clear") : "");
        return b;
    }
};

std::optional<double> number(const radios::SchemaDef& s, const radios::Record& r, const char* name)
{
    const int i = s.field_index(name);
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

bool matches(const Cell& want, const Cell& got)
{
    return std::fabs(want.downlink_hz - got.downlink_hz) <= kMatchHz && want.mcc == got.mcc &&
           want.mnc == got.mnc && want.colour_code == got.colour_code && want.la == got.la &&
           (!want.encryption || want.encryption == got.encryption);
}

/// Checks one fixture; prints why it fails.
bool check(const fs::path& expected_path, const fs::path& work)
{
    const std::string name = expected_path.filename().string();
    const fs::path stem = expected_path.parent_path() / name.substr(0, name.size() - std::string(".expected.json").size());
    const fs::path meta = fs::path(stem.string() + ".sigmf-meta");
    const fs::path data = fs::path(stem.string() + ".sigmf-data");
    std::printf("%s\n", stem.string().c_str());
    if (!fs::exists(meta) || !fs::exists(data)) {
        std::printf("  FAIL: %s or %s missing\n", meta.filename().string().c_str(), data.filename().string().c_str());
        return false;
    }
    if (fs::file_size(data) > kMaxBytes) {
        std::printf("  FAIL: recording larger than 32 MiB\n");
        return false;
    }
    nlohmann::json j;
    try {
        std::ifstream in(expected_path);
        j = nlohmann::json::parse(in);
        if (j.at("format").get<std::string>() != "yantra-radios-fixture" || j.at("version").get<int>() != 1)
            throw std::runtime_error("not a yantra-radios-fixture version 1 file");
    } catch (const std::exception& e) {
        std::printf("  FAIL: %s: %s\n", expected_path.filename().string().c_str(), e.what());
        return false;
    }
    std::vector<Cell> want;
    try {
        for (const auto& c : j.at("cells")) {
            Cell x;
            x.downlink_hz = c.at("downlink_hz").get<double>();
            x.mcc = c.at("mcc").get<int64_t>();
            x.mnc = c.at("mnc").get<int64_t>();
            x.colour_code = c.at("colour_code").get<int64_t>();
            x.la = c.at("location_area").get<int64_t>();
            if (c.contains("air_encryption"))
                x.encryption = c.at("air_encryption").get<bool>();
            want.push_back(x);
        }
    } catch (const std::exception& e) {
        std::printf("  FAIL: cells: %s\n", e.what());
        return false;
    }
    const bool complete = j.value("complete", false);

    fs::create_directories(work);
    DecodeIqOptions o;
    o.iq = meta.string();
    o.format = "sigmf";
    o.out = (work / (stem.filename().string() + ".records.jsonl")).string();
    DecodeIqResult res;
    std::string error;
    if (!yantra::radio_host::decode_iq(o, res, error)) {
        std::printf("  FAIL: decode-iq: %s\n", error.c_str());
        return false;
    }
    if (res.seconds > kMaxSeconds || (res.seconds > 0.0 && static_cast<double>(res.samples) / res.seconds < kMinRate)) {
        std::printf("  FAIL: %.1f s at %.0f samples/s (at most 30 s, at least 250 ksps)\n", res.seconds,
                    res.seconds > 0.0 ? static_cast<double>(res.samples) / res.seconds : 0.0);
        return false;
    }
    radios::RecordFileHeader header;
    std::vector<radios::RecordFileLine> lines;
    if (!radios::read_record_file(o.out, header, lines, error)) {
        std::printf("  FAIL: %s\n", error.c_str());
        return false;
    }
    std::set<Cell> got;
    for (const radios::RecordFileLine& l : lines) {
        if (l.kind != radios::RecordFileLine::Kind::record || (l.record.flags & YR_RECORD_NO_SYNC))
            continue;
        const radios::SchemaDef& s = header.schemas.at(l.record.schema_index);
        if (s.id != "tetra_cell")
            continue;
        const auto la = number(s, l.record, YR_FIELD_LA);
        const auto f = number(s, l.record, YR_FIELD_FREQUENCY_HZ);
        if (!la || !f)
            continue;  // SYSINFO not decoded yet
        Cell c;
        c.downlink_hz = *f;
        c.mcc = static_cast<int64_t>(number(s, l.record, YR_FIELD_MCC).value_or(-1));
        c.mnc = static_cast<int64_t>(number(s, l.record, YR_FIELD_MNC).value_or(-1));
        c.colour_code = static_cast<int64_t>(number(s, l.record, YR_FIELD_COLOUR_CODE).value_or(-1));
        c.la = static_cast<int64_t>(*la);
        if (const auto e = number(s, l.record, "encryption"))
            c.encryption = *e != 0.0;
        // The SYSINFO downlink frequency when given, else the measured one
        // rounded to the 6.25 kHz grid.
        if (const auto d = number(s, l.record, "mcch_frequency_hz"))
            c.downlink_hz = *d;
        else
            c.downlink_hz = std::round(*f / 6250.0) * 6250.0;
        got.insert(c);
    }
    bool ok = true;
    for (const Cell& w : want) {
        bool found = false;
        for (const Cell& g : got)
            found = found || matches(w, g);
        std::printf("  %s %s\n", found ? "ok     " : "MISSING", w.text().c_str());
        ok = ok && found;
    }
    for (const Cell& g : got) {
        bool known = false;
        for (const Cell& w : want)
            known = known || matches(w, g);
        if (!known) {
            std::printf("  %s %s\n", complete ? "EXTRA  " : "other  ", g.text().c_str());
            ok = ok && !complete;
        }
    }
    std::printf("  %s (%.1f s, %llu records)\n", ok ? "PASS" : "FAIL", res.seconds,
                static_cast<unsigned long long>(res.records));
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: yantra-check-fixtures ROOT WORK\n");
        return 2;
    }
    const fs::path root = argv[1];
    std::vector<fs::path> fixtures;
    if (fs::is_directory(root))
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            const std::string n = e.path().filename().string();
            if (e.is_regular_file() && n.size() > 14 && n.ends_with(".expected.json"))
                fixtures.push_back(e.path());
        }
    std::sort(fixtures.begin(), fixtures.end());
    if (fixtures.empty()) {
        std::printf("no real-signal fixtures in %s: RAD-121 is BLOCKED until real recordings are added\n",
                    root.string().c_str());
        return 0;
    }
    int failed = 0;
    for (const fs::path& p : fixtures)
        failed += check(p, argv[2]) ? 0 : 1;
    std::printf("%zu fixtures, %d failed\n", fixtures.size(), failed);
    return failed == 0 ? 0 : 1;
}
