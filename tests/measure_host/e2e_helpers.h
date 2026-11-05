// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The record files of the radios' scenarios (gen_records.cmake) and their
// ground truth, shared by the TST-10 cases (test_measure_e2e.cpp) and the
// cell database cases (test_measure_celldb.cpp). Moved unchanged from
// test_measure_e2e.cpp (made inline).
#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <yantra/core/measure/record_file.h>

#include "measure_rig.h"

namespace mh {

inline constexpr std::int64_t kTenth = 100'000'000;  // the truth interval of the scenarios

inline fs::path records_dir() { return fs::path(E2E_RECORDS_DIR); }

inline bool have_records(const char* what) {
    if (std::string(E2E_RECORDS_DIR).empty() || !fs::exists(records_dir() / "fraunces.records.jsonl")) {
        MESSAGE("no record files of the radios' scenarios (radio tools not built): skipped " << what);
        return false;
    }
    return true;
}

// A CSV file with a header line: one map per row (no quoted commas: the
// generator's CSV files have none).
inline std::vector<std::map<std::string, std::string>> read_csv(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE_MESSAGE(in, path.string());
    auto split = [](const std::string& line) {
        std::vector<std::string> out;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) out.push_back(cell);
        if (!line.empty() && line.back() == ',') out.emplace_back();
        return out;
    };
    std::string line;
    std::getline(in, line);
    const auto head = split(line);
    std::vector<std::map<std::string, std::string>> rows;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cells = split(line);
        std::map<std::string, std::string> row;
        for (std::size_t i = 0; i < head.size() && i < cells.size(); ++i) row[head[i]] = cells[i];
        rows.push_back(std::move(row));
    }
    return rows;
}

// What a scenario's cells broadcast (STEM.cells.csv) and their true level
// per 0.1 s (STEM.truth.csv) of a yantra-gen run.
struct Truth {
    struct Cell {
        std::string id;
        std::uint64_t mcc = 0, mnc = 0, la = 0, colour_code = 0, frequency_hz = 0;
        bool on_air = false;
        std::vector<std::string> neighbours;  // cell ids
    };
    struct Row {
        bool on_air = false;
        double level_dbm = 0.0;
        int walls = 0;
    };
    std::map<std::string, Cell> cells;
    std::map<std::uint64_t, std::string> by_frequency;
    std::map<std::pair<std::int64_t, std::string>, Row> rows;  // (tenths of a second, cell)

    explicit Truth(const std::string& id) {
        for (const auto& r : read_csv(records_dir() / (id + ".cells.csv"))) {
            Cell c;
            c.id = r.at("cell");
            c.mcc = std::stoull(r.at("mcc"));
            c.mnc = std::stoull(r.at("mnc"));
            c.la = std::stoull(r.at("la"));
            c.colour_code = std::stoull(r.at("colour_code"));
            c.frequency_hz = static_cast<std::uint64_t>(std::llround(std::stod(r.at("frequency_hz"))));
            c.on_air = r.at("on_air") == "1";
            std::stringstream ss(r.at("neighbours"));
            std::string n;
            while (ss >> n) c.neighbours.push_back(n.substr(0, n.find(':')));
            by_frequency[c.frequency_hz] = c.id;
            cells[c.id] = std::move(c);
        }
        for (const auto& r : read_csv(records_dir() / (id + ".truth.csv"))) {
            Row row;
            row.on_air = r.at("on_air") == "1";
            row.level_dbm = std::stod(r.at("level_dbm"));
            row.walls = r.at("walls").empty() ? 0 : std::stoi(r.at("walls"));
            rows[{std::llround(std::stod(r.at("t_s")) * 10.0), r.at("cell")}] = row;
        }
    }
    // On air at any truth row in [from, to) (monotonic ns).
    bool on_air_between(const std::string& cell, std::int64_t from, std::int64_t to) const {
        for (std::int64_t k = (from + kTenth - 1) / kTenth; k * kTenth < to; ++k)
            if (auto it = rows.find({k, cell}); it != rows.end() && it->second.on_air) return true;
        return false;
    }
};

// A value of a mapped record by field name, as a number; empty when absent.
inline std::optional<double> num(const ym::MappedRecord& m, const char* name) {
    const auto i = m.schema->field_index(name);
    if (!i) return std::nullopt;
    const yp::Value& v = m.record.values[*i];
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    if (const auto* s = std::get_if<std::int64_t>(&v)) return static_cast<double>(*s);
    if (const auto* e = std::get_if<yp::EnumValue>(&v)) return static_cast<double>(e->value);
    return std::nullopt;
}
inline std::string text(const ym::MappedRecord& m, const char* name) {
    const auto i = m.schema->field_index(name);
    if (!i) return {};
    const auto* s = std::get_if<std::string>(&m.record.values[*i]);
    return s ? *s : std::string{};
}

// C1 by hand (EN 300 392-2 clause 23): level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS power).
inline double c1_by_hand(const ym::MappedRecord& m, double level_dbm, double ms_dbm = 30.0) {
    return level_dbm - *num(m, "rxlev_access_min_dbm") - std::max(0.0, *num(m, "ms_txpwr_max_cell_dbm") - ms_dbm);
}

inline ym::RecordFile load(const std::string& name) {
    const auto p = ym::read_record_file(records_dir() / name);
    std::string errors;
    for (const auto& e : p.errors) errors += e + "\n";
    INFO(errors);
    REQUIRE(p.ok());
    return *p.value;
}

inline std::int64_t last_time(const ym::RecordFile& f) {
    std::int64_t t = 0;
    for (const ym::RecordFileLine& l : f.lines) {
        if (const auto* r = std::get_if<ym::RadioRecord>(&l)) t = std::max(t, r->record.time.monotonic_ns);
        else t = std::max(t, std::get<ym::RadioEvent>(l).monotonic_ns);
    }
    return t;
}

// A record file replayed into a pipeline with the TETRA plugin on a
// ManualClock that follows the recorded times (cycles as live, TST-31).
struct FileRun {
    Rig rig{false, 4096, std::int64_t{0}};
    TetraPlugin tetra{rig};

    std::size_t run(const ym::RecordFile& f, ym::ReplayOptions o = {}) {
        o.advance = [this](std::int64_t t) { rig.advance_to(t); };
        const std::size_t n = ym::replay(f, *rig.pipeline, o);
        rig.advance_to(last_time(f) + kSecond);  // close the last period
        return n;
    }
    const std::vector<ym::MappedRecord>& records() const { return rig.out->records; }
    const std::vector<ym::MeasurementCycle>& cycles() const { return rig.out->cycles; }
};

// TST-31: cycles in sequence on the 1 s grid, UTC moving on.

}  // namespace mh
