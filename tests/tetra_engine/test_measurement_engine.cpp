// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The measurement engine (RAD-102, RAD-110..114, RAD-21..27) on generator
// spans: records at the record interval with level, identity, quality,
// broadcast parameters and frame number; calibration and C1; level
// linearity (RAD-125 groundwork) and level steps; several carriers and the
// serving choice; overload, non-finite samples and discontinuities; scan
// records; SYNC events; and the same records whatever the thread count.
// Every record passes the radio API's record validator.

#include <doctest/doctest.h>

#include "span.hpp"

#include <yantra/radio.hpp>
#include <yantra/receiver/measurement_engine.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace yantra::receiver::tetra;
using enginetest::cf;
using enginetest::Span;
using enginetest::SpanCell;
namespace radios = yantra::radios;

namespace {

constexpr double kCentre = 390.0e6;  // carrier 3600 of band 3 (CellConfig::defaults)
constexpr double kRate = 250000.0;

/// A plugin descriptor around the engine's schemas, for validate_record.
struct Descriptor {
    std::vector<std::unique_ptr<radios::SchemaView>> views;
    std::vector<const yr_schema*> ptrs;
    yr_descriptor d{};
    explicit Descriptor(const MeasurementEngine& e)
    {
        for (const radios::SchemaDef& s : e.schemas()) {
            views.push_back(std::make_unique<radios::SchemaView>(s));
            ptrs.push_back(&views.back()->schema_c);
        }
        d.struct_size = sizeof(yr_descriptor);
        d.capabilities = (e.config().calibrated ? YR_CAP_SELF_CALIBRATED : 0u) |
                         (e.config().simulated ? YR_CAP_SIMULATED : 0u) | (e.config().scan_records ? YR_CAP_SCAN : 0u);
        d.schema_count = static_cast<uint32_t>(ptrs.size());
        d.schemas = ptrs.data();
    }
};

struct Run {
    EngineOutput out;
    std::vector<radios::Record> records;
    std::vector<radios::Event> events;
};

/// Feeds `seconds` of the span in 0.1 s blocks (a time base at the first).
void feed(MeasurementEngine& e, Span& span, double seconds, Run& run, const std::function<bool(double)>& gate = {},
          double centre = kCentre)
{
    const auto block = static_cast<std::size_t>(span.rate() / 10.0);
    std::vector<cf> x(block);
    const auto blocks = static_cast<int>(std::lround(seconds * 10.0));
    for (int b = 0; b < blocks; ++b) {
        span.read(x.data(), block, gate);
        BlockInfo info;
        if (e.stats().samples == 0)
            info.monotonic_ns = 1'000'000'000;
        run.out.clear();
        e.process(x.data(), block, centre, span.rate(), info, run.out);
        run.records.insert(run.records.end(), run.out.records.begin(), run.out.records.end());
        run.events.insert(run.events.end(), run.out.events.begin(), run.out.events.end());
    }
}

const radios::Value& value(const MeasurementEngine& e, const radios::Record& r, const char* name)
{
    const int i = e.schemas()[r.schema_index].field_index(name);
    REQUIRE(i >= 0);
    return r.values[static_cast<std::size_t>(i)];
}

double num(const MeasurementEngine& e, const radios::Record& r, const char* name)
{
    const radios::Value& v = value(e, r, name);
    switch (v.type) {
    case YR_TYPE_DOUBLE: return v.f64;
    case YR_TYPE_UINT: return static_cast<double>(v.u64);
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
    case YR_TYPE_BOOL: return static_cast<double>(v.i64);
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

bool has(const MeasurementEngine& e, const radios::Record& r, const char* name)
{
    return value(e, r, name).type != YR_TYPE_NONE;
}

void check_valid(const MeasurementEngine& e, const std::vector<radios::Record>& records)
{
    const Descriptor d(e);
    std::size_t bad = 0;
    for (const radios::Record& r : records) {
        const radios::RecordView v(r);
        const std::vector<std::string> p = yantra::radio::validate_record(d.d, v.record);
        if (!p.empty() && bad++ < 3)
            for (const std::string& s : p)
                MESSAGE(s);
    }
    CHECK(bad == 0);
}

/// Cell records of carrier `hz` (within 1 Hz).
std::vector<const radios::Record*> of(const MeasurementEngine& e, const std::vector<radios::Record>& rs, double hz)
{
    std::vector<const radios::Record*> v;
    for (const radios::Record& r : rs)
        if (r.schema_index == kEngineCellSchema && std::fabs(num(e, r, YR_FIELD_FREQUENCY_HZ) - hz) < 1.0)
            v.push_back(&r);
    return v;
}

int count_events(const std::vector<radios::Event>& ev, uint32_t kind)
{
    int n = 0;
    for (const radios::Event& e : ev)
        n += e.event == kind ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("engine: one carrier, records every interval with level, identity, quality, broadcast, frame")
{
    SpanCell c;
    c.level_dbfs = -30.0;
    c.colour_code = 7;
    c.mnc = 12;
    c.location_area = 345;
    Span span(kRate, {c}, -90.0, 3);
    MeasurementEngine e;
    Run run;
    feed(e, span, 6.0, run);
    check_valid(e, run.records);
    const auto recs = of(e, run.records, kCentre);
    REQUIRE(recs.size() == 6);  // one per second (RAD-112: >= 1 Hz)
    // Interval centres on the given time base.
    for (std::size_t i = 0; i < recs.size(); ++i)
        CHECK(recs[i]->time.monotonic_ns == 1'000'000'000 + 500'000'000 + static_cast<int64_t>(i) * 1'000'000'000);
    // Level from the first record on, synced records from the third on.
    for (const radios::Record* r : recs)
        CHECK(std::fabs(num(e, *r, YR_FIELD_LEVEL) + 30.0) < 0.5);
    const radios::Record& r = *recs.back();
    CHECK((r.flags & YR_RECORD_NO_SYNC) == 0);
    CHECK(e.schemas()[0].fields[YR_CORE_LEVEL].unit == "dBFS");
    CHECK(num(e, r, YR_FIELD_ROLE) == YR_ROLE_SERVING);
    CHECK(num(e, r, YR_FIELD_MCC) == 901);
    CHECK(num(e, r, YR_FIELD_MNC) == 12);
    CHECK(num(e, r, YR_FIELD_LA) == 345);
    CHECK(num(e, r, YR_FIELD_COLOUR_CODE) == 7);
    CHECK(num(e, r, YR_FIELD_CHANNEL) == 3600);
    // Quality (RAD-113).
    CHECK((r.present & YR_GROUP_QUALITY) != 0);
    CHECK(num(e, r, YR_FIELD_MER_PCT) <= 1.0);
    CHECK(num(e, r, "crc_error_pct_aach") <= 1.0);
    CHECK(num(e, r, YR_FIELD_SNR_DB) > 25.0);
    CHECK(std::fabs(num(e, r, YR_FIELD_FREQ_ERROR_HZ)) < 20.0);
    CHECK(has(e, r, YR_FIELD_EVM_MER_DB));
    // C1 inputs without C1 (dBFS levels).
    CHECK(has(e, r, "rxlev_access_min_dbm"));
    CHECK(has(e, r, "ms_txpwr_max_cell_dbm"));
    CHECK_FALSE(has(e, r, YR_FIELD_C1_DB));
    CHECK(num(e, r, "main_carrier") == 3600);
    CHECK(num(e, r, "frequency_band") == 3);
    CHECK(num(e, r, "encryption") == 0);
    // Frame number (DEC-11).
    CHECK((r.present & YR_GROUP_FRAME) != 0);
    CHECK(num(e, r, YR_FIELD_SLOT) >= 1);
    CHECK(num(e, r, YR_FIELD_SLOT) <= 4);
    // Events.
    CHECK(count_events(run.events, YR_EVENT_SYNC_FOUND) == 1);
    CHECK(count_events(run.events, YR_EVENT_SERVING_CHANGED) == 1);
    REQUIRE(e.serving_hz());
    CHECK(*e.serving_hz() == doctest::Approx(kCentre));
    CHECK(e.stats().intervals == 6);
    CHECK(e.stats().samples == static_cast<uint64_t>(6 * kRate));
}

TEST_CASE("engine: calibration gives dBm and C1 (RAD-111, clause 23.3)")
{
    SpanCell c;
    c.level_dbfs = -25.0;
    Span span(kRate, {c}, -90.0, 4);
    EngineConfig cfg;
    cfg.calibrated = true;
    cfg.level_offset_db = -50.0;  // 0 dBFS = -50 dBm
    MeasurementEngine e(cfg);
    CHECK(e.schemas()[0].fields[YR_CORE_LEVEL].unit == "dBm");
    Run run;
    feed(e, span, 5.0, run);
    check_valid(e, run.records);
    const auto recs = of(e, run.records, kCentre);
    REQUIRE(!recs.empty());
    const radios::Record& r = *recs.back();
    const double level = num(e, r, YR_FIELD_LEVEL);
    CHECK(std::fabs(level + 75.0) < 0.5);
    REQUIRE(has(e, r, YR_FIELD_C1_DB));
    const double expect = radios::tetra_c1_db(level, num(e, r, "rxlev_access_min_dbm"),
                                              num(e, r, "ms_txpwr_max_cell_dbm"));
    CHECK(num(e, r, YR_FIELD_C1_DB) == doctest::Approx(expect));
}

TEST_CASE("engine: level linearity of a pinned carrier from -10 to -100 dBFS (RAD-125 groundwork)")
{
    for (double level : {-10.0, -30.0, -50.0, -70.0, -90.0, -100.0}) {
        CAPTURE(level);
        SpanCell c;
        c.level_dbfs = level;
        Span span(kRate, {c}, -300.0);  // no noise: the meter alone
        EngineConfig cfg;
        cfg.carriers = {kCentre};
        cfg.search = false;
        MeasurementEngine e(cfg);
        Run run;
        feed(e, span, 2.0, run);
        check_valid(e, run.records);
        const auto recs = of(e, run.records, kCentre);
        REQUIRE(recs.size() == 2);
        for (const radios::Record* r : recs)
            CHECK(std::fabs(num(e, *r, YR_FIELD_LEVEL) - level) < 0.3);
    }
}

TEST_CASE("engine: level steps follow within one interval")
{
    SpanCell c;
    c.level_dbfs = -40.0;
    Span span(kRate, {c}, -90.0, 5);
    EngineConfig cfg;
    cfg.carriers = {kCentre};
    MeasurementEngine e(cfg);
    Run run;
    // On for 4 s, off (noise only) for 3 s, on again for 5 s.
    feed(e, span, 12.0, run, [](double t) { return t < 4.0 || t >= 7.0; });
    check_valid(e, run.records);
    const auto recs = of(e, run.records, kCentre);
    REQUIRE(recs.size() == 12);
    // Noise in the channel: -90 dBFS over 250 kHz, about -100 dBFS in 25 kHz.
    for (int i : {0, 1, 2, 3, 7, 8, 9, 10, 11})
        CHECK(std::fabs(num(e, *recs[i], YR_FIELD_LEVEL) + 40.0) < 0.5);
    for (int i : {5, 6})
        CHECK(num(e, *recs[i], YR_FIELD_LEVEL) < -95.0);
    // The first interval after the step still holds the channel filters'
    // delay (a few ms) of the signal: more than 25 dB down.
    CHECK(num(e, *recs[4], YR_FIELD_LEVEL) < -65.0);
    // Synchronisation lost while off, found again after.
    CHECK(count_events(run.events, YR_EVENT_SYNC_LOST) >= 1);
    CHECK(count_events(run.events, YR_EVENT_SYNC_FOUND) >= 2);
    CHECK((recs[11]->flags & YR_RECORD_NO_SYNC) == 0);
}

TEST_CASE("engine: several carriers, the strongest serves, the others are measured (RAD-110)")
{
    std::vector<SpanCell> cells(3);
    cells[0].offset_hz = -50000.0;
    cells[0].level_dbfs = -35.0;
    cells[0].colour_code = 1;
    cells[0].main_carrier = 3598;
    cells[1].offset_hz = 0.0;
    cells[1].level_dbfs = -25.0;
    cells[1].colour_code = 2;
    cells[2].offset_hz = 75000.0 + 6250.0;
    cells[2].offset_code = 1;
    cells[2].level_dbfs = -45.0;
    cells[2].colour_code = 3;
    cells[2].main_carrier = 3603;
    cells[2].timing_offset_s = 0.013;
    Span span(kRate, cells, -90.0, 6);
    MeasurementEngine e;
    Run run;
    feed(e, span, 7.0, run);
    check_valid(e, run.records);
    for (const SpanCell& c : cells) {
        CAPTURE(c.offset_hz);
        const auto recs = of(e, run.records, kCentre + c.offset_hz);
        REQUIRE(recs.size() >= 3);
        const radios::Record& r = *recs.back();
        CHECK((r.flags & YR_RECORD_NO_SYNC) == 0);
        CHECK(num(e, r, YR_FIELD_COLOUR_CODE) == c.colour_code);
        CHECK(num(e, r, YR_FIELD_CHANNEL) == c.main_carrier);
        CHECK(std::fabs(num(e, r, YR_FIELD_LEVEL) - c.level_dbfs) < 0.5);
        CHECK(num(e, r, YR_FIELD_ROLE) == (c.colour_code == 2 ? YR_ROLE_SERVING : YR_ROLE_OTHER));
    }
    CHECK(count_events(run.events, YR_EVENT_SYNC_FOUND) == 3);
    REQUIRE(e.serving_hz());
    CHECK(*e.serving_hz() == doctest::Approx(kCentre));
}

TEST_CASE("engine: a configured serving carrier serves even when weaker")
{
    std::vector<SpanCell> cells(2);
    cells[0].offset_hz = -50000.0;
    cells[0].level_dbfs = -25.0;
    cells[0].main_carrier = 3598;
    cells[1].level_dbfs = -45.0;
    cells[1].colour_code = 9;
    Span span(kRate, cells, -90.0, 7);
    EngineConfig cfg;
    cfg.serving_hz = kCentre;
    MeasurementEngine e(cfg);
    Run run;
    feed(e, span, 5.0, run);
    const auto recs = of(e, run.records, kCentre);
    REQUIRE(!recs.empty());
    CHECK(num(e, *recs.back(), YR_FIELD_ROLE) == YR_ROLE_SERVING);
    CHECK(num(e, *of(e, run.records, kCentre - 50000.0).back(), YR_FIELD_ROLE) == YR_ROLE_OTHER);
}

TEST_CASE("engine: overload, non-finite samples and discontinuities")
{
    SpanCell c;
    c.level_dbfs = -30.0;
    Span span(kRate, {c}, -90.0, 8);
    EngineConfig cfg;
    MeasurementEngine e(cfg);
    Run run;
    feed(e, span, 4.0, run);
    const std::size_t block = 25000;
    std::vector<cf> x(block);
    // Overload and NaN samples in second 5.
    for (int b = 0; b < 10; ++b) {
        span.read(x.data(), block);
        BlockInfo info;
        if (b == 3) {
            info.overload = true;
            x[10] = {std::numeric_limits<float>::quiet_NaN(), 0.0f};
            x[11] = {std::numeric_limits<float>::infinity(), 1.0f};
        }
        run.out.clear();
        e.process(x.data(), block, kCentre, kRate, info, run.out);
        run.records.insert(run.records.end(), run.out.records.begin(), run.out.records.end());
        run.events.insert(run.events.end(), run.out.events.begin(), run.out.events.end());
    }
    check_valid(e, run.records);
    auto recs = of(e, run.records, kCentre);
    REQUIRE(recs.size() == 5);
    CHECK((recs[3]->flags & YR_RECORD_OVERLOAD) == 0);
    CHECK((recs[4]->flags & YR_RECORD_OVERLOAD) != 0);
    CHECK(std::isfinite(num(e, *recs[4], YR_FIELD_LEVEL)));
    CHECK(e.stats().non_finite == 2);
    CHECK(e.stats().overload_blocks == 1);
    // A discontinuity: every carrier starts over (SYNC_LOST), then is found again.
    span.read(x.data(), block);
    BlockInfo jump;
    jump.discontinuity = true;
    run.out.clear();
    e.process(x.data(), block, kCentre, kRate, jump, run.out);
    run.events.insert(run.events.end(), run.out.events.begin(), run.out.events.end());
    CHECK(count_events(run.out.events, YR_EVENT_SYNC_LOST) == 1);
    feed(e, span, 4.0, run);
    CHECK(count_events(run.events, YR_EVENT_SYNC_FOUND) == 2);
    recs = of(e, run.records, kCentre);
    CHECK((recs.back()->flags & YR_RECORD_NO_SYNC) == 0);
}

TEST_CASE("engine: scan records and SCAN_DONE (RAD-23)")
{
    SpanCell c;
    c.offset_hz = 25000.0;
    c.level_dbfs = -30.0;
    Span span(kRate, {c}, -90.0, 9);
    EngineConfig cfg;
    cfg.scan_records = true;
    MeasurementEngine e(cfg);
    REQUIRE(e.schemas().size() == 3);
    Run run;
    feed(e, span, 2.0, run);
    check_valid(e, run.records);
    CHECK(count_events(run.events, YR_EVENT_SCAN_DONE) == 2);
    std::map<uint64_t, double> last;
    for (const radios::Record& r : run.records)
        if (r.schema_index == kEngineScanSchema)
            last[static_cast<uint64_t>(num(e, r, YR_FIELD_FREQUENCY_HZ))] = num(e, r, YR_FIELD_LEVEL);
    REQUIRE(last.size() >= 7);
    REQUIRE(last.count(static_cast<uint64_t>(kCentre + 25000.0)) == 1);
    CHECK(std::fabs(last[static_cast<uint64_t>(kCentre + 25000.0)] + 30.0) < 1.0);
    CHECK(last[static_cast<uint64_t>(kCentre - 50000.0)] < -90.0);
}

TEST_CASE("engine: the same records with a worker pool")
{
    std::vector<SpanCell> cells(3);
    cells[0].offset_hz = -50000.0;
    cells[0].level_dbfs = -30.0;
    cells[1].level_dbfs = -26.0;
    cells[1].colour_code = 4;
    cells[2].offset_hz = 50000.0;
    cells[2].level_dbfs = -38.0;
    cells[2].colour_code = 5;
    std::vector<radios::Record> result[2];
    for (int k = 0; k < 2; ++k) {
        Span span(kRate, cells, -90.0, 10);
        EngineConfig cfg;
        cfg.threads = k == 0 ? 1 : 3;
        MeasurementEngine e(cfg);
        Run run;
        feed(e, span, 4.0, run);
        result[k] = run.records;
    }
    REQUIRE(result[0].size() == result[1].size());
    REQUIRE(!result[0].empty());
    for (std::size_t i = 0; i < result[0].size(); ++i)
        CHECK(result[0][i] == result[1][i]);
}
