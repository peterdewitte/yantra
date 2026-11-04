// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The common data model on the host side (PLG-05, PLG-08, PLG-12): name
// rules and the scope word check, schema upgrades, record checks with
// keys and NO_SYNC, provenance and how stored data opens, the licence
// rule (PLG-10) and the match of a technology schema with a radio schema.

#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include <yantra/core/plugin/descriptor.h>
#include <yantra/core/plugin/licence.h>
#include <yantra/core/plugin/record.h>
#include <yantra/radio.h>

namespace yp = yantra::plugin;

namespace {

const yp::Schema& cell_schema() {
    static const yp::Schema s = [] {
        yantra_host_api h{};
        h.struct_size = sizeof h;
        h.api_version = yantra_api_version_compiled();
        const auto r =
            yp::parse_descriptor(yantra_plugin_entry(&h), yp::host_api_version(), "ctest.so");
        REQUIRE(r.ok());
        return r.value->technology->schemas[0];
    }();
    return s;
}

struct CellRecord {
    yantra_value v[10];
    yantra_record r{};
    CellRecord() {
        v[0] = yantra_value_uint(430025000u);
        v[1] = yantra_value_uint(101);
        v[2] = yantra_value_enum(YR_ROLE_SERVING);
        v[3] = yantra_value_double(-70.5);
        v[4] = yantra_value_uint(901);
        v[5] = yantra_value_uint(1);
        v[6] = yantra_value_uint(7);
        v[7] = yantra_value_uint(3);
        v[8] = yantra_value_double(-105.0);
        v[9] = yantra_value_none();
        r.struct_size = sizeof r;
        r.value_count = 10;
        r.values = v;
    }
};

}  // namespace

TEST_CASE("names: identifiers and the scope words (PLG-08)") {
    CHECK(yp::is_identifier("level"));
    CHECK_FALSE(yp::is_identifier("Level"));
    CHECK_FALSE(yp::is_identifier("1st"));
    CHECK_FALSE(yp::is_identifier(std::string(65, 'a')));
    CHECK(yp::is_item_name("1"));
    for (const char* bad : {"issi", "nb_gssi", "sds_text", "voice", "iq_samples", "imsi", "pdu",
                            "cck_id", "subscriber_count", "tmsi"}) {
        INFO(bad);
        CHECK(yp::names_out_of_scope_data(bad));
    }
    for (const char* good : {"rssi", "encrypted_blocks", "mcc", "la", "colour_code", "ber",
                             "keyed", "messages_per_s_rate"}) {
        INFO(good);
        CHECK_FALSE(yp::names_out_of_scope_data(good));
    }
}

TEST_CASE("records: types, required fields, NO_SYNC keys, flags (PLG-05, PLG-08)") {
    const yp::Schema& s = cell_schema();
    CellRecord c;
    auto check = yp::parse_record(&c.r, s);
    INFO((check.parsed.errors.empty() ? std::string{} : check.parsed.errors[0]));
    REQUIRE(check.parsed.ok());
    const yp::Record& rec = *check.parsed.value;
    CHECK(std::get<double>(rec.values[3]) == -70.5);
    CHECK(std::holds_alternative<std::monostate>(rec.values[9]));
    CHECK(yp::record_key(rec, s) == "mcc=901;mnc=1;la=7");
    CHECK(yp::value_text(rec.values[2], s.fields[2]) == "Serving");

    // A level-only record: keys may be absent with NO_SYNC, not without.
    c.v[4] = c.v[5] = c.v[6] = yantra_value_none();
    CHECK_FALSE(yp::parse_record(&c.r, s).parsed.ok());
    c.r.flags = YANTRA_RECORD_NO_SYNC;
    CHECK(yp::parse_record(&c.r, s).parsed.ok());
    c.v[7] = yantra_value_none();  // colour_code is not OPTIONAL
    CHECK_FALSE(yp::parse_record(&c.r, s).parsed.ok());

    CellRecord t;
    t.r.flags = 0x100;
    CHECK_FALSE(yp::parse_record(&t.r, s).parsed.ok());
    t = CellRecord{};
    t.r.values = t.v;
    t.v[3] = yantra_value_uint(1);
    CHECK_FALSE(yp::parse_record(&t.r, s).parsed.ok());
    t.v[3] = yantra_value_double(NAN);
    CHECK_FALSE(yp::parse_record(&t.r, s).parsed.ok());
    t.v[3] = yantra_value_double(-70.0);
    t.v[2] = yantra_value_enum(9);
    CHECK_FALSE(yp::parse_record(&t.r, s).parsed.ok());
    t.v[2] = yantra_value_enum(1);
    t.r.value_count = 11;
    CHECK_FALSE(yp::parse_record(&t.r, s).parsed.ok());
    CHECK_FALSE(yp::parse_record(nullptr, s).parsed.ok());
}

TEST_CASE("records: strings are short UTF-8 values (PLG-08)") {
    yp::Schema s;
    s.id = "notes";
    s.label = "Notes";
    s.kind = yp::SchemaKind::Recording;
    yp::Field f;
    f.name = "site_label";
    f.label = "Site";
    f.type = yp::FieldType::String;
    s.fields.push_back(f);
    yantra_value v = yantra_value_string("Hall B");
    yantra_record r{};
    r.struct_size = sizeof r;
    r.value_count = 1;
    r.values = &v;
    CHECK(yp::parse_record(&r, s).parsed.ok());
    const std::string long_text(YANTRA_MAX_STRING_VALUE_BYTES + 1, 'x');
    v = yantra_value_string(long_text.c_str());
    const auto too_long = yp::parse_record(&r, s);
    CHECK_FALSE(too_long.parsed.ok());
    CHECK(too_long.scope_violation);
    v = yantra_value_string("\xC3\x28");
    const auto bad = yp::parse_record(&r, s);
    CHECK_FALSE(bad.parsed.ok());
    CHECK_FALSE(bad.scope_violation);
}

TEST_CASE("provenance and how stored data opens (PLG-12)") {
    yantra_provenance c{};
    c.struct_size = sizeof c;
    c.schema_version = 1;
    c.technology_id = "ctest";
    c.plugin_id = "org.example.ctest";
    c.plugin_version = "1.0.0";
    c.schema_id = "ctest_cell";
    const auto p = yp::parse_provenance(&c);
    REQUIRE(p.ok());
    CHECK(p.value->technology_id == "ctest");
    const yantra_provenance back = yp::to_c(*p.value);
    CHECK(std::string(back.schema_id) == "ctest_cell");
    c.schema_id = "";
    CHECK_FALSE(yp::parse_provenance(&c).ok());

    yp::Schema loaded = cell_schema();
    CHECK(yp::open_mode(*p.value, &loaded) == yp::OpenMode::Full);
    CHECK(yp::open_mode(*p.value, nullptr) == yp::OpenMode::ReadOnlyGeneric);
    yp::Provenance newer = *p.value;
    newer.schema_version = 2;
    CHECK(yp::open_mode(newer, &loaded) == yp::OpenMode::ReadOnlyGeneric);
}

TEST_CASE("schema upgrades keep stored data readable (PLG-05, PLG-12)") {
    const yp::Schema& a = cell_schema();
    yp::Schema b = a;
    CHECK(yp::check_schema_upgrade(a, b).empty());
    yp::Field extra;
    extra.name = "extra";
    extra.label = "Extra";
    extra.type = yp::FieldType::UInt;
    b.fields.push_back(extra);
    CHECK_FALSE(yp::check_schema_upgrade(a, b).empty());  // no version step
    b.version = 2;
    CHECK(yp::check_schema_upgrade(a, b).empty());
    b.fields.back().key = true;
    CHECK_FALSE(yp::check_schema_upgrade(a, b).empty());
    b = a;
    b.version = 2;
    b.fields[3].unit = "dB";
    CHECK_FALSE(yp::check_schema_upgrade(a, b).empty());
    b = a;
    b.version = 0;
    CHECK_FALSE(yp::check_schema_upgrade(a, b).empty());
}

TEST_CASE("licence rule: GPL-3.0-compatible SPDX expressions (PLG-10)") {
    for (const char* ok : {"GPL-3.0-or-later", "MIT", "gpl-2.0-or-later OR MIT",
                           "(Apache-2.0 AND BSD-3-Clause)", "LGPL-2.1-or-later"}) {
        INFO(ok);
        CHECK(yp::check_licence(ok).compatible);
    }
    for (const char* bad : {"", "GPL-2.0-only", "LicenseRef-Proprietary", "MIT AND CC-BY-NC-4.0",
                            "MIT OR", "(MIT"}) {
        INFO(bad);
        CHECK_FALSE(yp::check_licence(bad).compatible);
    }
    CHECK(std::string(yp::licence_notice()).find("GPL-3.0") != std::string::npos);
}

TEST_CASE("a technology schema matches the radio schema of the same id (PLG-05, RAD-21)") {
    const yp::Schema& tech = cell_schema();
    static const yr_enum_item roles[] = {
        {1, "serving", "Serving"}, {2, "neighbour", "Neighbour"}, {3, "other", "Other"}};
    auto def = [](const char* name, std::uint32_t type, const char* unit) {
        yr_field_def f{};
        f.struct_size = sizeof f;
        f.type = type;
        f.group = YR_GROUP_CORE;
        f.name = name;
        f.label = name;
        f.unit = unit;
        f.help = "x";
        return f;
    };
    yr_field_def fields[] = {def("frequency_hz", YR_TYPE_UINT, "Hz"),
                             def("channel", YR_TYPE_UINT, ""),
                             def("role", YR_TYPE_ENUM, ""),
                             def("level", YR_TYPE_DOUBLE, "dBFS"),
                             def("mcc", YR_TYPE_UINT, ""),
                             def("mnc", YR_TYPE_UINT, ""),
                             def("la", YR_TYPE_UINT, ""),
                             def("colour_code", YR_TYPE_UINT, "")};
    fields[2].enum_items = roles;
    fields[2].enum_count = 3;
    const yr_field_def* ptrs[8];
    for (int i = 0; i < 8; ++i) {
        ptrs[i] = &fields[i];
    }
    yr_schema radio{};
    radio.struct_size = sizeof radio;
    radio.kind = YR_SCHEMA_MEASUREMENT;
    radio.id = "ctest_cell";
    radio.label = "Cell";
    radio.version = 1;
    radio.field_count = 8;
    radio.fields = ptrs;
    {
        const auto problems = yp::check_radio_schema(tech, radio);
        INFO((problems.empty() ? std::string{} : problems[0]));
        CHECK(problems.empty());
    }
    fields[3].unit = "dBm";  // a calibrated radio
    CHECK(yp::check_radio_schema(tech, radio).empty());

    fields[1].type = YR_TYPE_INT;
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
    fields[1].type = YR_TYPE_UINT;
    fields[0].unit = "kHz";
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
    fields[0].unit = "Hz";
    radio.field_count = 7;  // colour_code not delivered, but not OPTIONAL
    CHECK(yp::check_radio_schema(tech, radio).size() == 1);
    radio.field_count = 8;
    fields[7].name = "extra_field";
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
    fields[7].name = "colour_code";
    static const yr_enum_item more_roles[] = {{4, "relay", "Relay"}};
    fields[2].enum_items = more_roles;
    fields[2].enum_count = 1;
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
    fields[2].enum_items = roles;
    fields[2].enum_count = 3;
    radio.kind = YR_SCHEMA_SCAN;
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
    radio.kind = YR_SCHEMA_MEASUREMENT;
    radio.id = "other";
    CHECK_FALSE(yp::check_radio_schema(tech, radio).empty());
}
