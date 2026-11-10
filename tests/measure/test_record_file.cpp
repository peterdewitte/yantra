// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record files of yantra-radios (RAD-65, TST-10) read by the core and fed
// into the measurement input, and the mapping of radio records onto a
// technology schema (PLG-05, OA-13): by field name, dBFS levels to dBm
// with the calibration offset, UNCALIBRATED without one.

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

#include <yantra/core/calibration/corrector.h>
#include <yantra/core/calibration/table.h>
#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/core/measure/record_file.h>
#include <yantra/core/measure/technology_map.h>
#include <yantra/core/platform/clock.h>
#include <yantra/core/plugin/record.h>

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace yp = yantra::plugin;

namespace {

const fs::path kSource = YANTRA_SOURCE_DIR;
const fs::path kExample =
    kSource / "third_party/yantra-radios/tests/fixtures/record_files/example_v1.records.jsonl";
const fs::path kBeacon = kSource / "plugins/example_beacon/testdata/beacon.records.jsonl";

std::string all(const std::vector<std::string>& errors) {
    std::string s;
    for (const auto& e : errors) s += e + "\n";
    return s;
}

const std::string kHeader =
    R"({"format":"yantra-radio-records","version":1,"producer":"test","plugin":{"id":"p","technology_id":"t","capabilities":["simulated","future_cap"]},)"
    R"("schemas":[{"id":"cell","kind":"measurement","version":1,"groups":["quality"],"fields":[)"
    R"({"name":"frequency_hz","type":"uint","group":"core","unit":"Hz","flags":["key"]},)"
    R"({"name":"role","type":"enum","group":"core","unit":"","items":[{"value":1,"name":"serving","label":"Serving"}]},)"
    R"({"name":"level","type":"double","group":"core","unit":"dBFS"},)"
    R"({"name":"snr_db","type":"double","group":"quality","unit":"dB"}]}]})";

yp::Parsed<ym::RecordFile> parse(const std::string& text) {
    std::istringstream in(text);
    return ym::parse_record_file(in, "test.jsonl");
}

void refused(const std::string& text, const std::string& needle) {
    const auto p = parse(text);
    INFO(all(p.errors));
    REQUIRE_FALSE(p.ok());
    CHECK(all(p.errors).find(needle) != std::string::npos);
}

// A technology schema for the test radio schema "cell" (plugin.h data).
const yantra_field_def kFreq = {sizeof(yantra_field_def), YANTRA_TYPE_UINT,
                                YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_KEY, "frequency_hz",
                                "Frequency", "Hz", "Help.", nullptr, 0, 0};
const yantra_enum_item kRoles[] = {{1, "serving", "Serving"}, {2, "neighbour", "Neighbour"}};
const yantra_field_def kRole = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM,
                                YANTRA_CATEGORY_SYSTEM_INFO, 0, "role", "Role", "", "Help.",
                                kRoles, 2, 0};
const yantra_field_def kLevel = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE,
                                 YANTRA_CATEGORY_LEVEL, 0, "level", "Level", "dBm", "Help.",
                                 nullptr, 0, 0};
const yantra_field_def kSnr = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE,
                               YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL, "snr_db", "SNR",
                               "dB", "Help.", nullptr, 0, 0};
const yantra_field_def kExtra = {sizeof(yantra_field_def), YANTRA_TYPE_UINT,
                                 YANTRA_CATEGORY_SYSTEM_INFO, YANTRA_FIELD_OPTIONAL, "extra",
                                 "Extra", "", "Help.", nullptr, 0, 0};
// Technology order differs from the radio order on purpose.
const yantra_field_def* const kTechFields[] = {&kLevel, &kExtra, &kFreq, &kRole, &kSnr};
const yantra_schema kTech = {sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "cell", "Cell", 1,
                             5, kTechFields};

yp::Schema tech_schema() {
    auto s = yp::parse_schema(&kTech, YANTRA_CATEGORY_ALL_MASK);
    INFO(all(s.errors));
    REQUIRE(s.ok());
    return *s.value;
}

}  // namespace

TEST_CASE("record file: the yantra-radios example file is read") {
    const auto p = ym::read_record_file(kExample);
    INFO(all(p.errors));
    REQUIRE(p.ok());
    const ym::RecordFile& f = *p.value;
    CHECK(f.plugin_id == "example");
    CHECK(f.technology_id == "beacon");
    CHECK((f.capabilities & YR_CAP_SIMULATED) != 0);
    REQUIRE(f.schemas.size() == 2);
    CHECK(f.schemas[0].kind == YR_SCHEMA_MEASUREMENT);
    CHECK(f.schemas[1].kind == YR_SCHEMA_NEIGHBOUR);
    CHECK(f.record_count == 2);
    CHECK(f.event_count == 1);
    CHECK(f.complete);
    REQUIRE(f.lines.size() == 3);
    CHECK(std::get<ym::RadioEvent>(f.lines[0]).event == YR_EVENT_SYNC_FOUND);
    const auto& r = std::get<ym::RadioRecord>(f.lines[1]);
    CHECK(r.record.schema_id == "beacon");
    CHECK(r.field_names->at(0) == "frequency_hz");
    CHECK(std::get<std::uint64_t>(r.record.values[0]) == 392012500u);
    CHECK(std::get<double>(r.record.values[3]) == doctest::Approx(-48.5));
    CHECK((r.record.flags & YR_RECORD_SIMULATED) != 0);
    CHECK((r.present & YR_GROUP_QUALITY) != 0);
    CHECK(r.record.time.utc_source == YANTRA_TIME_SOURCE_SYSTEM);
}

TEST_CASE("record file: the example beacon plugin's golden file is complete (PLG-11)") {
    const auto p = ym::read_record_file(kBeacon);
    INFO(all(p.errors));
    REQUIRE(p.ok());
    CHECK(p.value->plugin_id == "beacon");
    CHECK(p.value->technology_id == "beacon");
    CHECK(p.value->complete);
    CHECK(p.value->dropped == 0);
    CHECK(p.value->record_count >= 20);
    ym::RecordQueue queue;
    CHECK(ym::replay(*p.value, queue) == p.value->record_count);
    CHECK(queue.record_count() == p.value->record_count);
    CHECK(queue.events().size() == p.value->event_count);
}

TEST_CASE("record file: versioning rules and refusals") {
    SUBCASE("unknown members, line types and flag names are ignored") {
        const auto p = parse(
            kHeader + "\n" +
            R"({"type":"record","schema":"cell","new_member":1,"flags":["simulated","future"],"values":{"frequency_hz":5,"role":1,"level":-40.5}})"
            "\n" R"({"type":"future_line","x":1})" "\n");
        INFO(all(p.errors));
        REQUIRE(p.ok());
        CHECK(p.value->record_count == 1);
        CHECK_FALSE(p.value->complete);
        const auto& r = std::get<ym::RadioRecord>(p.value->lines[0]);
        CHECK(r.record.flags == YR_RECORD_SIMULATED);
        CHECK(std::holds_alternative<std::monostate>(r.record.values[3]));
    }
    SUBCASE("refusals name the problem") {
        refused("", "empty file");
        refused(R"({"format":"other","version":1})", "not a yantra-radio-records file");
        refused(R"({"format":"yantra-radio-records","version":2})", "newer than this reader");
        refused(kHeader + "\n{oops", "test.jsonl:2: not valid JSON");
        refused(kHeader + "\n" + R"({"type":"record","schema":"nope","values":{}})",
                "unknown schema 'nope'");
        refused(kHeader + "\n" + R"({"type":"record","schema":"cell","values":{"bogus":1}})",
                "has no field 'bogus'");
        refused(kHeader + "\n" +
                    R"({"type":"record","schema":"cell","values":{"frequency_hz":-5}})",
                "does not fit the field type");
        refused(kHeader + "\n" + R"({"type":"record","schema":"cell","values":{"role":7}})",
                "not an item of the enum");
        refused(kHeader + "\n" + R"({"type":"event","event":"bogus"})", "unknown event");
        refused(kHeader + "\n" + R"({"type":"end"})" + "\n" + R"({"type":"event","event":"error"})",
                "after the end line");
    }
}

TEST_CASE("technology map: radio records become technology records by field name") {
    const auto p = parse(
        kHeader + "\n" +
        R"({"type":"record","schema":"cell","flags":["simulated"],"values":{"frequency_hz":430025000,"role":1,"level":-40.0,"snr_db":21.5}})");
    INFO(all(p.errors));
    REQUIRE(p.ok());
    const ym::RadioSchemaView radio(p.value->schemas[0]);
    const yp::Schema tech = tech_schema();
    const auto map = ym::SchemaMap::make(tech, 3, radio.get());
    INFO(all(map.errors));
    REQUIRE(map.ok());
    CHECK(map.value->tech_index() == 3);
    const auto& rr = std::get<ym::RadioRecord>(p.value->lines[0]);

    SUBCASE("uncalibrated: the dBFS level is kept and flagged") {
        const yp::Record r = map.value->map(rr.record, {});
        REQUIRE(r.values.size() == 5);
        CHECK(std::get<double>(r.values[0]) == doctest::Approx(-40.0));
        CHECK(std::holds_alternative<std::monostate>(r.values[1]));
        CHECK(std::get<std::uint64_t>(r.values[2]) == 430025000u);
        CHECK(std::get<yp::EnumValue>(r.values[3]).value == 1);
        CHECK(std::get<double>(r.values[4]) == doctest::Approx(21.5));
        CHECK((r.flags & YANTRA_RECORD_UNCALIBRATED) != 0);
        CHECK((r.flags & YANTRA_RECORD_SIMULATED) != 0);
        // The result is a valid record of the technology schema.
        const ym::RecordView view(r, 3);
        const yp::RecordCheck check = yp::parse_record(view.get(), tech);
        INFO(all(check.parsed.errors));
        CHECK(check.parsed.ok());
        CHECK(yp::record_key(*check.parsed.value, tech) == "frequency_hz=430025000");
    }
    SUBCASE("calibrated: dBm = dBFS + offset") {
        const yp::Record r = map.value->map(rr.record, {true, -60.0, 0.0});
        CHECK(std::get<double>(r.values[0]) == doctest::Approx(-100.0));
        CHECK((r.flags & YANTRA_RECORD_UNCALIBRATED) == 0);
    }
    SUBCASE("the antenna correction is added to every level (HW-09)") {
        const yp::Record r = map.value->map(rr.record, {true, -60.0, 2.25});
        CHECK(std::get<double>(r.values[0]) == doctest::Approx(-97.75));
        const yp::Record u = map.value->map(rr.record, {false, 0.0, -1.5});
        CHECK(std::get<double>(u.values[0]) == doctest::Approx(-41.5));
        CHECK((u.flags & YANTRA_RECORD_UNCALIBRATED) != 0);
    }
    SUBCASE("the generic schema of a radio schema describes it (PLG-12 generic views)") {
        const yp::Schema g = ym::generic_schema(radio.get());
        CHECK(g.id == "cell");
        CHECK(yp::check_radio_schema(g, radio.get()).empty());
        const auto level = g.field_index("level");
        REQUIRE(level);
        CHECK(g.fields[*level].unit == "dBm");
        CHECK(g.fields[*level].category == yp::Category::Level);
        CHECK_FALSE(g.fields[*level].optional);
        const auto gm = ym::SchemaMap::make(g, 0, radio.get());
        CHECK(gm.ok());
    }
}

TEST_CASE("technology map: a radio schema the technology does not describe is refused") {
    const std::string header = R"({"format":"yantra-radio-records","version":1,"plugin":{"id":"p","technology_id":"t"},)"
                               R"("schemas":[{"id":"cell","kind":"measurement","version":1,"fields":[)"
                               R"({"name":"frequency_hz","type":"uint","unit":"Hz"},)"
                               R"({"name":"level","type":"double","unit":"dBFS"},)"
                               R"({"name":"rogue","type":"uint","unit":""}]}]})";
    const auto p = parse(header);
    REQUIRE(p.ok());
    const ym::RadioSchemaView radio(p.value->schemas[0]);
    const auto map = ym::SchemaMap::make(tech_schema(), 0, radio.get());
    REQUIRE_FALSE(map.ok());
    const std::string e = all(map.errors);
    CHECK(e.find("'rogue'") != std::string::npos);
    CHECK(e.find("'role'") != std::string::npos);  // not delivered and not OPTIONAL
}

namespace {

// What a pipeline gave out for a replay.
struct Replayed final : ym::MeasurementObserver {
    std::vector<ym::MappedRecord> records;
    void on_record(const ym::MappedRecord& r) override { records.push_back(r); }
    void on_cycle(const ym::MeasurementCycle&) override {}
    void on_event(const ym::RadioEvent&) override {}
};

yantra::calibration::Entry practice_entry(const yantra::calibration::Key& key, double offset_db) {
    yantra::calibration::Entry e;
    e.key = key;
    e.created_unix = yantra::calibration::now_unix();
    e.simulated = true;
    e.method = yantra::calibration::kMethodPractice;
    for (double hz : {350e6, 480e6}) {
        yantra::calibration::Point p;
        p.frequency_hz = hz;
        p.offset_db = offset_db;
        e.points.push_back(p);
    }
    return e;
}

}  // namespace

// 6.2/6.3 known issue: replayed records had no radio schema (the pipeline
// refused them as unmapped) and no calibration key (dBFS levels stayed
// uncalibrated). The replay now hands both on (HW-01b, RF-02, TST-10).
TEST_CASE("record file: a replay is mapped and calibrated by the device's key or an explicit one (HW-01b, RF-02, TST-10)") {
    const auto p = ym::read_record_file(kSource / "plugins/tetra/testdata/tetra_sim.records.jsonl");
    INFO(all(p.errors));
    REQUIRE(p.ok());
    const ym::RecordFile& file = *p.value;
    CHECK(file.device_key == "simulator:0");
    CHECK(file.device_driver == "simulator");
    CHECK(file.device_serial == "0");
    const auto device_key = ym::device_calibration_key(file);
    REQUIRE(device_key != nullptr);
    CHECK(device_key->id() == yantra::calibration::Key{"simulator", "0", {}}.id());

    // The simulator's true offset at gain 0 is -20 dB (RAD-61): a practice
    // entry for the file's device.
    auto table = std::make_shared<yantra::calibration::Table>();
    table->put(practice_entry(*device_key, -20.0));
    auto run = [&](const ym::ReplayOptions& options) {
        ym::MeasurementPipeline::Options o;
        o.clock = std::make_shared<yantra::platform::ManualClock>(0);
        o.corrector = std::make_shared<yantra::calibration::TableCorrector>(table);
        ym::MeasurementPipeline pipeline(std::move(o));
        auto out = std::make_shared<Replayed>();
        pipeline.add_observer(out);
        REQUIRE(ym::replay(file, pipeline, options) == file.record_count);
        CHECK(pipeline.stats().unmapped == 0);
        CHECK(pipeline.stats().records == file.record_count);
        return out->records;
    };
    auto levels = [](const std::vector<ym::MappedRecord>& rs) {
        std::size_t n = 0;
        for (const ym::MappedRecord& r : rs) n += std::isfinite(r.radio_level) ? 1 : 0;
        return n;
    };

    SUBCASE("by default the header's device names the key") {
        const auto rs = run({});
        REQUIRE(levels(rs) > 10);
        for (const ym::MappedRecord& r : rs) {
            if (!std::isfinite(r.radio_level)) continue;
            CHECK(r.radio_dbfs);
            CHECK(r.correction.calibrated);
            CHECK(r.correction.key == device_key->id());
            CHECK((r.flags & ym::sample_flag::kUncalibrated) == 0);
            CHECK(r.level == doctest::Approx(r.radio_level - 20.0).epsilon(1e-12));
        }
    }
    SUBCASE("an explicit key wins: another key has no entry") {
        ym::ReplayOptions o;
        o.calibration_key = std::make_shared<const yantra::calibration::Key>(
            yantra::calibration::Key{"simulator", "0", {{"gain_db", "17"}}});
        for (const ym::MappedRecord& r : run(o)) {
            if (!std::isfinite(r.radio_level)) continue;
            CHECK_FALSE(r.correction.calibrated);
            CHECK((r.flags & ym::sample_flag::kUncalibrated) != 0);
        }
        table->put(practice_entry(*o.calibration_key, -37.0));
        for (const ym::MappedRecord& r : run(o)) {
            if (!std::isfinite(r.radio_level)) continue;
            CHECK(r.correction.calibrated);
            CHECK(r.level == doctest::Approx(r.radio_level - 37.0).epsilon(1e-12));
        }
    }
    SUBCASE("without a key the levels stay dBFS and uncalibrated") {
        ym::ReplayOptions o;
        o.key_from_device = false;
        const auto rs = run(o);
        REQUIRE(levels(rs) > 10);
        for (const ym::MappedRecord& r : rs) {
            if (!std::isfinite(r.radio_level)) continue;
            CHECK_FALSE(r.correction.calibrated);
            CHECK((r.flags & ym::sample_flag::kUncalibrated) != 0);
            CHECK(r.level == r.radio_level);
        }
    }
    SUBCASE("a header without a device gives no key") {
        const auto gen = ym::read_record_file(kSource / "plugins/tetra/testdata/tetra_gen.records.jsonl");
        REQUIRE(gen.ok());
        CHECK(gen.value->device_driver.empty());
        CHECK(ym::device_calibration_key(*gen.value) == nullptr);
    }
}

TEST_CASE("record file: records flagged level_dbm are dBm whatever the schema's unit; the others keep the dBFS path (radio API 1.1, HW-01b)") {
    // A dBFS schema (not SELF_CALIBRATED), as the simulator writes it in
    // either "Levels" mode: the record flag says which unit each level has.
    const auto p = parse(kHeader + "\n" +
                         R"({"type":"record","schema":"cell","time":{"monotonic_ns":1000},"flags":["simulated","level_dbm"],"values":{"frequency_hz":430025000,"role":1,"level":-61.5}})" "\n" +
                         R"({"type":"record","schema":"cell","time":{"monotonic_ns":2000},"flags":["simulated"],"values":{"frequency_hz":430025000,"role":1,"level":-41.5}})" "\n");
    INFO(all(p.errors));
    REQUIRE(p.ok());
    const ym::RecordFile& file = *p.value;
    REQUIRE(file.record_count == 2);

    ym::MeasurementPipeline::Options o;
    o.clock = std::make_shared<yantra::platform::ManualClock>(0);
    ym::MeasurementPipeline pipeline(std::move(o));
    auto out = std::make_shared<Replayed>();
    pipeline.add_observer(out);
    ym::ReplayOptions ro;
    ro.key_from_device = false;
    REQUIRE(ym::replay(file, pipeline, ro) == 2);
    REQUIRE(out->records.size() == 2);
    const ym::MappedRecord& dbm = out->records[0];
    CHECK_FALSE(dbm.radio_dbfs);
    CHECK(dbm.level == doctest::Approx(-61.5));
    CHECK((dbm.flags & ym::sample_flag::kUncalibrated) == 0);
    CHECK((dbm.flags & ym::sample_flag::kSimulated) != 0);
    const ym::MappedRecord& dbfs = out->records[1];
    CHECK(dbfs.radio_dbfs);
    CHECK((dbfs.flags & ym::sample_flag::kUncalibrated) != 0);
}
