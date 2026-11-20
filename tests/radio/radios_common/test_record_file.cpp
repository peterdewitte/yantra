// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Record files (RAD-65): round trip, C ABI conversions, and the reader's
// refusals (other format, newer version, lines that do not fit the schema).
#include <doctest/doctest.h>

#include <yantra/radio.hpp>
#include <yantra/radios/record_file.hpp>

#include "fixture_descriptor.hpp"

#include <limits>
#include <sstream>
#include <string>

using namespace yantra::radios;

namespace {

Value u(uint64_t x)
{
    Value v;
    v.type = YR_TYPE_UINT;
    v.u64 = x;
    return v;
}
Value d(double x)
{
    Value v;
    v.type = YR_TYPE_DOUBLE;
    v.f64 = x;
    return v;
}
Value i(uint32_t type, int64_t x)
{
    Value v;
    v.type = type;
    v.i64 = x;
    return v;
}
Value s(const char* x)
{
    Value v;
    v.type = YR_TYPE_STRING;
    v.str = x;
    return v;
}

RecordFileHeader make_header()
{
    const yr_descriptor desc = fixture::make_descriptor();
    RecordFileHeader h = header_from_descriptor(desc);
    h.producer = "test 1.0";
    h.created_utc_ns = 1'790'000'000'000'000'000;
    h.device = DeviceInfo{"fake:0", "Fake device", "fake", "0001", "1.0", YR_DEVICE_SIMULATED};
    h.plan = PlanInfo{YR_PLAN_CARRIERS, 0, {392'012'500, 392'037'500}, 0, 0, 0, 0};
    return h;
}

Record serving_record(int64_t t)
{
    Record r;
    r.schema_index = 0;
    r.time = {t, 1'790'000'000'000'000'000 + t, YR_TIME_SOURCE_SYSTEM, 0};
    r.flags = YR_RECORD_SIMULATED;
    r.present = YR_GROUP_QUALITY | YR_GROUP_SIM_POSITION | YR_GROUP_OTHER;
    r.values = {u(392'012'500), u(3600),  i(YR_TYPE_ENUM, YR_ROLE_SERVING),
                d(-61.25),       u(901),   u(1),
                u(100),          u(1),     Value{} /* MER absent inside the group */,
                s("f1"),         d(12.5),  d(0.1 + 0.2),
                i(YR_TYPE_BOOL, 1), i(YR_TYPE_INT, -3)};
    return r;
}

std::string write_file(const RecordFileHeader& h, const std::vector<Record>& records)
{
    std::ostringstream out;
    RecordFileWriter w(out);
    w.write_header(h);
    w.write_event({YR_EVENT_SYNC_FOUND, 392'012'500, 5});
    for (const Record& r : records)
        w.write_record(r);
    w.write_end({w.records(), w.events(), 0});
    return out.str();
}

std::string read_error(const std::string& text)
{
    std::istringstream in(text);
    RecordFileReader reader(in);
    std::string error;
    if (!reader.read_header(error))
        return error;
    RecordFileLine line;
    for (;;) {
        const auto r = reader.next(line, error);
        if (r == RecordFileReader::Result::error)
            return error;
        if (r == RecordFileReader::Result::end_of_file)
            return {};
    }
}

std::string header_line(const std::string& file)
{
    return file.substr(0, file.find('\n') + 1);
}

} // namespace

TEST_CASE("record file: header, records, events and end survive a round trip")
{
    const RecordFileHeader h = make_header();
    Record nb;
    nb.schema_index = 1;
    nb.time = {20, 0, YR_TIME_SOURCE_NONE, 0};
    nb.present = YR_GROUP_NEIGHBOURS;
    nb.values = {u(392'012'500), u(0), u(1), u(17)};
    Record max_values = serving_record(30);
    max_values.values[0] = u(std::numeric_limits<uint64_t>::max());
    max_values.values[13] = i(YR_TYPE_INT, std::numeric_limits<int64_t>::min());
    max_values.values[10] = d(-1.0e-300);
    const std::vector<Record> records = {serving_record(10), nb, max_values};
    const std::string text = write_file(h, records);

    std::istringstream in(text);
    RecordFileReader reader(in);
    std::string error;
    REQUIRE(reader.read_header(error));
    CHECK(reader.header() == h);
    std::vector<RecordFileLine> lines;
    RecordFileLine line;
    for (;;) {
        const auto r = reader.next(line, error);
        REQUIRE(r != RecordFileReader::Result::error);
        if (r == RecordFileReader::Result::end_of_file)
            break;
        lines.push_back(line);
    }
    REQUIRE(lines.size() == 5);
    CHECK(lines[0].kind == RecordFileLine::Kind::event);
    CHECK(lines[0].event == Event{YR_EVENT_SYNC_FOUND, 392'012'500, 5});
    for (std::size_t k = 0; k < records.size(); ++k) {
        REQUIRE(lines[k + 1].kind == RecordFileLine::Kind::record);
        CHECK(lines[k + 1].record == records[k]);
    }
    CHECK(lines[4].kind == RecordFileLine::Kind::end);
    CHECK(lines[4].end == EndSummary{3, 1, 0});
    CHECK(reader.line_number() == 6);
}

TEST_CASE("record file: lines are readable JSON with fields by name")
{
    const std::string text = write_file(make_header(), {serving_record(10)});
    CHECK(text.rfind("{\"format\":\"yantra-radio-records\",\"version\":1,", 0) == 0);
    CHECK(text.find("\"values\":{\"frequency_hz\":392012500,\"channel\":3600,\"role\":1,"
                    "\"level\":-61.25,") != std::string::npos);
    CHECK(text.find("\"flags\":[\"simulated\"],\"groups\":[\"quality\",\"sim_position\","
                    "\"other\"]") != std::string::npos);
    const std::string record = text.substr(text.find("{\"type\":\"record\""));
    CHECK(record.find("\"mer_pct\"") == std::string::npos); // absent value: no member
    CHECK(text.find("{\"type\":\"event\",\"event\":\"sync_found\",\"value\":392012500,"
                    "\"monotonic_ns\":5}") != std::string::npos);
    CHECK(text.find("{\"type\":\"end\",\"records\":1,\"events\":1,\"dropped\":0}\n") !=
          std::string::npos);
}

TEST_CASE("record file: conversions from and to the C ABI")
{
    const yr_descriptor desc = fixture::make_descriptor();
    for (uint32_t k = 0; k < desc.schema_count; ++k)
        for (const std::string& problem :
             yantra::radio::validate_schema(*desc.schemas[k], desc.capabilities))
            FAIL_CHECK(problem);
    const RecordFileHeader h = header_from_descriptor(desc);
    REQUIRE(h.schemas.size() == 2);
    CHECK(h.plugin.id == "fixture");
    CHECK(h.plugin.capabilities == (YR_CAP_SIMULATED | YR_CAP_SCENE));
    CHECK(h.schemas[0].fields.size() == 14);
    CHECK(h.schemas[0].fields[2].items.size() == 3);
    CHECK(h.schemas[0].field_index("sim_x_m") == 10);
    CHECK(h.schemas[1].version == 2);

    // A stored record fed back as a yr_record is valid for the schema.
    const Record r = serving_record(10);
    RecordView view(r);
    CHECK(yantra::radio::validate_record(desc, view.record).empty());
    CHECK(view.record.value_count == 14);
    CHECK(std::string(view.values[9].u.str) == "f1");

    // ... and copied back gives the same record.
    CHECK(record_from_c(view.record, h.schemas[0]) == r);

    // Values of groups the record does not carry, and trailing values the
    // plugin left out, are absent in the copy.
    yr_record c = view.record;
    c.present = YR_GROUP_QUALITY;
    c.value_count = 12;
    const Record copy = record_from_c(c, h.schemas[0]);
    REQUIRE(copy.values.size() == 14);
    CHECK(copy.values[3] == d(-61.25));
    CHECK(copy.values[9].type == YR_TYPE_NONE);
    CHECK(copy.values[13].type == YR_TYPE_NONE);

    yr_plan plan{};
    plan.struct_size = sizeof plan;
    plan.type = YR_PLAN_SCAN;
    plan.scan_first_hz = 380'000'000;
    plan.scan_last_hz = 400'000'000;
    const PlanInfo p = plan_from_c(plan);
    CHECK(p.type == YR_PLAN_SCAN);
    CHECK(p.scan_last_hz == 400'000'000);
    CHECK(p.carriers_hz.empty());
}

TEST_CASE("record file: names of the API constants")
{
    CHECK(event_name(YR_EVENT_END_OF_STREAM) == "end_of_stream");
    CHECK(event_from_name("device_back") == YR_EVENT_DEVICE_BACK);
    CHECK_FALSE(event_from_name("nope").has_value());
    CHECK(event_name(999).empty());
    CHECK(type_name(YR_TYPE_ENUM) == "enum");
    CHECK(group_name(YR_GROUP_SIM_POSITION) == "sim_position");
    CHECK(schema_kind_name(YR_SCHEMA_NEIGHBOUR) == "neighbour");
    CHECK(plan_type_name(YR_PLAN_CARRIERS) == "carriers");
    CHECK(time_source_name(YR_TIME_SOURCE_DEVICE) == "device");
}

TEST_CASE("record file: the reader refuses other formats and newer versions")
{
    const std::string good = write_file(make_header(), {serving_record(10)});
    CHECK(read_error(good).empty());
    CHECK(read_error("").find("no header") != std::string::npos);
    CHECK(read_error("not json\n").find("not valid JSON") != std::string::npos);
    CHECK(read_error("{\"format\":\"yantra-recording\",\"version\":1}\n")
              .find("not a record file") != std::string::npos);

    std::string newer = good;
    newer.replace(newer.find("\"version\":1,"), 12, "\"version\":2,");
    CHECK(read_error(newer).find("version 2 is newer") != std::string::npos);
    std::string zero = good;
    zero.replace(zero.find("\"version\":1,"), 12, "\"version\":0,");
    CHECK(read_error(zero).find("invalid version") != std::string::npos);
}

TEST_CASE("record file: the reader checks every line against its schema")
{
    const std::string head = header_line(write_file(make_header(), {}));
    auto rec = [&](const std::string& body) {
        return read_error(head + "{\"type\":\"record\",\"schema\":\"cell\",\"time\":"
                                 "{\"monotonic_ns\":1}," +
                          body + "}\n");
    };
    CHECK(rec("\"values\":{\"frequency_hz\":1}").empty());
    CHECK(rec("\"values\":{\"nope\":1}").find("not in schema") != std::string::npos);
    CHECK(rec("\"values\":{\"frequency_hz\":-1}").find("unsigned") != std::string::npos);
    CHECK(rec("\"values\":{\"level\":\"x\"}").find("number") != std::string::npos);
    CHECK(rec("\"values\":{\"role\":7}").find("not an item") != std::string::npos);
    CHECK(rec("\"values\":{\"barred\":1}").find("group the record does not carry") !=
          std::string::npos);
    CHECK(rec("\"groups\":[\"other\"],\"values\":{\"barred\":1}").find("true or false") !=
          std::string::npos);
    CHECK(rec("\"groups\":[\"c1\"],\"values\":{}").find("not declared") != std::string::npos);
    CHECK(rec("\"groups\":[\"sim_position\"],\"values\":{\"sim_floor_id\":\"" +
              std::string(300, 'x') + "\"}")
              .find("longer than") != std::string::npos);
    CHECK(rec("\"values\":[]").find("values must be an object") != std::string::npos);
    CHECK(read_error(head + "{\"type\":\"record\",\"schema\":\"x\",\"time\":{\"monotonic_ns\":1},"
                            "\"values\":{}}\n")
              .find("not in the header") != std::string::npos);
    CHECK(read_error(head + "{\"type\":\"event\",\"event\":\"bang\",\"value\":0}\n")
              .find("unknown event") != std::string::npos);
    CHECK(read_error(head + "{\"type\":\"record\"\n").find("line 2: not valid JSON") !=
          std::string::npos);
}

TEST_CASE("record file: unknown members, line types and flag names are skipped")
{
    const std::string head = header_line(write_file(make_header(), {}));
    const std::string text =
        head + "\n{\"type\":\"marker\",\"text\":\"later version\"}\n" +
        "{\"type\":\"record\",\"schema\":\"cell\",\"extra\":1,\"time\":{\"monotonic_ns\":7},"
        "\"flags\":[\"simulated\",\"future_flag\"],\"values\":{\"frequency_hz\":5}}\n";
    std::istringstream in(text);
    RecordFileReader reader(in);
    std::string error;
    REQUIRE(reader.read_header(error));
    RecordFileLine line;
    REQUIRE(reader.next(line, error) == RecordFileReader::Result::line);
    CHECK(line.kind == RecordFileLine::Kind::record);
    CHECK(line.record.flags == YR_RECORD_SIMULATED);
    CHECK(line.record.values[0] == u(5));
    CHECK(reader.line_number() == 4);
    CHECK(reader.next(line, error) == RecordFileReader::Result::end_of_file);
}

TEST_CASE("record file: the writer refuses what the format cannot hold")
{
    std::ostringstream out;
    RecordFileWriter w(out);
    CHECK_THROWS_AS(w.write_record(serving_record(1)), std::logic_error);
    w.write_header(make_header());
    CHECK_THROWS_AS(w.write_header(make_header()), std::logic_error);
    Record r = serving_record(1);
    r.values[3] = d(std::numeric_limits<double>::quiet_NaN());
    CHECK_THROWS_AS(w.write_record(r), std::invalid_argument);
    r = serving_record(1);
    r.values[0] = d(1.0); // DOUBLE in a UINT field
    CHECK_THROWS_AS(w.write_record(r), std::invalid_argument);
    r = serving_record(1);
    r.schema_index = 5;
    CHECK_THROWS_AS(w.write_record(r), std::invalid_argument);
    CHECK_THROWS_AS(w.write_event({999, 0, 0}), std::invalid_argument);
    CHECK(w.records() == 0);
}

TEST_CASE("record file: the documented example file reads")
{
    RecordFileHeader h;
    std::vector<RecordFileLine> lines;
    std::string error;
    REQUIRE_MESSAGE(read_record_file(std::string(YANTRA_TEST_DATA_DIR) +
                                         "/record_files/example_v1.records.jsonl",
                                     h, lines, error),
                    error);
    CHECK(h.plugin.id == "example");
    CHECK(h.schemas.size() == 2);
    REQUIRE(lines.size() == 4);
    CHECK(lines.back().kind == RecordFileLine::Kind::end);
    CHECK_FALSE(read_record_file("/nonexistent/x.records.jsonl", h, lines, error));
    CHECK(error.find("cannot open") != std::string::npos);
}
