// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TST-10 end to end: measurements without a radio, from the radios'
// scenarios (yantra-radios data/scenarios) to Yantra's cycles, against the
// generator's ground truth.
//   - record files written in the build tree by the submodule's tools
//     (gen_records.cmake): yantra-gen ground truth (dBm, its STEM.cells.csv
//     and STEM.truth.csv) and decode-iq dBFS records, replayed into the
//     pipeline with the TETRA technology plugin on a ManualClock;
//   - the simulator radio plugin (RAD-61) on the same scenarios through
//     RadioHost + pipeline + TETRA plugin, also with Yantra's building
//     model (walls, PLG-27);
//   - late, burst, out-of-order and duplicate TETRA records (TST-23) and
//     the >= 1 Hz cadence without gaps (TST-31), with calibration (TST-30).
// Levels, identities, neighbours, C1 (delivered and estimated) and sample
// flags are checked against the ground truth. Cases skip when the radio
// plugins or tools are not built.

#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include <yantra/core/building/model.h>
#include <yantra/core/context/building_context.h>
#include <yantra/core/measure/record_file.h>

#include "measure_rig.h"

using namespace mh;
namespace yb = yantra::building;

namespace {

constexpr std::int64_t kTenth = 100'000'000;  // the truth interval of the scenarios

fs::path records_dir() { return fs::path(E2E_RECORDS_DIR); }

bool have_records(const char* what) {
    if (std::string(E2E_RECORDS_DIR).empty() || !fs::exists(records_dir() / "fraunces.records.jsonl")) {
        MESSAGE("no record files of the radios' scenarios (radio tools not built): skipped " << what);
        return false;
    }
    return true;
}

// A CSV file with a header line: one map per row (no quoted commas: the
// generator's CSV files have none).
std::vector<std::map<std::string, std::string>> read_csv(const fs::path& path) {
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
std::optional<double> num(const ym::MappedRecord& m, const char* name) {
    const auto i = m.schema->field_index(name);
    if (!i) return std::nullopt;
    const yp::Value& v = m.record.values[*i];
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    if (const auto* s = std::get_if<std::int64_t>(&v)) return static_cast<double>(*s);
    if (const auto* e = std::get_if<yp::EnumValue>(&v)) return static_cast<double>(e->value);
    return std::nullopt;
}
std::string text(const ym::MappedRecord& m, const char* name) {
    const auto i = m.schema->field_index(name);
    if (!i) return {};
    const auto* s = std::get_if<std::string>(&m.record.values[*i]);
    return s ? *s : std::string{};
}

// C1 by hand (EN 300 392-2 clause 23): level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS power).
double c1_by_hand(const ym::MappedRecord& m, double level_dbm, double ms_dbm = 30.0) {
    return level_dbm - *num(m, "rxlev_access_min_dbm") - std::max(0.0, *num(m, "ms_txpwr_max_cell_dbm") - ms_dbm);
}

ym::RecordFile load(const std::string& name) {
    const auto p = ym::read_record_file(records_dir() / name);
    std::string errors;
    for (const auto& e : p.errors) errors += e + "\n";
    INFO(errors);
    REQUIRE(p.ok());
    return *p.value;
}

std::int64_t last_time(const ym::RecordFile& f) {
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
void check_cadence(const std::vector<ym::MeasurementCycle>& cs) {
    for (std::size_t i = 1; i < cs.size(); ++i) {
        CHECK(cs[i].sequence == cs[i - 1].sequence + 1);
        CHECK(cs[i].time.monotonic_ns - cs[i - 1].time.monotonic_ns == kSecond);
        CHECK(cs[i].time.utc_ns > cs[i - 1].time.utc_ns);
    }
}

// The neighbour entries the pipeline gave out, per broadcasting carrier:
// the set of announced carriers.
std::map<std::uint64_t, std::set<std::uint64_t>> neighbour_lists(const std::vector<ym::MappedRecord>& rs) {
    std::map<std::uint64_t, std::set<std::uint64_t>> out;
    for (const ym::MappedRecord& m : rs)
        if (m.schema->id == "tetra_neighbour")
            out[m.frequency_hz].insert(static_cast<std::uint64_t>(*num(m, "neighbour_frequency_hz")));
    return out;
}

// The simulator radio plugin through the rig's host, `scenario` chosen by
// its setting (item value: 1 + index in the sorted scenario list).
struct SimRun {
    Rig rig{true};
    TetraPlugin tetra{rig};
    std::string id;
    std::string key;
    std::shared_ptr<yr::RadioDevice> dev;

    bool open(const std::string& scenario, double record_rate_hz = 4.0) {
        const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
        if (!fs::exists(path)) {
            MESSAGE("no simulator plugin built: skipped");
            return false;
        }
        const yr::RadioPluginEntry e = rig.host->load_file(path);
        INFO(e.message);
        REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
        id = e.id;
        const yr::EnumerateResult en = rig.host->enumerate(id);
        REQUIRE(!en.devices.empty());
        key = en.devices.front().key;
        if (!scenario.empty()) {
            std::vector<std::string> ids;
            for (const auto& f : fs::directory_iterator(RADIOS_SCENARIO_DIR))
                if (f.path().extension() == ".json") ids.push_back(f.path().stem().string());
            std::sort(ids.begin(), ids.end());
            const auto it = std::find(ids.begin(), ids.end(), scenario);
            REQUIRE(it != ids.end());
            rig.state->set_device_setting(id, key, "scenario",
                                          yp::EnumValue{1 + static_cast<std::int64_t>(it - ids.begin())});
        }
        rig.state->set_device_setting(id, key, "record_rate_hz", record_rate_hz);
        rig.state->set_device_setting(id, key, "gain_db", 0.0);
        std::string error;
        dev = rig.host->open(id, key, error);
        REQUIRE_MESSAGE(dev, error);
        return true;
    }
    // The simulator's true calibration offset at gain 0 (RAD-61: scenario
    // reference level -20 dBm): a practice table entry for its key.
    void calibrate(double offset_db = -20.0, ycal::AntennaSettings antenna = {}) {
        auto table = std::make_shared<ycal::Table>();
        table->put(flat_entry(*dev->calibration_key(), offset_db, true));
        rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, antenna));
    }
    std::vector<ym::MappedRecord> records() {
        const std::lock_guard lock(rig.out->m);
        return rig.out->records;
    }
    std::vector<ym::MeasurementCycle> cycles() {
        const std::lock_guard lock(rig.out->m);
        return rig.out->cycles;
    }
    ~SimRun() {
        if (dev) {
            dev->stop();
            dev->close();
        }
    }
};

}  // namespace

TEST_CASE("TST-10 record files: yantra-gen ground truth reaches the samples unchanged: levels, identities, neighbours, C1, flags") {
    if (!have_records("")) return;
    for (const std::string scenario : {"fraunces", "failed_site", "floor_change", "indoor_weak_das"}) {
        CAPTURE(scenario);
        const Truth truth(scenario);
        const ym::RecordFile file = load(scenario + ".records.jsonl");
        CHECK((file.capabilities & YR_CAP_SELF_CALIBRATED) != 0);
        FileRun run;
        REQUIRE(run.run(file) == file.record_count);
        const ym::PipelineStats st = run.rig.pipeline->stats();
        CHECK(st.unmapped == 0);
        CHECK(st.generic == 0);
        CHECK(st.schema_mismatch == 0);
        CHECK(st.implausible == 0);
        CHECK(st.out_of_order == 0);
        CHECK(st.late == 0);
        CHECK(st.c1_estimated == 0);

        std::size_t cells = 0;
        for (const ym::MappedRecord& m : run.records()) {
            CHECK(m.provenance.plugin_id == "yantra.tetra");
            CHECK((m.flags & sf::kSimulated) != 0);
            CHECK((m.flags & (sf::kUncalibrated | sf::kLate | sf::kNoSync | sf::kOverload)) == 0);
            if (m.schema->id != "tetra_cell") continue;
            ++cells;
            const std::string cell = text(m, "cell_id");
            REQUIRE_MESSAGE(truth.cells.count(cell) == 1, cell);
            const Truth::Cell& c = truth.cells.at(cell);
            // Identity as the cell broadcasts it.
            CHECK(m.frequency_hz == c.frequency_hz);
            CHECK(num(m, "mcc") == static_cast<double>(c.mcc));
            CHECK(num(m, "mnc") == static_cast<double>(c.mnc));
            CHECK(num(m, "la") == static_cast<double>(c.la));
            CHECK(num(m, "colour_code") == static_cast<double>(c.colour_code));
            // Level: the true level at that time (CSV with 0.01 dB), on air.
            const std::int64_t tenth = (m.record.time.monotonic_ns + kTenth / 2) / kTenth;
            const auto row = truth.rows.find({tenth, cell});
            REQUIRE(row != truth.rows.end());
            CHECK(row->second.on_air);
            CHECK(std::fabs(m.level - row->second.level_dbm) <= 0.005 + 1e-9);
            CHECK(m.level == m.radio_level);  // dBm of a SELF_CALIBRATED radio, no antenna correction
            CHECK(num(m, "walls") == static_cast<double>(row->second.walls));
            // C1 delivered by the "radio" and the plugin's rule agree (RF-06).
            CHECK(m.c1_source == ym::C1Source::Radio);
            CHECK(*num(m, "c1_db") == doctest::Approx(c1_by_hand(m, m.level)).epsilon(1e-12));
            const auto mine = run.tetra.c1(m, m.level);
            REQUIRE(mine);
            CHECK(*mine == doctest::Approx(*num(m, "c1_db")).epsilon(1e-12));
        }
        CHECK(cells > 0);
        CHECK(st.c1_radio == cells);

        // RF-03: every broadcaster's list names the neighbours of the scenario.
        const auto lists = neighbour_lists(run.records());
        REQUIRE(!lists.empty());
        for (const auto& [hz, announced] : lists) {
            REQUIRE(truth.by_frequency.count(hz) == 1);
            std::set<std::uint64_t> want;
            for (const std::string& n : truth.cells.at(truth.by_frequency.at(hz)).neighbours)
                want.insert(truth.cells.at(n).frequency_hz);
            CHECK(announced == want);
        }

        // TST-31 and RF-08: a cycle per second without gaps; a carrier is
        // fresh in a cycle exactly when its cell was on air in that second,
        // else held (a cell off the air: failed_site N2 from 6 to 8 s).
        const auto& cs = run.cycles();
        REQUIRE(cs.size() >= 3);
        check_cadence(cs);
        for (const ym::MeasurementCycle& cy : cs) {
            REQUIRE(cy.serving() != nullptr);
            for (const ym::CarrierSample& s : cy.samples) {
                if (s.latest.schema->id != "tetra_cell") continue;
                const std::string& cell = truth.by_frequency.at(s.latest.frequency_hz);
                const bool on = truth.on_air_between(cell, cy.time.monotonic_ns - kSecond, cy.time.monotonic_ns);
                CAPTURE(cell);
                CAPTURE(cy.time.monotonic_ns);
                CHECK((s.records_in_cycle > 0) == on);
                CHECK(((s.flags & sf::kHeld) != 0) == !on);
            }
        }
        // A cell never on the air (failed_site N3) never has a cell record.
        for (const auto& [id, c] : truth.cells)
            if (!c.on_air)
                for (const ym::MappedRecord& m : run.records())
                    CHECK_FALSE((m.schema->id == "tetra_cell" && m.frequency_hz == c.frequency_hz));
    }
}

TEST_CASE("TST-10 record files: the serving cell follows the walk across floors, the plugin reselects to it (DB-08, TST-31)") {
    if (!have_records("")) return;
    const Truth truth("floor_change");
    FileRun run;
    run.run(load("floor_change.records.jsonl"));
    const std::uint64_t d0 = truth.cells.at("D0").frequency_hz;
    const std::uint64_t d1 = truth.cells.at("D1").frequency_hz;
    // The generator changes the serving cell at 6.5 s (the stairs).
    bool changed = false;
    for (const ym::RadioEvent& e : run.rig.out->events)
        changed = changed || (e.event == YR_EVENT_SERVING_CHANGED && e.value == static_cast<std::int64_t>(d1));
    CHECK(changed);
    std::size_t positions = 0;
    for (const ym::MeasurementCycle& cy : run.cycles()) {
        const ym::CarrierSample* s = cy.serving();
        REQUIRE(s != nullptr);
        if (cy.time.monotonic_ns <= 6 * kSecond) CHECK(s->latest.frequency_hz == d0);
        if (cy.time.monotonic_ns >= 8 * kSecond) CHECK(s->latest.frequency_hz == d1);
        // TST-31: every serving sample has its position on the floor.
        CHECK_FALSE(text(s->latest, "sim_floor_id").empty());
        CHECK(num(s->latest, "sim_x_m"));
        positions += num(s->latest, "sim_y_m") ? 1 : 0;
    }
    CHECK(positions == run.cycles().size());

    // Reselection (DB-08) with the levels of the cycle after the stairs:
    // serving on D0 with D1 stronger. The generator broadcasts no
    // reselection thresholds, so the plugin cannot decide (C2 above C1
    // needs SRT/SRH/FRT/FRH): a defined UNSUPPORTED. With thresholds put
    // into the serving record (SRT above the indoor C1) it reselects slowly
    // to D1, the cell the generator serves after the stairs.
    const ym::MeasurementCycle* after = nullptr;
    for (const ym::MeasurementCycle& cy : run.cycles())
        if (cy.time.monotonic_ns == 8 * kSecond) after = &cy;
    REQUIRE(after != nullptr);
    const ym::CarrierSample* s0 = nullptr;
    const ym::CarrierSample* s1 = nullptr;
    for (const ym::CarrierSample& s : after->samples) {
        if (s.latest.schema->id != "tetra_cell") continue;
        if (s.latest.frequency_hz == d0) s0 = &s;
        if (s.latest.frequency_hz == d1) s1 = &s;
    }
    REQUIRE((s0 && s1));
    CHECK(s1->latest.level > s0->latest.level);
    const auto* api = run.tetra.loader.descriptor("yantra.tetra")->technology;
    auto reselect = [&](const yp::Record& serving_record, yantra_reselection_result& out) {
        const ym::RecordView v0(serving_record, s0->latest.schema_index);
        const ym::RecordView v1(s1->latest.record, s1->latest.schema_index);
        yantra_reselection_cell serving{sizeof(yantra_reselection_cell), 0, v0.get(), s0->latest.level};
        yantra_reselection_cell neighbour{sizeof(yantra_reselection_cell), 0, v1.get(), s1->latest.level};
        const yantra_reselection_cell* neighbours[] = {&neighbour};
        yantra_reselection_input in{sizeof(yantra_reselection_input), 1, &serving, neighbours, 30.0};
        out = yantra_reselection_result{};
        out.struct_size = sizeof out;
        return api->reselect(run.tetra.loader.instance("yantra.tetra"), &in, &out);
    };
    yantra_reselection_result out{};
    CHECK(reselect(s0->latest.record, out) == YANTRA_ERR_UNSUPPORTED);
    yp::Record with = s0->latest.record;
    const double c1 = c1_by_hand(s0->latest, s0->latest.level);
    for (const auto& [field, db] : {std::pair{"srt_db", c1 + 10.0}, std::pair{"srh_db", 6.0},
                                    std::pair{"frt_db", 0.0}, std::pair{"frh_db", 3.0}}) {
        const auto i = s0->latest.schema->field_index(field);
        REQUIRE(i);
        with.values[*i] = static_cast<std::uint64_t>(std::ceil(db));
    }
    REQUIRE(reselect(with, out) == YANTRA_OK);
    MESSAGE(out.reason);
    CHECK(out.chosen == 0);
}

TEST_CASE("TST-10 record files: replayed dBFS records are calibrated by the replay's key, interpolated, and get an estimated C1 (TST-30, RF-06)") {
    if (!have_records("")) return;
    const Truth truth("fraunces");
    const ym::RecordFile file = load("fraunces_dbfs.records.jsonl");
    CHECK(file.device_driver.empty());  // decode-iq: no device, no key of its own
    CHECK((file.capabilities & YR_CAP_SELF_CALIBRATED) == 0);

    SUBCASE("without a key the levels stay dBFS: uncalibrated, no C1") {
        FileRun run;
        run.run(file);
        CHECK(run.rig.pipeline->stats().c1_estimated == 0);
        CHECK(run.rig.pipeline->stats().c1_radio == 0);
        std::size_t n = 0;
        for (const ym::MappedRecord& m : run.records()) {
            if (!std::isfinite(m.radio_level)) continue;
            ++n;
            CHECK(m.radio_dbfs);
            CHECK((m.flags & sf::kUncalibrated) != 0);
            CHECK(m.c1_source == ym::C1Source::None);
        }
        CHECK(n > 4);
    }
    SUBCASE("with a key: the table interpolated over frequency and the antenna correction, to 0.01 dB") {
        const ycal::Key key{"yantra-gen", "fraunces", {{"reference_level_dbm", "-20"}}};
        ycal::Entry e = flat_entry(key, 0.0);
        // Two points around the carriers: -19 dB at 380 MHz, -21 dB at 400 MHz
        // (CALIBRATION.md: linear in dB between points).
        e.points.clear();
        for (const auto& [hz, db] : {std::pair{380e6, -19.0}, std::pair{400e6, -21.0}}) {
            ycal::Point p;
            p.frequency_hz = hz;
            p.offset_db = db;
            e.points.push_back(p);
        }
        auto table = std::make_shared<ycal::Table>();
        table->put(e);
        ycal::AntennaSettings antenna;
        antenna.all.cable_loss_db = 2.5;
        FileRun run;
        run.rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, antenna));
        ym::ReplayOptions o;
        o.calibration_key = std::make_shared<const ycal::Key>(key);
        run.run(file, o);
        std::size_t estimated = 0;
        for (const ym::MappedRecord& m : run.records()) {
            if (!std::isfinite(m.radio_level)) continue;
            const double table_db = -19.0 + (static_cast<double>(m.frequency_hz) - 380e6) / 20e6 * -2.0;
            CHECK(m.correction.calibrated);
            CHECK((m.flags & sf::kUncalibrated) == 0);
            CHECK(std::fabs(m.level - (m.radio_level + table_db + 2.5)) <= 0.01);
            if (m.schema->id != "tetra_cell" || !truth.by_frequency.count(m.frequency_hz)) continue;
            // Against the ground truth (scenario reference level -20 dBm: the
            // true offset is -20 dB; the table is off by the interpolation).
            const Truth::Cell& c = truth.cells.at(truth.by_frequency.at(m.frequency_hz));
            const double true_dbm = truth.rows.at({0, c.id}).level_dbm;
            CHECK(std::fabs(m.level - 2.5 - (true_dbm + table_db + 20.0)) <= 0.5);
            if ((m.flags & sf::kNoSync) != 0) continue;
            CHECK(num(m, "la") == static_cast<double>(c.la));
            CHECK(num(m, "colour_code") == static_cast<double>(c.colour_code));
            // RF-06: no C1 in the record: estimated from the calibrated level.
            if (m.c1_source == ym::C1Source::Estimated) {
                ++estimated;
                CHECK((m.flags & sf::kC1Estimated) != 0);
                CHECK(*num(m, "c1_db") == doctest::Approx(c1_by_hand(m, m.level)).epsilon(1e-12));
            }
        }
        CHECK(estimated >= 4);
        CHECK(run.rig.pipeline->stats().c1_estimated == estimated);
        CHECK(run.rig.pipeline->stats().c1_radio == 0);
    }
}

TEST_CASE("TST-23 record files: late, out-of-order and duplicate TETRA records have defined results") {
    if (!have_records("")) return;
    const ym::RecordFile file = load("fraunces.records.jsonl");
    const Truth truth("fraunces");
    const std::uint64_t t1 = truth.cells.at("T1").frequency_hz;
    const std::uint64_t m1 = truth.cells.at("M1").frequency_hz;
    auto hz_of = [](const ym::RecordFileLine& l) -> std::uint64_t {
        const auto* r = std::get_if<ym::RadioRecord>(&l);
        if (r == nullptr || r->schema_kind != YR_SCHEMA_MEASUREMENT) return 0;
        for (std::size_t i = 0; i < r->field_names->size(); ++i)
            if ((*r->field_names)[i] == "frequency_hz") return std::get<std::uint64_t>(r->record.values[i]);
        return 0;
    };
    auto time_of = [](const ym::RecordFileLine& l) {
        if (const auto* r = std::get_if<ym::RadioRecord>(&l)) return r->record.time.monotonic_ns;
        return std::get<ym::RadioEvent>(l).monotonic_ns;
    };
    // M1's records arrive in a burst 1.5 s late (in their own order); two
    // T1 records swap places (one out of order); one T1 record twice.
    std::vector<ym::RecordFileLine> held, lines;
    for (const ym::RecordFileLine& l : file.lines) {
        while (!held.empty() && time_of(l) >= time_of(held.front()) + 3 * kSecond / 2) {
            lines.push_back(held.front());
            held.erase(held.begin());
        }
        if (hz_of(l) == m1) held.push_back(l);
        else lines.push_back(l);
    }
    lines.insert(lines.end(), held.begin(), held.end());
    std::vector<std::size_t> t1_lines;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (hz_of(lines[i]) == t1) t1_lines.push_back(i);
    REQUIRE(t1_lines.size() > 10);
    std::swap(lines[t1_lines[3]], lines[t1_lines[4]]);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(t1_lines[8]) + 1, lines[t1_lines[8]]);
    ym::RecordFile shuffled = file;
    shuffled.lines = lines;
    shuffled.record_count += 1;

    // The pipeline clock never goes back: a record arrives at the newest time seen.
    std::uint64_t late = 0;
    std::int64_t clock = 0;
    for (const ym::RecordFileLine& l : lines) {
        clock = std::max(clock, time_of(l));
        if (std::holds_alternative<ym::RadioRecord>(l) && clock - time_of(l) > kSecond) ++late;
    }
    FileRun run;
    REQUIRE(run.run(shuffled) == shuffled.record_count);
    const ym::PipelineStats st = run.rig.pipeline->stats();
    CHECK(st.out_of_order == 1);
    CHECK(st.duplicates == 1);
    CHECK(late > 0);
    CHECK(st.late == late);
    CHECK(st.accepted == shuffled.record_count - 2);
    // Late M1 values are used and flagged; T1 keeps its newest level.
    bool m1_late = false;
    for (const ym::MeasurementCycle& cy : run.cycles())
        for (const ym::CarrierSample& s : cy.samples) {
            if (s.latest.frequency_hz == m1 && s.latest.schema->id == "tetra_cell")
                m1_late = m1_late || (s.flags & sf::kLate) != 0;
            if (s.latest.frequency_hz == t1 && s.latest.schema->id == "tetra_cell")
                CHECK((s.flags & sf::kLate) == 0);
        }
    CHECK(m1_late);
    check_cadence(run.cycles());
}

TEST_CASE("TST-10 simulator: a scenario through RadioHost, pipeline and TETRA plugin matches the ground truth (RF-01, RF-03, RF-06, RF-08, TST-31)") {
    if (!have_records("(ground truth)")) return;
    const Truth truth("failed_site");
    SimRun sim;
    if (!sim.open("failed_site", 4.0)) return;
    sim.calibrate();
    // The ground truth's serving cell: the strongest on the air at the
    // start (N1); every other cell on the air is measured too (N2, RF-03).
    std::string strongest;
    for (const auto& [id, c] : truth.cells) {
        const auto row = truth.rows.find({0, id});
        if (!c.on_air || row == truth.rows.end() || !row->second.on_air) continue;
        if (strongest.empty() || row->second.level_dbm > truth.rows.at({0, strongest}).level_dbm) strongest = id;
    }
    REQUIRE(strongest == "N1");
    const std::uint64_t serving_hz = truth.cells.at(strongest).frequency_hz;
    std::set<std::uint64_t> on_air;
    for (const auto& [id, c] : truth.cells)
        if (c.on_air) on_air.insert(c.frequency_hz);
    REQUIRE(on_air.size() == 2);
    // Synchronised tetra_cell records per carrier.
    auto synced = [&](std::uint64_t hz) {
        std::size_t n = 0;
        for (const ym::MappedRecord& m : sim.rig.out->records)
            if (m.schema->id == "tetra_cell" && m.frequency_hz == hz && (m.flags & sf::kNoSync) == 0) ++n;
        return n;
    };
    std::size_t first_cycles = 0;
    {
        ym::CycleRunner runner(*sim.rig.pipeline);
        REQUIRE(sim.dev->start(serving(0)) == YR_OK);
        auto serving_cycles = [&] {
            std::size_t n = 0;
            for (const auto& c : sim.rig.out->cycles) {
                const ym::CarrierSample* s = c.serving(sim.key);
                if (s != nullptr && (s->flags & sf::kHeld) == 0 && s->latest.frequency_hz == serving_hz) ++n;
            }
            return n;
        };
        // The serving cell's whole neighbour list (RF-03).
        auto list_seen = [&] {
            const auto lists = neighbour_lists(sim.rig.out->records);
            return lists.count(serving_hz) == 1 &&
                   lists.at(serving_hz).size() >= truth.cells.at(strongest).neighbours.size();
        };
        REQUIRE(sim.rig.out->wait([&] {
            if (serving_cycles() < 4 || !list_seen()) return false;
            for (std::uint64_t hz : on_air)
                if (synced(hz) < 4) return false;
            return true;
        }));
        CHECK(sim.dev->stop() == YR_OK);
        first_cycles = sim.cycles().size();
    }
    const Truth::Cell& serving_cell = truth.cells.at(strongest);
    const ym::PipelineStats st = sim.rig.pipeline->stats();
    CHECK(st.generic == 0);
    CHECK(st.schema_mismatch == 0);
    CHECK(st.skipped_cycles == 0);
    std::map<std::uint64_t, std::size_t> compared;
    // A record per carrier and record interval (one decoder per carrier).
    std::set<std::pair<std::int64_t, std::uint64_t>> intervals;
    bool serving_seen = false;
    // Neighbour roles come with the serving cell's D-NWRK-BROADCAST.
    std::int64_t list_from = std::numeric_limits<std::int64_t>::max();
    for (const ym::MappedRecord& m : sim.records())
        if (m.schema->id == "tetra_neighbour" && m.frequency_hz == serving_hz)
            list_from = std::min(list_from, m.record.time.monotonic_ns);
    for (const ym::MappedRecord& m : sim.records()) {
        CHECK((m.flags & sf::kSimulated) != 0);
        if (m.schema->id != "tetra_cell" || !std::isfinite(m.radio_level)) continue;
        CHECK(m.correction.calibrated);
        CHECK((m.flags & sf::kUncalibrated) == 0);
        CHECK(m.level == doctest::Approx(m.radio_level - 20.0).epsilon(1e-12));
        if ((m.flags & sf::kNoSync) != 0) continue;
        CHECK(intervals.insert({m.record.time.monotonic_ns, m.frequency_hz}).second);
        const std::string cell = text(m, "cell_id");
        REQUIRE_MESSAGE(truth.cells.count(cell) == 1, cell);
        const Truth::Cell& c = truth.cells.at(cell);
        CHECK(c.on_air);  // N3 is off the air: never decoded
        CHECK(m.frequency_hz == c.frequency_hz);
        CHECK(num(m, "mcc") == static_cast<double>(c.mcc));
        CHECK(num(m, "mnc") == static_cast<double>(c.mnc));
        CHECK(num(m, "la") == static_cast<double>(c.la));
        CHECK(num(m, "colour_code") == static_cast<double>(c.colour_code));
        // The calibrated level against the simulator's true level (its
        // estimate within 1 dB at these SNRs, RAD-112/RAD-125).
        CHECK(std::fabs(m.level - *num(m, "level_inst_dbm")) <= 1.0);
        ++compared[m.frequency_hz];
        // Roles: once the strongest cell serves and its list came, the
        // other is its neighbour.
        if (m.frequency_hz == serving_hz && num(m, "role") == static_cast<double>(YR_ROLE_SERVING)) serving_seen = true;
        if (serving_seen && m.record.time.monotonic_ns >= list_from) {
            CAPTURE(cell);
            CHECK(num(m, "role") == static_cast<double>(m.frequency_hz == serving_hz ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR));
        }
        // C1 delivered by the radio, the same as the plugin's for the level.
        CHECK(m.c1_source == ym::C1Source::Radio);
        const auto mine = sim.tetra.c1(m, m.level);
        REQUIRE(mine);
        CHECK(*mine == doctest::Approx(*num(m, "c1_db")).epsilon(1e-9));
    }
    CHECK(serving_seen);
    for (std::uint64_t hz : on_air) {
        CAPTURE(hz);
        CHECK(compared[hz] >= 4);
    }
    // RF-03: the serving cell's list as broadcast.
    const auto lists = neighbour_lists(sim.records());
    REQUIRE(lists.count(serving_hz) == 1);
    std::set<std::uint64_t> want;
    for (const std::string& n : serving_cell.neighbours) want.insert(truth.cells.at(n).frequency_hz);
    CHECK(lists.at(serving_hz) == want);
    // TST-31: every cycle from the first with the strongest cell serving
    // has it (until the stop), on the grid; before it the receiver may
    // follow the first carrier it locked to.
    const auto cs = sim.cycles();
    REQUIRE(cs.size() == first_cycles);
    std::size_t i = 0;
    while (i < cs.size() &&
           (cs[i].serving(sim.key) == nullptr || cs[i].serving(sim.key)->latest.frequency_hz != serving_hz))
        ++i;
    REQUIRE(i < cs.size());
    for (; i < cs.size(); ++i) {
        const ym::CarrierSample* s = cs[i].serving(sim.key);
        if (s == nullptr) break;  // after the stop
        CHECK(s->latest.frequency_hz == serving_hz);
    }
    for (; i < cs.size(); ++i) CHECK(cs[i].serving(sim.key) == nullptr);
    check_cadence(cs);
}

TEST_CASE("TST-10 simulator: a serving carrier asked for is followed, the strongest measured as its neighbour (RF-01, RF-03)") {
    if (!have_records("(ground truth)")) return;
    const Truth truth("failed_site");
    SimRun sim;
    if (!sim.open("failed_site", 4.0)) return;
    sim.calibrate();
    const std::uint64_t n1 = truth.cells.at("N1").frequency_hz, n2 = truth.cells.at("N2").frequency_hz;
    // N2, the weaker cell, at the capture centre; N1 at its edge.
    auto records_of = [&](std::uint64_t hz, double role) {
        std::size_t n = 0;
        for (const ym::MappedRecord& m : sim.rig.out->records)
            if (m.schema->id == "tetra_cell" && m.frequency_hz == hz && (m.flags & sf::kNoSync) == 0 &&
                num(m, "role") == role)
                ++n;
        return n;
    };
    {
        ym::CycleRunner runner(*sim.rig.pipeline);
        REQUIRE(sim.dev->start(serving(n2)) == YR_OK);
        REQUIRE(sim.rig.out->wait([&] {
            return records_of(n2, YR_ROLE_SERVING) >= 4 && records_of(n1, YR_ROLE_NEIGHBOUR) >= 4;
        }));
        CHECK(sim.dev->stop() == YR_OK);
    }
    for (const ym::MappedRecord& m : sim.records()) {
        if (m.schema->id != "tetra_cell" || (m.flags & sf::kNoSync) != 0) continue;
        CAPTURE(m.frequency_hz);
        CHECK((num(m, "role") != static_cast<double>(YR_ROLE_SERVING) || m.frequency_hz == n2));
        CHECK(std::fabs(m.level - *num(m, "level_inst_dbm")) <= 1.0);
    }
    for (const auto& c : sim.cycles())
        if (const ym::CarrierSample* s = c.serving(sim.key)) CHECK(s->latest.frequency_hz == n2);
}

namespace {

// A 20 m x 10 m floor with the Fraunces scenario's T1 antenna at (3, 5) m
// (as tests/scene_sim), `wall`: a concrete wall along x = 8 m.
yb::Building office(bool wall) {
    yb::Building b;
    b.name = "Simulated office";
    b.cells.push_back(yb::Cell{"T1", "Indoor DAS", "tetra", "tetra_cell", 390'000'000,
                               {{"mcc", 901}, {"mnc", 1}, {"la", 100}, {"colour_code", 1}}});
    yb::Floor f;
    f.id = "sim-ground";
    f.name = "Ground floor";
    f.example_id = "fraunces-1";
    f.plan_width = 400;
    f.plan_height = 200;
    f.scale.method = yb::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    yb::Antenna a;
    a.id = "a1";
    a.position = {60, 100};
    a.height_m = 2.8;
    a.eirp_dbm = 10.0;
    a.cell = "T1";
    f.antennas.push_back(a);
    if (wall) f.walls.push_back(yb::Wall{"w1", "concrete", {{160, 0}, {160, 200}}, std::nullopt});
    b.floors.push_back(f);
    return b;
}

}  // namespace

TEST_CASE("TST-10 simulator: Yantra's building model sets the indoor level; a wall lowers the calibrated TETRA level by its attenuation (PLG-27, IND-11)") {
    SimRun sim;
    yc::BuildingContext context(sim.rig.publisher, yr::SceneExportOptions{});
    context.set_building(office(false));
    context.show_floor("sim-ground");
    context.set_cursor(yb::Point2{240.0, 100.0});  // (12, 5) m, 9 m from the antenna
    if (!sim.open("", 10.0)) return;
    sim.calibrate();
    REQUIRE(sim.dev->start(serving(390'000'000)) == YR_OK);
    // T1 records at the cursor from `from` on, after `skip` that let the
    // receiver settle.
    auto at_cursor = [&](std::size_t from, std::size_t skip, std::size_t n, int walls) {
        std::vector<ym::MappedRecord> got;
        REQUIRE(sim.rig.out->wait([&] {
            got.clear();
            std::size_t seen = 0;
            for (std::size_t i = from; i < sim.rig.out->records.size(); ++i) {
                const ym::MappedRecord& m = sim.rig.out->records[i];
                if (m.schema->id != "tetra_cell" || m.frequency_hz != 390'000'000 ||
                    num(m, "sim_x_m") != 12.0 || num(m, "walls") != static_cast<double>(walls))
                    continue;
                if (++seen > skip) got.push_back(m);
            }
            return got.size() >= n;
        }));
        double sum = 0.0;
        for (const auto& m : got) {
            CHECK(m.correction.calibrated);
            CHECK(text(m, "sim_floor_id") == "sim-ground");
            sum += m.level;
        }
        return sum / static_cast<double>(got.size());
    };
    const double open = at_cursor(0, 10, 10, 0);
    // Free space from the antenna (EIRP 10 dBm, 2.8 m high) to the receiver
    // 1.5 m above the floor (yantra-radios docs/CHANNEL_MODEL.md), in dBm
    // through the calibration (true offset -20 dB).
    const double d = std::hypot(9.0, 2.8 - 1.5);
    const double fspl = 20.0 * std::log10(4.0 * std::numbers::pi * d * 390e6 / 299'792'458.0);
    CHECK(std::fabs(open - (10.0 - fspl)) <= 0.5);
    const double wall_db = *office(true).wall_attenuation_db(office(true).floors[0].walls[0], 390.0);
    std::size_t mark = 0;
    {
        const std::lock_guard lock(sim.rig.out->m);
        mark = sim.rig.out->records.size();
    }
    CHECK(context.set_building(office(true)) == yc::kSceneChanged);
    const double behind = at_cursor(mark, 10, 10, 1);
    MESSAGE("T1 at (12, 5) m: " << open << " dBm without the wall, " << behind << " dBm behind it");
    CHECK(std::fabs((open - behind) - wall_db) <= 1.0);
}

TEST_CASE("TST-23 simulator: a burst the pipeline cannot keep up with is dropped and counted; the cycles go on") {
    SimRun sim;
    sim.rig = Rig(true, 8);  // a sink queue of 8 records
    if (!sim.open("failed_site", 10.0)) return;
    {
        const std::lock_guard lock(sim.rig.out->m);
        sim.rig.out->gate = true;
    }
    ym::CycleRunner runner(*sim.rig.pipeline);
    REQUIRE(sim.dev->start(serving(0)) == YR_OK);
    // The delivery thread hangs in the observer; the simulator pushes on.
    REQUIRE(sim.rig.out->wait([&] { return sim.rig.out->waiting == 1; }));
    REQUIRE(poll_until([&] { return sim.dev->stats().sink.dropped >= 20; }));
    // Cycles keep coming while the records are held up (their carriers are held).
    std::size_t cycles = 0;
    {
        const std::lock_guard lock(sim.rig.out->m);
        cycles = sim.rig.out->cycles.size();
    }
    REQUIRE(sim.rig.out->wait([&] { return sim.rig.out->cycles.size() >= cycles + 2; }));
    sim.rig.out->open_gate();
    // Fresh serving samples again after the burst.
    std::size_t from = 0;
    {
        const std::lock_guard lock(sim.rig.out->m);
        from = sim.rig.out->cycles.size();
    }
    REQUIRE(sim.rig.out->wait([&] {
        for (std::size_t i = from; i < sim.rig.out->cycles.size(); ++i) {
            const ym::CarrierSample* s = sim.rig.out->cycles[i].serving(sim.key);
            if (s != nullptr && (s->flags & sf::kHeld) == 0) return true;
        }
        return false;
    }));
    CHECK(sim.dev->stop() == YR_OK);
    const yr::DeviceStats ds = sim.dev->stats();
    CHECK(ds.sink.dropped >= 20);
    CHECK(ds.sink.delivered == ds.sink.accepted);
    CHECK(sim.rig.pipeline->stats().records == ds.sink.delivered);
    check_cadence(sim.cycles());
}

TEST_CASE("TST-31 long replayed walk test: a sample with its position every second, no gaps" * doctest::test_suite("long")) {
    if (!have_records("")) return;
    const fs::path gen = fs::path(RADIOS_BIN_DIR) / "yantra-gen";
    REQUIRE(fs::exists(gen));
    const fs::path out = records_dir() / "long_walk.records.jsonl";
    constexpr int kSeconds = 600;
    // yantra-gen writes the side files next to the records (removed below).
    const std::string cmd = gen.string() + " --scenario indoor_good_das --duration " + std::to_string(kSeconds) +
                            " --out - --records " + out.string() + " --quiet > /dev/null";
    REQUIRE(std::system(cmd.c_str()) == 0);
    const ym::RecordFile file = load(out.filename().string());
    FileRun run;
    REQUIRE(run.run(file) == file.record_count);
    const auto& cs = run.cycles();
    CHECK(cs.size() >= static_cast<std::size_t>(kSeconds));
    check_cadence(cs);
    CHECK(run.rig.pipeline->stats().skipped_cycles == 0);
    double x_min = 1e9, x_max = -1e9;
    for (std::size_t i = 0; i + 1 < cs.size(); ++i) {  // the last one closes after the end
        const ym::CarrierSample* s = cs[i].serving();
        REQUIRE(s != nullptr);
        CHECK((s->flags & sf::kHeld) == 0);
        CHECK(s->records_in_cycle >= 1);
        CHECK_FALSE(text(s->latest, "sim_floor_id").empty());
        const auto x = num(s->latest, "sim_x_m");
        REQUIRE(x);
        x_min = std::min(x_min, *x);
        x_max = std::max(x_max, *x);
    }
    CHECK(x_max - x_min > 1.0);  // the walk moves
    for (const auto& f : fs::directory_iterator(records_dir()))
        if (f.path().filename().string().starts_with("long_walk.")) fs::remove(f.path());
}
