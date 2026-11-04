// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Measurement pipeline (OA-13, RF-01, RF-03..05, RF-07, RF-08, TST-23,
// TST-31, PLG-05, PLG-12) with an injected ManualClock: records go in as
// the radio host hands them on (RadioRecord with its radio schema), cycles
// come out when the clock passes a period end. No wall-clock waits.

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <yantra/core/calibration/corrector.h>
#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/radio.h>

namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace sf = yantra::measure::sample_flag;
using yantra::platform::ManualClock;

namespace {

constexpr std::int64_t kSecond = 1'000'000'000;

const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                               {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                               {YR_ROLE_OTHER, "other", "Other"}};

#define FIELD(var, name, type, group, flags, unit, items, count)                             \
    const yr_field_def var = {sizeof(yr_field_def), type, group, flags, name, "Label " name, \
                              unit, "Help " name, items, count, 0}

FIELD(f_freq, YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, "Hz", nullptr, 0);
FIELD(f_chan, YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_role, YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, 0, "", kRoles, 3);
FIELD(f_level, YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, "dBFS", nullptr, 0);
FIELD(f_level_dbm, YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, "dBm", nullptr, 0);
FIELD(f_mcc, YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_mnc, YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_la, YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_cc, YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_mer, YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "%", nullptr, 0);
FIELD(f_evm, YR_FIELD_EVM_MER_DB, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "dB", nullptr, 0);
FIELD(f_ferr, YR_FIELD_FREQ_ERROR_HZ, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "Hz", nullptr, 0);
FIELD(f_entry, YR_FIELD_ENTRY_INDEX, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_count, YR_FIELD_ENTRY_COUNT, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_ncell, "cell_id", YR_TYPE_UINT, YR_GROUP_NEIGHBOURS, 0, "", nullptr, 0);

const yr_field_def* const kCellFields[] = {&f_freq, &f_chan, &f_role, &f_level, &f_mcc, &f_mnc,
                                           &f_la,   &f_cc,   &f_mer,  &f_evm,   &f_ferr};
const yr_schema kCell = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 11,
                         kCellFields, YR_GROUP_QUALITY, 0};
const yr_field_def* const kDbmFields[] = {&f_freq, &f_chan, &f_role, &f_level_dbm,
                                          &f_mcc,  &f_mnc,  &f_la,   &f_cc};
const yr_schema kCellDbm = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 8,
                            kDbmFields, 0, 0};
const yr_field_def* const kScanFields[] = {&f_freq, &f_chan, &f_role, &f_level,
                                           &f_mcc,  &f_mnc,  &f_la,   &f_cc};
const yr_schema kScan = {sizeof(yr_schema), YR_SCHEMA_SCAN, "x_scan", "Scan", 1, 8, kScanFields, 0, 0};
FIELD(f_rxmin, "rx_min_dbm", YR_TYPE_DOUBLE, YR_GROUP_BROADCAST, 0, "dBm", nullptr, 0);
FIELD(f_txmax, "tx_max_dbm", YR_TYPE_INT, YR_GROUP_BROADCAST, 0, "dBm", nullptr, 0);
FIELD(f_truth, "truth_dbm", YR_TYPE_DOUBLE, YR_GROUP_OTHER, 0, "dBm", nullptr, 0);
const yr_field_def* const kThrFields[] = {&f_freq, &f_chan, &f_role, &f_level, &f_mcc, &f_mnc,
                                          &f_la,   &f_cc,   &f_rxmin, &f_txmax, &f_truth};
const yr_schema kThr = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_thr", "Cell", 1, 11, kThrFields,
                        YR_GROUP_BROADCAST | YR_GROUP_OTHER, 0};
const yr_field_def* const kNbFields[] = {&f_freq, &f_entry, &f_count, &f_ncell};
const yr_schema kNeighbour = {sizeof(yr_schema), YR_SCHEMA_NEIGHBOUR, "x_neighbour", "Neighbour", 1, 4,
                              kNbFields, YR_GROUP_NEIGHBOURS, 0};

ym::RadioRecord base(const yr_schema& s, std::uint32_t index, std::int64_t t, std::uint32_t flags) {
    ym::RadioRecord r;
    r.plugin_id = "test.radio";
    r.plugin_version = "1.2.3";
    r.technology_id = "xtech";
    r.device_key = "dev:0";
    r.stream_id = 1;
    r.schema_index = index;
    r.schema_kind = s.kind;
    r.radio_schema = &s;
    r.record.schema_id = s.id;
    r.record.schema_version = s.version;
    r.record.time.monotonic_ns = t;
    r.record.flags = flags;
    r.record.values.resize(s.field_count);
    return r;
}

// A MEASUREMENT record of kCell: `hz`, role, `level` dBFS, quality values.
ym::RadioRecord cell(std::int64_t t, std::uint64_t hz, double level,
                     std::uint32_t role = YR_ROLE_SERVING, std::uint32_t flags = 0) {
    ym::RadioRecord r = base(kCell, 0, t, flags);
    auto& v = r.record.values;
    v[0] = hz;
    v[2] = yp::EnumValue{role};
    v[3] = level;
    if ((flags & YR_RECORD_NO_SYNC) == 0) {
        v[1] = std::uint64_t{3600};
        v[4] = std::uint64_t{901};
        v[5] = std::uint64_t{9};
        v[6] = std::uint64_t{17};
        v[7] = std::uint64_t{1};
        v[8] = 2.5;
        v[9] = 31.0;
        v[10] = -12.0;
        r.present = YR_GROUP_QUALITY;
    }
    return r;
}

struct Collector final : ym::MeasurementObserver {
    std::mutex m;
    std::vector<ym::MappedRecord> records;
    std::vector<ym::MeasurementCycle> cycles;
    std::vector<ym::RadioEvent> events;
    std::atomic<int> on_measurement_thread{0};
    void on_record(const ym::MappedRecord& r) override {
        if (yp::on_measurement_thread()) ++on_measurement_thread;
        const std::lock_guard lock(m);
        records.push_back(r);
    }
    void on_cycle(const ym::MeasurementCycle& c) override {
        const std::lock_guard lock(m);
        cycles.push_back(c);
    }
    void on_event(const ym::RadioEvent& e) override {
        const std::lock_guard lock(m);
        events.push_back(e);
    }
    ym::MeasurementCycle last() {
        const std::lock_guard lock(m);
        REQUIRE(!cycles.empty());
        return cycles.back();
    }
};

struct Fixture {
    std::shared_ptr<ManualClock> clock = std::make_shared<ManualClock>(100 * kSecond);
    std::shared_ptr<Collector> out = std::make_shared<Collector>();
    std::unique_ptr<ym::MeasurementPipeline> p;
    std::vector<std::string> log;

    explicit Fixture(ym::PipelineConfig config = {},
                     std::shared_ptr<ym::TechnologyCatalog> catalog = nullptr,
                     std::shared_ptr<const ym::LevelCorrector> corrector = nullptr) {
        ym::MeasurementPipeline::Options o;
        o.config = config;
        o.catalog = std::move(catalog);
        o.corrector = std::move(corrector);
        o.clock = clock;
        o.log = [this](const std::string& m) { log.push_back(m); };
        p = std::make_unique<ym::MeasurementPipeline>(std::move(o));
        p->add_observer(out);
    }
    std::int64_t now() const { return clock->monotonic_ns(); }
    // Advances the clock to the end of the next cycle and ticks.
    ym::MeasurementCycle next() {
        clock->set(p->next_cycle_ns());
        REQUIRE(p->tick() == 1);
        return out->last();
    }
};

ym::RadioEvent event(ym::RadioEvent::Kind kind, std::uint32_t e = 0, std::int64_t value = 0) {
    ym::RadioEvent ev;
    ev.kind = kind;
    ev.plugin_id = "test.radio";
    ev.device_key = "dev:0";
    ev.event = e;
    ev.value = value;
    return ev;
}

}  // namespace

TEST_CASE("pipeline: the configuration needs at least one cycle per second (TST-31)") {
    ym::PipelineConfig c;
    CHECK(c.validate().empty());
    c.period_ns = 2 * kSecond;
    CHECK(c.validate().find("at least one cycle per second") != std::string::npos);
    ym::MeasurementPipeline::Options o;
    o.config = c;
    CHECK_THROWS_AS(ym::MeasurementPipeline{o}, std::invalid_argument);
    c = {};
    c.drop_after_ns = c.stale_after_ns - 1;
    CHECK_FALSE(c.validate().empty());
}

TEST_CASE("pipeline: a serving record becomes a timestamped sample with its flags (RF-01, RF-08)") {
    Fixture f;
    const std::int64_t t0 = f.now();
    // Nothing is emitted before the first period ends.
    CHECK(f.p->tick() == 0);
    f.clock->advance(300'000'000);
    f.p->on_radio_record(cell(f.now(), 390'012'500, -55.5));
    REQUIRE(f.out->records.size() == 1);
    const ym::MappedRecord& r = f.out->records[0];
    CHECK(r.generic);  // no technology plugin in the catalog
    CHECK(r.provenance.technology_id == "xtech");
    CHECK(r.provenance.plugin_id.empty());
    CHECK(r.provenance.schema_id == "x_cell");
    CHECK(r.radio_plugin_id == "test.radio");
    CHECK(r.radio_plugin_version == "1.2.3");
    CHECK(r.device_key == "dev:0");
    CHECK(r.role == ym::CarrierRole::Serving);
    CHECK(r.frequency_hz == 390'012'500u);
    CHECK(r.level == doctest::Approx(-55.5));
    CHECK(r.radio_dbfs);
    CHECK(r.flags == sf::kUncalibrated);  // dBFS without a calibration (HW-01b)
    CHECK(f.out->on_measurement_thread == 1);  // PLG-07
    // RF-05 / RF-07: the QUALITY fields of the record.
    REQUIRE(r.quality.size() == 3);
    CHECK(r.quality[0].field == "mer_pct");
    CHECK(r.quality[0].unit == "%");
    CHECK(r.quality[1].field == "evm_mer_db");
    CHECK(r.quality[2].value == doctest::Approx(-12.0));
    // The generic schema: level in dBm, quality fields QUALITY (PLG-05).
    REQUIRE(r.schema);
    CHECK(r.schema->fields[3].unit == "dBm");
    CHECK(r.schema->fields[3].category == yp::Category::Level);
    CHECK(r.schema->fields[8].category == yp::Category::Quality);

    const ym::MeasurementCycle c = f.next();
    CHECK(c.sequence == 0);
    CHECK(c.time.monotonic_ns == t0 + kSecond);
    CHECK(c.time.utc_ns == f.clock->utc_ns());
    CHECK(c.period_ns == kSecond);
    REQUIRE(c.samples.size() == 1);
    const ym::CarrierSample* s = c.serving("dev:0");
    REQUIRE(s != nullptr);
    CHECK(s->age_ns == 700'000'000);
    CHECK(s->records_in_cycle == 1);
    CHECK(s->flags == sf::kUncalibrated);
    CHECK(c.flags == sf::kUncalibrated);
    CHECK(ym::sample_flags_text(s->flags | sf::kHeld | sf::kDataLoss) ==
          "uncalibrated,held,data_loss");
}

TEST_CASE("pipeline: cycles follow the period grid without gaps (TST-31)") {
    ym::PipelineConfig cfg;
    cfg.period_ns = 250'000'000;  // 4 Hz is allowed (>= 1 Hz)
    Fixture f(cfg);
    const std::int64_t t0 = f.now();
    f.clock->advance(3 * 250'000'000 + 100'000'000);  // a late tick catches up
    CHECK(f.p->tick() == 3);
    f.clock->advance(150'000'000);
    CHECK(f.p->tick() == 1);
    CHECK(f.p->tick() == 0);
    REQUIRE(f.out->cycles.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(f.out->cycles[i].sequence == i);
        CHECK(f.out->cycles[i].time.monotonic_ns ==
              t0 + static_cast<std::int64_t>(i + 1) * 250'000'000);
        CHECK(f.out->cycles[i].samples.empty());
    }
    CHECK(f.p->stats().cycles == 4);
}

TEST_CASE("pipeline: a tick far behind emits at most max_catch_up cycles and counts the rest") {
    ym::PipelineConfig cfg;
    cfg.max_catch_up = 10;
    Fixture f(cfg);
    f.clock->advance(1000 * kSecond);
    CHECK(f.p->tick() == 10);
    CHECK(f.p->stats().skipped_cycles == 990);
    const ym::MeasurementCycle c = f.next();
    CHECK(c.sequence == 1000);  // the sequence shows the gap
}

TEST_CASE("pipeline: without new records a carrier is held, then stale, then leaves (RF-08)") {
    Fixture f;
    f.p->on_radio_record(cell(f.now(), 390'012'500, -60.0));
    CHECK(f.next().samples.at(0).flags == sf::kUncalibrated);
    // Second cycle: no new record: held.
    ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 1);
    CHECK(c.samples[0].flags == (sf::kUncalibrated | sf::kHeld));
    CHECK(c.samples[0].records_in_cycle == 0);
    CHECK(c.samples[0].latest.level == doctest::Approx(-60.0));
    CHECK(f.next().samples.at(0).flags == (sf::kUncalibrated | sf::kHeld));  // age 3 s: not yet stale
    c = f.next();  // age 4 s > 3 s
    CHECK(c.samples.at(0).flags == (sf::kUncalibrated | sf::kHeld | sf::kStale));
    for (int i = 0; i < 6; ++i) c = f.next();  // age 10 s: still there
    CHECK(c.samples.size() == 1);
    c = f.next();  // age 11 s > drop_after
    CHECK(c.samples.empty());
    const ym::PipelineStats st = f.p->stats();
    CHECK(st.expired == 1);
    CHECK(st.held == 9);
    CHECK(st.stale == 7);
    // A new record brings it back fresh.
    f.p->on_radio_record(cell(f.now(), 390'012'500, -61.0));
    CHECK(f.next().samples.at(0).flags == sf::kUncalibrated);
}

TEST_CASE("pipeline: out-of-order and duplicate records are ignored and counted (TST-23)") {
    Fixture f;
    const std::int64_t t = f.now();
    f.p->on_radio_record(cell(t + 500, 390'012'500, -50.0));
    f.p->on_radio_record(cell(t + 100, 390'012'500, -70.0));  // older than the newest
    f.p->on_radio_record(cell(t + 500, 390'012'500, -50.0));  // the same record again
    f.p->on_radio_record(cell(t + 500, 390'012'500, -51.0));  // same time, other value: kept
    const ym::PipelineStats st = f.p->stats();
    CHECK(st.records == 4);
    CHECK(st.accepted == 2);
    CHECK(st.out_of_order == 1);
    CHECK(st.duplicates == 1);
    CHECK(f.out->records.size() == 2);
    const ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 1);
    CHECK(c.samples[0].latest.level == doctest::Approx(-51.0));
    CHECK(c.samples[0].records_in_cycle == 2);
    // Another carrier's older record is not out of order (per carrier).
    f.p->on_radio_record(cell(t, 390'037'500, -80.0, YR_ROLE_NEIGHBOUR));
    CHECK(f.p->stats().out_of_order == 1);
}

TEST_CASE("pipeline: a late record is used and flagged late (TST-23)") {
    Fixture f;
    f.clock->advance(5 * kSecond);
    CHECK(f.p->tick() == 5);
    // Measured 2.5 s before it arrived (> late_after 1 s): its own cycle was emitted already.
    f.p->on_radio_record(cell(f.now() - 2'500'000'000, 390'012'500, -66.0));
    CHECK(f.p->stats().late == 1);
    const ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 1);
    CHECK((c.samples[0].flags & sf::kLate) != 0);
    CHECK(c.samples[0].age_ns == 3'500'000'000);
    // The flag belongs to the cycle it arrived in only.
    CHECK((f.next().samples.at(0).flags & sf::kLate) == 0);
}

TEST_CASE("pipeline: a burst of records keeps the newest per carrier and counts them (TST-23)") {
    Fixture f;
    const std::int64_t t = f.now();
    for (int i = 0; i < 4000; ++i)
        f.p->on_radio_record(cell(t + 1 + i, 390'012'500 + static_cast<std::uint64_t>(i % 4) * 25'000,
                                  -40.0 - i % 50, i % 4 == 0 ? YR_ROLE_SERVING : YR_ROLE_NEIGHBOUR));
    CHECK(f.p->stats().accepted == 4000);
    const ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 4);
    for (const ym::CarrierSample& s : c.samples) {
        CHECK(s.records_in_cycle == 1000);
        CHECK(s.latest.record.time.monotonic_ns >= t + 3997);
    }
    CHECK(c.with_role(ym::CarrierRole::Neighbour).size() == 3);  // RF-03
    // Sorted by frequency within the device and schema.
    CHECK(c.samples[0].latest.frequency_hz < c.samples[3].latest.frequency_hz);
}

TEST_CASE("pipeline: NaN, Inf and out-of-range levels are refused and counted (TST-23)") {
    Fixture f;
    f.p->on_radio_record(cell(f.now(), 390'012'500, std::nan("")));
    f.p->on_radio_record(cell(f.now(), 390'012'500, std::numeric_limits<double>::infinity()));
    f.p->on_radio_record(cell(f.now(), 390'012'500, 500.0));
    f.p->on_radio_record(cell(f.now(), 390'012'500, -1000.0));
    ym::RadioRecord no_level = cell(f.now(), 390'012'500, -50.0);
    no_level.record.values[3] = std::monostate{};
    f.p->on_radio_record(no_level);
    ym::RadioRecord no_schema = cell(f.now(), 390'012'500, -50.0);
    no_schema.radio_schema = nullptr;
    f.p->on_radio_record(no_schema);
    ym::RadioRecord no_freq = cell(f.now(), 390'012'500, -50.0);
    no_freq.record.values[0] = std::monostate{};
    f.p->on_radio_record(no_freq);
    const ym::PipelineStats st = f.p->stats();
    CHECK(st.records == 7);
    CHECK(st.implausible == 5);
    CHECK(st.unmapped == 2);
    CHECK(st.accepted == 0);
    CHECK(f.next().samples.empty());
}

TEST_CASE("pipeline: the level-correction stage is applied to every level (RF-02, HW-09 hook)") {
    struct Corrector final : ym::LevelCorrector {
        mutable std::mutex m;
        mutable std::vector<std::uint64_t> asked;
        ym::LevelCorrection correction(const ym::LevelContext& c) const override {
            const std::lock_guard lock(m);
            asked.push_back(c.frequency_hz);
            ym::LevelCorrection out;
            out.calibrated = !c.radio_dbm;
            out.offset_db = c.radio_dbm ? 0.0 : -60.0;
            out.correction_db = 2.5;
            out.source = std::string(c.device_key) + "@cal1";
            return out;
        }
    };
    auto corrector = std::make_shared<Corrector>();
    Fixture f({}, nullptr, corrector);
    f.p->on_radio_record(cell(f.now(), 390'012'500, -40.0));
    REQUIRE(f.out->records.size() == 1);
    const ym::MappedRecord& r = f.out->records[0];
    CHECK(r.level == doctest::Approx(-97.5));
    CHECK(r.radio_level == doctest::Approx(-40.0));
    CHECK(r.flags == 0);  // calibrated: no UNCALIBRATED
    CHECK(r.correction.source == "dev:0@cal1");
    CHECK(r.correction.correction_db == doctest::Approx(2.5));
    CHECK(corrector->asked == std::vector<std::uint64_t>{390'012'500});

    SUBCASE("a radio that reports dBm gets the antenna correction only") {
        ym::RadioRecord d = base(kCellDbm, 1, f.now(), 0);
        d.record.values[0] = std::uint64_t{390'012'500};
        d.record.values[2] = yp::EnumValue{YR_ROLE_SERVING};
        d.record.values[3] = -71.0;
        d.record.values[1] = d.record.values[4] = d.record.values[5] = d.record.values[6] =
            d.record.values[7] = std::uint64_t{1};
        f.p->on_radio_record(d);
        REQUIRE(f.out->records.size() == 2);
        CHECK(f.out->records[1].level == doctest::Approx(-68.5));
        CHECK_FALSE(f.out->records[1].radio_dbfs);
        CHECK(f.out->records[1].flags == 0);
    }
    SUBCASE("a new corrector applies to later records") {
        f.p->set_corrector(nullptr);  // identity
        f.p->on_radio_record(cell(f.now() + 1, 390'012'500, -40.0));
        CHECK(f.out->records[1].level == doctest::Approx(-40.0));
        CHECK(f.out->records[1].flags == sf::kUncalibrated);
    }
}

TEST_CASE("pipeline: records map to the technology plugin's schema of the same id (PLG-05, PLG-12)") {
    auto catalog = std::make_shared<ym::TechnologyCatalog>();
    yp::Schema tech = ym::generic_schema(kCell);
    tech.label = "Technology cell";
    tech.fields.push_back({"extra", "Extra", "", "", yp::FieldType::UInt, yp::Category::SystemInfo,
                           false, false, true, false, {}});
    yp::Schema other;
    other.id = "unrelated";
    catalog->add("xtech", "org.example.xtech", "4.5.6", {other, tech});
    Fixture f({}, catalog);
    f.p->on_radio_record(cell(f.now(), 390'012'500, -50.0));
    REQUIRE(f.out->records.size() == 1);
    const ym::MappedRecord& r = f.out->records[0];
    CHECK_FALSE(r.generic);
    CHECK(r.provenance.plugin_id == "org.example.xtech");
    CHECK(r.provenance.plugin_version == "4.5.6");
    CHECK(r.schema_index == 1);
    CHECK(r.schema->label == "Technology cell");
    CHECK(r.record.values.size() == tech.fields.size());
    CHECK(std::holds_alternative<std::monostate>(r.record.values.back()));

    SUBCASE("a schema that does not match falls back to the generic one, logged once") {
        yp::Schema bad = ym::generic_schema(kCell);
        bad.fields.erase(bad.fields.begin() + 9);  // drops evm_mer_db
        catalog->add("xtech", "org.example.bad", "1", {bad});
        f.p->on_radio_record(cell(f.now() + 1, 390'012'500, -50.0));
        f.p->on_radio_record(cell(f.now() + 2, 390'012'500, -50.0));
        REQUIRE(f.out->records.size() == 3);
        CHECK(f.out->records[2].generic);
        CHECK(f.p->stats().schema_mismatch == 1);
        REQUIRE(f.log.size() == 1);
        CHECK(f.log[0].find("evm_mer_db") != std::string::npos);
    }
    SUBCASE("other technology ids are not mapped onto it") {
        ym::RadioRecord r2 = cell(f.now() + 1, 390'012'500, -50.0);
        r2.plugin_id = "test.other_radio";  // the technology id is per radio plugin
        r2.technology_id = "ytech";
        f.p->on_radio_record(r2);
        CHECK(f.out->records.back().generic);
    }
}

TEST_CASE("pipeline: stream events set flags: sync lost, data loss, device lost (RF-08)") {
    using K = ym::RadioEvent::Kind;
    Fixture f;
    f.p->on_radio_record(cell(f.now(), 390'012'500, -50.0));
    f.p->on_radio_record(cell(f.now(), 390'037'500, -70.0, YR_ROLE_NEIGHBOUR));
    f.p->on_radio_event(event(K::Stream, YR_EVENT_SYNC_LOST, 390'012'500));
    f.p->on_radio_event(event(K::Stream, YR_EVENT_OVERFLOW, 3));
    ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 2);
    CHECK(c.samples[0].flags == (sf::kUncalibrated | sf::kNoSync | sf::kDataLoss));
    CHECK(c.samples[1].flags == (sf::kUncalibrated | sf::kDataLoss));
    // Data loss belongs to its cycle; no sync stays until a synchronised record.
    c = f.next();
    CHECK(c.samples[0].flags == (sf::kUncalibrated | sf::kNoSync | sf::kHeld));
    f.p->on_radio_record(cell(f.now(), 390'012'500, -51.0));
    CHECK(f.next().samples[0].flags == sf::kUncalibrated);
    // A record without sync carries NO_SYNC itself.
    f.p->on_radio_record(cell(f.now(), 390'012'500, -52.0, YR_ROLE_SERVING, YR_RECORD_NO_SYNC));
    CHECK(f.next().samples[0].flags == (sf::kUncalibrated | sf::kNoSync));

    f.p->on_radio_event(event(K::Lost));
    c = f.next();
    CHECK((c.samples[0].flags & sf::kDeviceLost) != 0);
    CHECK((c.samples[1].flags & sf::kDeviceLost) != 0);
    f.p->on_radio_event(event(K::Reopened));
    CHECK((f.next().flags & sf::kDeviceLost) == 0);
    // A stop removes the device's carriers.
    f.p->on_radio_event(event(K::Stopped));
    CHECK(f.next().samples.empty());
    CHECK(f.out->events.size() == 5);
    CHECK(f.p->stats().events == 5);
}

TEST_CASE("pipeline: a SIMULATED radio's samples are flagged simulated (DEMO-07, RF-08)") {
    Fixture f;
    ym::RadioRecord r = cell(f.now(), 390'012'500, -50.0, YR_ROLE_SERVING, YR_RECORD_SIMULATED);
    r.capabilities = YR_CAP_SIMULATED;
    f.p->on_radio_record(r);
    ym::RadioRecord r2 = cell(f.now(), 390'037'500, -50.0);
    r2.capabilities = YR_CAP_SIMULATED;  // flag missing in the record: set from the capability
    f.p->on_radio_record(r2);
    const ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 2);
    for (const auto& s : c.samples) CHECK(s.flags == (sf::kSimulated | sf::kUncalibrated));
}

TEST_CASE("pipeline: scan channels and neighbour list entries are carriers of their own (RF-03, RF-04)") {
    Fixture f;
    for (std::uint64_t ch = 0; ch < 3; ++ch) {
        ym::RadioRecord s = base(kScan, 1, f.now(), 0);
        s.record.values[0] = 390'000'000 + ch * 25'000;
        s.record.values[2] = yp::EnumValue{YR_ROLE_OTHER};
        s.record.values[3] = -90.0 + static_cast<double>(ch);
        s.record.values[1] = s.record.values[4] = s.record.values[5] = s.record.values[6] =
            s.record.values[7] = std::uint64_t{1};
        f.p->on_radio_record(s);
    }
    for (std::uint64_t i = 0; i < 2; ++i) {
        ym::RadioRecord n = base(kNeighbour, 2, f.now(), 0);
        n.record.values[0] = std::uint64_t{390'012'500};
        n.record.values[1] = i;
        n.record.values[2] = std::uint64_t{2};
        n.record.values[3] = 100 + i;
        n.present = YR_GROUP_NEIGHBOURS;
        f.p->on_radio_record(n);
    }
    ym::MeasurementCycle c = f.next();
    REQUIRE(c.samples.size() == 5);
    const auto scan = c.with_role(ym::CarrierRole::ScanChannel);
    REQUIRE(scan.size() == 3);
    CHECK(scan[2]->latest.level == doctest::Approx(-88.0));
    const auto entries = c.with_role(ym::CarrierRole::NeighbourEntry);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1]->latest.entry_index == 1);
    CHECK(std::isnan(entries[0]->latest.level));
    CHECK(entries[0]->latest.flags == 0);  // no level: nothing to calibrate
    CHECK(c.serving() == nullptr);
    // Scan channels stay fresh far longer (a pass takes long); list entries go stale.
    for (int i = 0; i < 4; ++i) c = f.next();
    REQUIRE(c.samples.size() == 5);
    CHECK((c.with_role(ym::CarrierRole::ScanChannel)[0]->flags & sf::kStale) == 0);
    CHECK((c.with_role(ym::CarrierRole::NeighbourEntry)[0]->flags & sf::kStale) != 0);
}

TEST_CASE("pipeline: records from several threads while cycles run (TSan)") {
    Fixture f;
    std::atomic<bool> stop{false};
    std::thread ticker([&] {
        for (int i = 0; i < 2000 && !stop; ++i) {
            f.clock->advance(100'000'000);
            f.p->tick();
        }
    });
    std::vector<std::thread> radios;
    for (int d = 0; d < 3; ++d) {
        radios.emplace_back([&f, d] {
            for (int i = 0; i < 2000; ++i) {
                ym::RadioRecord r = cell(f.now(), 390'012'500 + static_cast<std::uint64_t>(i % 5) * 25'000,
                                         -50.0 - d);
                r.device_key = "dev:" + std::to_string(d);
                f.p->on_radio_record(r);
            }
        });
    }
    for (auto& t : radios) t.join();
    stop = true;
    ticker.join();
    const ym::PipelineStats st = f.p->stats();
    CHECK(st.records == 6000);
    CHECK(st.accepted + st.duplicates + st.out_of_order == 6000);
    CHECK(st.out_of_order == 0);  // one thread per device: in order
    std::lock_guard lock(f.out->m);
    for (std::size_t i = 1; i < f.out->cycles.size(); ++i)
        CHECK(f.out->cycles[i].sequence == f.out->cycles[i - 1].sequence + 1);
}

namespace {

yantra::calibration::Key cal_key(const std::string& gain) {
    yantra::calibration::Key k;
    k.driver = "xdriver";
    k.serial = "x-0001";
    k.settings = {{"gain", gain}};
    return k;
}

std::shared_ptr<const yantra::calibration::Table> cal_table() {
    auto t = std::make_shared<yantra::calibration::Table>();
    for (const auto& [gain, offset] : {std::pair{"10", -60.0}, std::pair{"20", -70.0}}) {
        yantra::calibration::Entry e;
        e.key = cal_key(gain);
        e.created_unix = yantra::calibration::now_unix();
        yantra::calibration::Point a, b;
        a.frequency_hz = 390e6;
        a.offset_db = offset;
        b.frequency_hz = 400e6;
        b.offset_db = offset + 2.0;
        e.points = {a, b};
        t->put(e);
    }
    return t;
}

}  // namespace

TEST_CASE("pipeline: the calibration table converts dBFS by the record's calibration key (RF-02, HW-01b, TST-30)") {
    yantra::calibration::AntennaSettings antenna;
    antenna.all.antenna_gain_dbi = 5.0;  // offset -5 dB
    antenna.all.cable_loss_db = 1.25;    // +1.25 dB
    Fixture f({}, nullptr, std::make_shared<yantra::calibration::TableCorrector>(cal_table(), antenna));
    auto rec = [&](std::int64_t dt, std::uint64_t hz, double level, const std::string& gain) {
        ym::RadioRecord r = cell(f.now() + dt, hz, level);
        if (!gain.empty()) r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key(gain));
        f.p->on_radio_record(r);
        REQUIRE(!f.out->records.empty());
        return f.out->records.back();
    };
    // Gain 10 at 395 MHz: offset -59 (interpolated), antenna -3.75: exactly to 0.01 dB.
    ym::MappedRecord a = rec(0, 395'000'000, -40.0, "10");
    CHECK(std::fabs(a.level - (-40.0 - 59.0 - 3.75)) < 0.01);
    CHECK(a.flags == 0);
    CHECK(a.correction.calibrated);
    CHECK(a.correction.key == cal_key("10").id());
    CHECK(a.correction.correction_db == doctest::Approx(-3.75));
    // The CALIBRATION setting changed (radio host: new key): the other entry.
    ym::MappedRecord b = rec(1, 395'000'000, -40.0, "20");
    CHECK(std::fabs(b.level - (-40.0 - 69.0 - 3.75)) < 0.01);
    CHECK(b.correction.key == cal_key("20").id());
    // A combination without an entry: dBFS (with the antenna correction), flagged.
    ym::MappedRecord c = rec(2, 395'000'000, -40.0, "30");
    CHECK(c.level == doctest::Approx(-43.75));
    CHECK(c.flags == sf::kUncalibrated);
    CHECK_FALSE(c.correction.calibrated);
    // No key at all (unknown device configuration): the same.
    ym::MappedRecord d = rec(3, 395'000'000, -40.0, "");
    CHECK(d.flags == sf::kUncalibrated);
    // Over the frequency range, every record exactly (TST-30).
    for (int i = 0; i <= 10; ++i) {
        const std::uint64_t hz = 390'000'000 + static_cast<std::uint64_t>(i) * 1'000'000;
        const ym::MappedRecord r = rec(10 + i, hz, -50.0, "20");
        CHECK(std::fabs(r.level - (-50.0 - 70.0 + 0.2 * i - 3.75)) < 0.01);
    }
}

TEST_CASE("pipeline: the antenna correction is applied once to level readings and never to thresholds (HW-09)") {
    // A technology schema for x_thr: the level is LEVEL, the broadcast
    // thresholds SYSTEM_INFO, the radio's ground truth in dBm a LEVEL too.
    yp::Schema tech = ym::generic_schema(kThr);
    for (yp::Field& fld : tech.fields)
        if (fld.name == "truth_dbm") fld.category = yp::Category::Level;
    auto catalog = std::make_shared<ym::TechnologyCatalog>();
    catalog->add("xtech", "org.example.xtech", "1.0.0", {tech});
    yantra::calibration::AntennaSettings antenna;
    antenna.all.attenuator_db = 10.0;
    Fixture f({}, catalog, std::make_shared<yantra::calibration::TableCorrector>(cal_table(), antenna));
    ym::RadioRecord r = base(kThr, 0, f.now(), 0);
    auto& v = r.record.values;
    v[0] = std::uint64_t{390'000'000};
    v[1] = std::uint64_t{3600};
    v[2] = yp::EnumValue{YR_ROLE_SERVING};
    v[3] = -40.0;
    v[4] = v[5] = v[6] = v[7] = std::uint64_t{1};
    v[8] = -110.0;
    v[9] = std::int64_t{30};
    v[10] = -97.5;
    r.present = YR_GROUP_BROADCAST | YR_GROUP_OTHER;
    r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));
    f.p->on_radio_record(r);
    REQUIRE(f.out->records.size() == 1);
    const ym::MappedRecord& m = f.out->records[0];
    CHECK_FALSE(m.generic);
    const auto value = [&](const char* name) { return m.record.values[*tech.field_index(name)]; };
    CHECK(std::get<double>(value("level")) == doctest::Approx(-40.0 - 60.0 + 10.0));  // once
    CHECK(m.level == doctest::Approx(-90.0));
    CHECK(std::get<double>(value("rx_min_dbm")) == -110.0);       // threshold untouched
    CHECK(std::get<std::int64_t>(value("tx_max_dbm")) == 30);     // threshold untouched
    CHECK(std::get<double>(value("truth_dbm")) == -97.5);         // not a reading of this radio

    SUBCASE("a technology schema with a broadcast threshold as LEVEL is refused") {
        yp::Schema bad = ym::generic_schema(kThr);
        for (yp::Field& fld : bad.fields)
            if (fld.name == "rx_min_dbm") {
                fld.category = yp::Category::Level;
            }
        const auto problems = yp::check_radio_schema(bad, kThr);
        REQUIRE(problems.size() == 1);
        CHECK(problems[0].find("rx_min_dbm") != std::string::npos);
        CHECK(problems[0].find("SYSTEM_INFO") != std::string::npos);
        // The generic schema keeps them SYSTEM_INFO.
        const yp::Schema g = ym::generic_schema(kThr);
        CHECK(g.fields[*g.field_index("rx_min_dbm")].category == yp::Category::SystemInfo);
    }
}

TEST_CASE("pipeline: a SELF_CALIBRATED radio's dBm levels get no table and no UNCALIBRATED flag (RAD-21, HW-09)") {
    yantra::calibration::AntennaSettings antenna;
    antenna.all.cable_loss_db = 3.0;
    Fixture f({}, nullptr, std::make_shared<yantra::calibration::TableCorrector>(cal_table(), antenna));
    ym::RadioRecord d = base(kCellDbm, 1, f.now(), 0);
    d.record.values[0] = std::uint64_t{390'000'000};
    d.record.values[2] = yp::EnumValue{YR_ROLE_SERVING};
    d.record.values[3] = -71.0;
    d.record.values[1] = d.record.values[4] = d.record.values[5] = d.record.values[6] = d.record.values[7] =
        std::uint64_t{1};
    d.capabilities = YR_CAP_SELF_CALIBRATED;
    d.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));  // ignored
    f.p->on_radio_record(d);
    REQUIRE(f.out->records.size() == 1);
    const ym::MappedRecord& m = f.out->records[0];
    CHECK(m.level == doctest::Approx(-68.0));
    CHECK(m.flags == 0);
    CHECK(m.correction.source == "radio");
    CHECK_FALSE(m.correction.calibrated);
}

namespace {

// A schema with C1 (RF-06): level, the radio's C1 and two access parameters.
FIELD(f_c1, YR_FIELD_C1_DB, YR_TYPE_DOUBLE, YR_GROUP_C1, 0, "dB", nullptr, 0);
FIELD(f_c1rx, "rx_min_dbm", YR_TYPE_INT, YR_GROUP_BROADCAST, 0, "dBm", nullptr, 0);
const yr_field_def* const kC1Fields[] = {&f_freq, &f_chan, &f_role, &f_level, &f_mcc,
                                         &f_mnc,  &f_la,   &f_cc,   &f_c1,    &f_c1rx};
const yr_schema kC1Schema = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_c1", "Cell", 1, 10,
                             kC1Fields, YR_GROUP_C1 | YR_GROUP_BROADCAST, 0};

std::atomic<int> c1_calls{0};

// A technology's C1: level - rx_min - (ms_power - 30); UNSUPPORTED without rx_min.
yantra_status test_c1(yantra_plugin_instance*, const yantra_c1_input* in, double* out) {
    ++c1_calls;
    if (in->record->value_count < 10 || in->record->values[9].type != YANTRA_TYPE_INT)
        return YANTRA_ERR_UNSUPPORTED;
    if (in->record->values[9].u.i64 == 0) return YANTRA_ERR_INVALID_ARGUMENT;
    *out = in->level_dbm - static_cast<double>(in->record->values[9].u.i64) - (in->ms_power_dbm - 30.0);
    return YANTRA_OK;
}

ym::RadioRecord c1_record(std::int64_t t, double level, std::optional<double> c1,
                          std::optional<std::int64_t> rx_min, std::uint32_t flags = 0) {
    ym::RadioRecord r = base(kC1Schema, 0, t, flags);
    auto& v = r.record.values;
    v[0] = std::uint64_t{390'000'000};
    v[2] = yp::EnumValue{YR_ROLE_SERVING};
    v[3] = level;
    v[1] = v[4] = v[5] = v[6] = v[7] = std::uint64_t{1};
    if (c1) v[8] = *c1;
    if (rx_min) v[9] = *rx_min;
    r.present = (c1 ? YR_GROUP_C1 : 0u) | (rx_min ? YR_GROUP_BROADCAST : 0u);
    return r;
}

}  // namespace

TEST_CASE("pipeline: C1 is the radio's, else estimated by the technology plugin and flagged (RF-06)") {
    yantra_technology_api api{};
    api.struct_size = sizeof api;
    api.compute_c1 = test_c1;
    auto rules = std::make_shared<ym::TechnologyRules>();
    rules->api = &api;
    rules->cell_schema = 0;
    auto catalog = std::make_shared<ym::TechnologyCatalog>();
    catalog->add("xtech", "org.example.xtech", "1.0.0", {ym::generic_schema(kC1Schema)}, rules);
    ym::PipelineConfig config;
    config.ms_power_dbm = 35.0;
    yantra::calibration::AntennaSettings antenna;
    antenna.all.cable_loss_db = 3.0;
    Fixture f(config, catalog, std::make_shared<yantra::calibration::TableCorrector>(cal_table(), antenna));
    const auto c1_of = [](const ym::MappedRecord& m) { return m.record.values[8]; };

    SUBCASE("a radio's C1 is kept as delivered") {
        ym::RadioRecord r = c1_record(f.now(), -40.0, 51.25, std::int64_t{-110});
        r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));
        f.p->on_radio_record(r);
        REQUIRE(f.out->records.size() == 1);
        CHECK(std::get<double>(c1_of(f.out->records[0])) == 51.25);
        CHECK(f.out->records[0].c1_source == ym::C1Source::Radio);
        CHECK((f.out->records[0].flags & sf::kC1Estimated) == 0);
        CHECK(f.p->stats().c1_radio == 1);
        CHECK(c1_calls == 0);
    }
    SUBCASE("a missing C1 is estimated from the calibrated level and the MS power") {
        ym::RadioRecord r = c1_record(f.now(), -40.0, std::nullopt, std::int64_t{-110});
        r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));
        f.p->on_radio_record(r);
        REQUIRE(f.out->records.size() == 1);
        const ym::MappedRecord& m = f.out->records[0];
        CHECK(m.level == doctest::Approx(-40.0 - 60.0 + 3.0));  // cable loss added back
        CHECK(std::get<double>(c1_of(m)) == doctest::Approx(-97.0 + 110.0 - 5.0));
        CHECK(m.c1_source == ym::C1Source::Estimated);
        CHECK((m.flags & sf::kC1Estimated) != 0);
        CHECK((m.record.flags & sf::kC1Estimated) == 0);  // the record keeps record bits only
        CHECK(ym::sample_flags_text(m.flags) == "c1_estimated");
        CHECK(f.p->stats().c1_estimated == 1);
        // The flag reaches the cycle sample.
        const ym::MeasurementCycle c = f.next();
        REQUIRE(c.samples.size() == 1);
        CHECK((c.samples[0].flags & sf::kC1Estimated) != 0);
    }
    SUBCASE("an uncalibrated level gets no C1") {
        f.p->on_radio_record(c1_record(f.now(), -40.0, std::nullopt, std::int64_t{-110}));
        REQUIRE(f.out->records.size() == 1);
        CHECK(std::holds_alternative<std::monostate>(c1_of(f.out->records[0])));
        CHECK(f.out->records[0].c1_source == ym::C1Source::None);
        CHECK(c1_calls == 0);
    }
    SUBCASE("without the broadcast parameters the plugin answers UNSUPPORTED: no C1, no log") {
        ym::RadioRecord r = c1_record(f.now(), -40.0, std::nullopt, std::nullopt);
        r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));
        f.p->on_radio_record(r);
        REQUIRE(f.out->records.size() == 1);
        CHECK(std::holds_alternative<std::monostate>(c1_of(f.out->records[0])));
        CHECK(f.p->stats().c1_failed == 0);
        CHECK(f.log.empty());
    }
    SUBCASE("an error answer leaves C1 empty, is counted and logged once") {
        for (int i = 0; i < 2; ++i) {
            ym::RadioRecord r = c1_record(f.now() + i, -40.0, std::nullopt, std::int64_t{0});
            r.calibration_key = std::make_shared<const yantra::calibration::Key>(cal_key("10"));
            f.p->on_radio_record(r);
        }
        REQUIRE(f.out->records.size() == 2);
        CHECK(std::holds_alternative<std::monostate>(c1_of(f.out->records[1])));
        CHECK(f.p->stats().c1_failed == 2);
        REQUIRE(f.log.size() == 1);
        CHECK(f.log[0].find("compute_c1") != std::string::npos);
    }
    c1_calls = 0;
}

TEST_CASE("pipeline: the MS power for C1 must be between 10 and 50 dBm (RF-06)") {
    ym::PipelineConfig c;
    CHECK(c.ms_power_dbm == 30.0);
    CHECK(c.validate().empty());
    for (double bad : {9.0, 51.0, std::nan("")}) {
        c.ms_power_dbm = bad;
        CHECK_FALSE(c.validate().empty());
    }
}
