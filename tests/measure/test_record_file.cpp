// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record files of yantra-radios (RAD-65, TST-10) read by the core and fed
// into the measurement input, and the mapping of radio records onto a
// technology schema (PLG-05, OA-13): by field name, dBFS levels to dBm
// with the calibration offset, UNCALIBRATED without one.

#include <doctest/doctest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <variant>

#include <yantra/core/measure/record_file.h>
#include <yantra/core/measure/technology_map.h>
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
        const yp::Record r = map.value->map(rr.record, {true, -60.0});
        CHECK(std::get<double>(r.values[0]) == doctest::Approx(-100.0));
        CHECK((r.flags & YANTRA_RECORD_UNCALIBRATED) == 0);
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
