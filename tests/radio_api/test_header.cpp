// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the radio API header and its C++ helpers (RAD-10..12, RAD-16,
// RAD-17, RAD-19, RAD-21..23, RAD-27, RAD-40, RAD-41, RAD-48): version
// refusal, descriptor, schema and record validation, presence flags,
// settings checks and the text-buffer rules.
#include <doctest/doctest.h>
#include <yantra/radio.h>
#include <yantra/radio.hpp>

#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

extern "C" const char* yantra_test_radio_entry_symbol_c(void);
extern "C" const yr_descriptor* yantra_test_c_descriptor(void);
extern "C" int yantra_test_c_helpers(void);

namespace yr = yantra::radio;

namespace {

bool mentions(const std::vector<std::string>& problems, std::string_view what)
{
    for (const std::string& p : problems)
        if (p.find(what) != std::string::npos)
            return true;
    return false;
}

std::string joined(const std::vector<std::string>& problems)
{
    std::string s;
    for (const std::string& p : problems)
        s += p + "\n";
    return s;
}

// A record of the C descriptor's MEASUREMENT schema (index 0) with all
// core fields and the groups the test sets.
struct record_builder {
    std::array<yr_value, 11> values{};
    yr_record r{};
    record_builder()
    {
        values[YR_CORE_FREQUENCY_HZ] = yr_value_uint(392012500);
        values[YR_CORE_CHANNEL] = yr_value_uint(3601);
        values[YR_CORE_ROLE] = yr_value_enum(YR_ROLE_SERVING);
        values[YR_CORE_LEVEL] = yr_value_double(-62.5);
        values[YR_CORE_MCC] = yr_value_uint(901);
        values[YR_CORE_MNC] = yr_value_uint(1);
        values[YR_CORE_LA] = yr_value_uint(100);
        values[YR_CORE_COLOUR_CODE] = yr_value_uint(1);
        values[8] = yr_value_none(); // mer_pct (QUALITY)
        values[9] = yr_value_none(); // hyperframe (FRAME)
        values[10] = yr_value_none(); // c1_db (C1)
        r.struct_size = sizeof(yr_record);
        r.schema_index = 0;
        r.time.monotonic_ns = 1'000'000'000;
        r.time.utc_ns = 1'790'000'000'000'000'000;
        r.time.utc_source = YR_TIME_SOURCE_SYSTEM;
        r.value_count = static_cast<uint32_t>(values.size());
        r.values = values.data();
    }
};

} // namespace

TEST_CASE("radio.h names the plugin entry point in C and C++")
{
    CHECK(std::string_view(YANTRA_RADIO_ENTRY_SYMBOL) == "yantra_radio_entry");
    CHECK(std::string_view(yantra_test_radio_entry_symbol_c()) == YANTRA_RADIO_ENTRY_SYMBOL);
}

TEST_CASE("the SIMD switch is defined as 0 or 1 (RAD-90)")
{
    CHECK((YANTRA_SIMD == 0 || YANTRA_SIMD == 1));
}

TEST_CASE("the C helpers work from C (RAD-10)")
{
    CHECK(yantra_test_c_helpers() == 1);
}

TEST_CASE("version compatibility: equal major, plugin minor not newer (RAD-11)")
{
    const yr_api_version host{1, 3};
    CHECK(yr_api_compatible(host, {1, 0}));
    CHECK(yr_api_compatible(host, {1, 3}));
    CHECK_FALSE(yr_api_compatible(host, {1, 4}));
    CHECK_FALSE(yr_api_compatible(host, {2, 0}));
    CHECK_FALSE(yr_api_compatible(host, {0, 9}));
    const yr_api_version compiled = yr_api_version_compiled();
    CHECK(compiled.major == YR_API_VERSION_MAJOR);
    CHECK(compiled.minor == YR_API_VERSION_MINOR);
}

TEST_CASE("refusal_reason gives a clear message and reads only the frozen prefix (RAD-11)")
{
    const yr_descriptor* good = yantra_test_c_descriptor();
    CHECK(yr::refusal_reason(yr_api_version_compiled(), good).empty());
    CHECK(yr::refusal_reason({1, 0}, nullptr) == "the plugin returned no descriptor");

    // A descriptor of another major version may have any layout after the
    // prefix: only these 16 bytes exist here.
    struct {
        uint32_t struct_size;
        yr_api_version v;
        uint32_t caps;
    } prefix{16, {2, 0}, 0};
    const auto* other = reinterpret_cast<const yr_descriptor*>(&prefix);
    const std::string major = yr::refusal_reason({1, 0}, other);
    CHECK(major.find("radio API 2.0") != std::string::npos);
    CHECK(major.find("1.x") != std::string::npos);

    prefix.v = {1, 5};
    const std::string minor = yr::refusal_reason({1, 2}, other);
    CHECK(minor.find("1.5") != std::string::npos);
    CHECK(minor.find("newer") != std::string::npos);

    prefix.v = {1, 0};
    CHECK(yr::refusal_reason({1, 0}, other).find("shorter") != std::string::npos);
    prefix.struct_size = 8;
    CHECK(yr::refusal_reason({1, 0}, other).find("malformed") != std::string::npos);
}

TEST_CASE("result codes and their names (RAD-16)")
{
    CHECK(std::string_view(yr_status_name(YR_OK)) == "OK");
    CHECK(std::string_view(yr_status_name(YR_ERR_DEVICE_LOST)) == "DEVICE_LOST");
    CHECK(std::string_view(yr_status_name(YR_ERR_NO_PLAN)) == "NO_PLAN");
    CHECK(std::string_view(yr_status_name(YR_ERR_NO_POSITION)) == "NO_POSITION");
    CHECK(std::string_view(yr_status_name(42)) == "UNKNOWN");
    // Every code of RAD-16 is distinct and negative.
    const std::array<yr_status, 13> codes = {
        YR_ERR_FAILED, YR_ERR_INVALID_ARGUMENT, YR_ERR_UNSUPPORTED, YR_ERR_NOT_FOUND,
        YR_ERR_BUFFER_TOO_SMALL, YR_ERR_NO_DEVICE, YR_ERR_DEVICE_LOST, YR_ERR_BUSY,
        YR_ERR_VERSION, YR_ERR_NO_MEMORY, YR_ERR_IO, YR_ERR_NO_PLAN, YR_ERR_NO_POSITION};
    for (size_t i = 0; i < codes.size(); ++i) {
        CHECK(codes[i] < 0);
        CHECK(std::string_view(yr_status_name(codes[i])) != "UNKNOWN");
        for (size_t j = i + 1; j < codes.size(); ++j)
            CHECK(codes[i] != codes[j]);
    }
}

TEST_CASE("a descriptor written in C passes validation (RAD-10, RAD-12)")
{
    const auto problems = yr::validate_descriptor(yantra_test_c_descriptor());
    INFO(joined(problems));
    CHECK(problems.empty());
}

TEST_CASE("descriptor validation finds broken descriptors (RAD-12, RAD-13, RAD-48)")
{
    yr_descriptor d = *yantra_test_c_descriptor();
    SUBCASE("bad id") {
        d.id = "Bad Id";
        CHECK(mentions(yr::validate_descriptor(&d), "id must match"));
    }
    SUBCASE("licence not GPL-3.0-compatible") {
        d.licence = "LicenseRef-Proprietary";
        CHECK(mentions(yr::validate_descriptor(&d), "licence"));
    }
    SUBCASE("missing source url") {
        d.source_url = "";
        CHECK(mentions(yr::validate_descriptor(&d), "source_url"));
    }
    SUBCASE("unknown capability") {
        d.capabilities |= 1u << 20;
        CHECK(mentions(yr::validate_descriptor(&d), "capability"));
    }
    SUBCASE("SCAN schema without SCAN capability") {
        d.capabilities = 0;
        CHECK(mentions(yr::validate_descriptor(&d), "SCAN"));
    }
    SUBCASE("context callback without SCENE") {
        d.on_context_event = [](yr_plugin*, uint32_t) {};
        CHECK(mentions(yr::validate_descriptor(&d), "SCENE"));
        d.capabilities |= YR_CAP_SCENE;
        CHECK_FALSE(mentions(yr::validate_descriptor(&d), "SCENE"));
    }
    SUBCASE("required functions") {
        d.init = nullptr;
        CHECK(mentions(yr::validate_descriptor(&d), "init"));
    }
    SUBCASE("no schemas") {
        d.schema_count = 0;
        CHECK(mentions(yr::validate_descriptor(&d), "no record schemas"));
    }
    SUBCASE("translations need nl, fr or de") {
        const yr_translation bad[] = {{"setting.gain.label", "es", "Ganancia"}};
        d.translations = bad;
        d.translation_count = 1;
        CHECK(mentions(yr::validate_descriptor(&d), "translation 0"));
    }
    SUBCASE("SELF_CALIBRATED needs dBm levels") {
        d.capabilities |= YR_CAP_SELF_CALIBRATED;
        CHECK(mentions(yr::validate_descriptor(&d), "level unit must be dBm"));
    }
    SUBCASE("a newer minor version than this code is refused") {
        d.api_version.minor = YR_API_VERSION_MINOR + 1;
        CHECK(mentions(yr::validate_descriptor(&d), "newer"));
    }
}

TEST_CASE("names, keys, scope words and licences")
{
    CHECK(yr::valid_plugin_id("hackrf"));
    CHECK(yr::valid_plugin_id("0beacon.example-1_x"));
    CHECK_FALSE(yr::valid_plugin_id("-x"));
    CHECK_FALSE(yr::valid_plugin_id("HackRF"));
    CHECK(yr::valid_name("mer_pct"));
    CHECK_FALSE(yr::valid_name("1st"));
    CHECK_FALSE(yr::valid_name("mer.pct"));
    CHECK(yr::valid_key("gain.lna"));
    CHECK_FALSE(yr::valid_key(""));
    CHECK_FALSE(yr::valid_key("Gain"));

    // RAD-17: subscriber, message, speech, key and raw-signal words.
    for (const char* n : {"issi", "called_ssi", "sds_text", "voice_frames", "cipher_key", "cck",
                          "iq", "raw_pdu", "burst_bits"})
        CHECK_MESSAGE(yr::is_out_of_scope_name(n), n);
    for (const char* n : {"security_class", "encrypted_blocks", "transmission_mode", "slot",
                          "keyed", "mcc", "colour_code", "mer_pct", "sim_x_m"})
        CHECK_FALSE_MESSAGE(yr::is_out_of_scope_name(n), n);

    CHECK(yr::licence_is_gpl3_compatible("GPL-3.0-or-later"));
    CHECK(yr::licence_is_gpl3_compatible("GPL-3.0-or-later AND AGPL-3.0-only"));
    CHECK(yr::licence_is_gpl3_compatible("(GPL-3.0-or-later AND MIT) OR BSL-1.0"));
    CHECK_FALSE(yr::licence_is_gpl3_compatible(""));
    CHECK_FALSE(yr::licence_is_gpl3_compatible("GPL-2.0-only"));
    CHECK_FALSE(yr::licence_is_gpl3_compatible("GPL-3.0-or-later WITH Some-exception"));
}

TEST_CASE("schema validation: core layout, groups, scope (RAD-17, RAD-21, RAD-22)")
{
    const yr_schema& good = *yantra_test_c_descriptor()->schemas[0];
    CHECK(yr::validate_schema(good, 0).empty());

    std::vector<const yr_field_def*> fields(good.fields, good.fields + good.field_count);
    yr_schema s = good;
    s.fields = fields.data();

    SUBCASE("core fields in the wrong order") {
        std::swap(fields[YR_CORE_MCC], fields[YR_CORE_MNC]);
        CHECK(mentions(yr::validate_schema(s, 0), "core field 4 must be mcc"));
    }
    SUBCASE("too few core fields") {
        s.field_count = 4;
        CHECK(mentions(yr::validate_schema(s, 0), "fewer fields"));
    }
    SUBCASE("an optional group not declared") {
        s.groups = YR_GROUP_QUALITY | YR_GROUP_FRAME;
        CHECK(mentions(yr::validate_schema(s, 0), "field c1_db: group"));
    }
    SUBCASE("a field outside the scope boundary") {
        yr_field_def bad = *fields[8];
        bad.name = "called_ssi";
        fields[8] = &bad;
        CHECK(mentions(yr::validate_schema(s, 0), "scope boundary"));
    }
    SUBCASE("duplicate names") {
        fields[9] = fields[8];
        CHECK(mentions(yr::validate_schema(s, 0), "duplicate name"));
    }
    SUBCASE("a standard name with another type") {
        yr_field_def bad = *fields[10];
        bad.type = YR_TYPE_INT;
        fields[10] = &bad;
        CHECK(mentions(yr::validate_schema(s, 0), "standard field"));
    }
    SUBCASE("help text required") {
        yr_field_def bad = *fields[8];
        bad.help = "";
        fields[8] = &bad;
        CHECK(mentions(yr::validate_schema(s, 0), "help text"));
    }
    SUBCASE("sim_position needs SIMULATED") {
        s.groups |= YR_GROUP_SIM_POSITION;
        CHECK(mentions(yr::validate_schema(s, 0), "sim_position"));
        CHECK_FALSE(mentions(yr::validate_schema(s, YR_CAP_SIMULATED), "sim_position"));
    }
    SUBCASE("neighbour schema layout") {
        const yr_schema& n = *yantra_test_c_descriptor()->schemas[2];
        CHECK(yr::validate_schema(n, 0).empty());
        yr_schema m = n;
        m.groups |= YR_GROUP_QUALITY;
        CHECK(mentions(yr::validate_schema(m, 0), "only NEIGHBOURS"));
    }
}

TEST_CASE("record validation: core fields and presence flags (RAD-21, RAD-22, RAD-27)")
{
    const yr_descriptor& d = *yantra_test_c_descriptor();
    record_builder b;
    CHECK(yr::validate_record(d, b.r).empty());

    SUBCASE("a present group carries values") {
        b.r.present = YR_GROUP_QUALITY | YR_GROUP_FRAME;
        b.values[8] = yr_value_double(1.5);
        b.values[9] = yr_value_uint(1234);
        CHECK(yr::validate_record(d, b.r).empty());
    }
    SUBCASE("a value in a group that is not present") {
        b.values[8] = yr_value_double(1.5);
        CHECK(mentions(yr::validate_record(d, b.r), "group is not present"));
    }
    SUBCASE("a present group may hold an absent value") {
        b.r.present = YR_GROUP_C1;
        CHECK(yr::validate_record(d, b.r).empty());
    }
    SUBCASE("present names a group the schema does not declare") {
        b.r.present = YR_GROUP_BROADCAST;
        CHECK(mentions(yr::validate_record(d, b.r), "does not declare"));
    }
    SUBCASE("identity missing without NO_SYNC") {
        b.values[YR_CORE_MCC] = yr_value_none();
        CHECK(mentions(yr::validate_record(d, b.r), "core field mcc"));
        b.r.flags = YR_RECORD_NO_SYNC;
        CHECK(yr::validate_record(d, b.r).empty());
    }
    SUBCASE("level is required even with NO_SYNC") {
        b.r.flags = YR_RECORD_NO_SYNC;
        b.values[YR_CORE_LEVEL] = yr_value_none();
        CHECK(mentions(yr::validate_record(d, b.r), "core field level"));
    }
    SUBCASE("trailing values may be left out only for optional fields") {
        b.r.value_count = YR_CORE_FIELD_COUNT;
        CHECK(yr::validate_record(d, b.r).empty());
        b.r.value_count = 3;
        CHECK(mentions(yr::validate_record(d, b.r), "core field level"));
    }
    SUBCASE("wrong value type, non-finite double, unknown enum") {
        b.values[YR_CORE_FREQUENCY_HZ] = yr_value_int(392012500);
        b.values[YR_CORE_LEVEL] = yr_value_double(std::nan(""));
        b.values[YR_CORE_ROLE] = yr_value_enum(7);
        const auto p = yr::validate_record(d, b.r);
        CHECK(mentions(p, "field frequency_hz: type"));
        CHECK(mentions(p, "finite"));
        CHECK(mentions(p, "not an item"));
    }
    SUBCASE("time fields (RAD-27)") {
        b.r.time.monotonic_ns = 0;
        CHECK(mentions(yr::validate_record(d, b.r), "monotonic_ns"));
        b.r.time.monotonic_ns = 5;
        b.r.time.utc_ns = 0;
        CHECK(mentions(yr::validate_record(d, b.r), "utc_ns"));
        b.r.time.utc_source = YR_TIME_SOURCE_NONE;
        CHECK(yr::validate_record(d, b.r).empty());
        b.r.time.utc_source = 9;
        CHECK(mentions(yr::validate_record(d, b.r), "utc_source"));
    }
    SUBCASE("unknown schema index and too many values") {
        b.r.schema_index = 7;
        CHECK(mentions(yr::validate_record(d, b.r), "not declared"));
        b.r.schema_index = 0;
        b.r.value_count = 12;
        CHECK(mentions(yr::validate_record(d, b.r), "more values"));
    }
    SUBCASE("SIMULATED flag follows the capability (RAD-19, RAD-21)") {
        b.r.flags = YR_RECORD_SIMULATED;
        CHECK(mentions(yr::validate_record(d, b.r), "SIMULATED"));
    }
    SUBCASE("scan records have role OTHER (RAD-23)") {
        b.r.schema_index = 1;
        b.r.value_count = YR_CORE_FIELD_COUNT;
        CHECK(mentions(yr::validate_record(d, b.r), "role OTHER"));
        b.values[YR_CORE_ROLE] = yr_value_enum(YR_ROLE_OTHER);
        CHECK(yr::validate_record(d, b.r).empty());
    }
    SUBCASE("long strings are refused (RAD-17)") {
        // A STRING field in the record: reuse the neighbour schema's
        // layout check through a simulated plugin with sim_floor_id.
        static const yr_field_def floor = {sizeof(yr_field_def), YR_TYPE_STRING,
                                           YR_GROUP_SIM_POSITION, 0, YR_FIELD_SIM_FLOOR_ID,
                                           "Floor", "", "Simulated floor.", nullptr, 0, 0};
        std::array<const yr_field_def*, YR_CORE_FIELD_COUNT + 1> fields{};
        for (uint32_t i = 0; i < YR_CORE_FIELD_COUNT; ++i)
            fields[i] = d.schemas[0]->fields[i];
        fields[YR_CORE_FIELD_COUNT] = &floor;
        yr_schema s = *d.schemas[0];
        s.fields = fields.data();
        s.field_count = static_cast<uint32_t>(fields.size());
        s.groups = YR_GROUP_SIM_POSITION;
        const yr_schema* list[] = {&s};
        yr_descriptor sim = d;
        sim.capabilities = YR_CAP_SIMULATED;
        sim.schemas = list;
        sim.schema_count = 1;
        CHECK(yr::validate_schema(s, sim.capabilities).empty());

        const std::string long_text(YR_MAX_STRING_VALUE_BYTES + 1, 'x');
        b.r.flags = YR_RECORD_SIMULATED;
        b.r.present = YR_GROUP_SIM_POSITION;
        b.r.value_count = YR_CORE_FIELD_COUNT + 1;
        b.values[YR_CORE_FIELD_COUNT] = yr_value_string("f1");
        CHECK(yr::validate_record(sim, b.r).empty());
        b.values[YR_CORE_FIELD_COUNT] = yr_value_string(long_text.c_str());
        CHECK(mentions(yr::validate_record(sim, b.r), "longer than"));
    }
}

TEST_CASE("neighbour records (RAD-22, DEC-02)")
{
    const yr_descriptor& d = *yantra_test_c_descriptor();
    std::array<yr_value, 4> v = {yr_value_uint(392012500), yr_value_uint(0), yr_value_uint(2),
                                 yr_value_none()};
    yr_record r{};
    r.struct_size = sizeof r;
    r.schema_index = 2;
    r.time.monotonic_ns = 1;
    r.value_count = 4;
    r.values = v.data();
    CHECK(mentions(yr::validate_record(d, r), "group is not present") == false);
    r.present = YR_GROUP_NEIGHBOURS;
    v[3] = yr_value_uint(17);
    CHECK(yr::validate_record(d, r).empty());
    v[1] = yr_value_uint(2);
    CHECK(mentions(yr::validate_record(d, r), "entry_index"));
}

TEST_CASE("setting definitions and requested values (RAD-40, RAD-41, RAD-42, RAD-45)")
{
    static const yr_enum_item ports[] = {{0, "rx", "RX"}, {1, "tx_rx", "TX/RX"}};
    yr_setting_def gain{};
    gain.struct_size = sizeof gain;
    gain.type = YR_TYPE_DOUBLE;
    gain.flags = YR_SETTING_CALIBRATION;
    gain.key = "gain.lna";
    gain.label = "LNA gain";
    gain.unit = "dB";
    gain.help = "Gain of the low-noise amplifier.";
    gain.group = "Gain";
    gain.minimum = 0;
    gain.maximum = 40;
    gain.step = 8;
    gain.default_value = yr_value_double(16);
    CHECK(yr::validate_setting_def(gain).empty());

    std::string why;
    CHECK(yr::check_setting_value(gain, yr_value_double(24), &why) == YR_OK);
    CHECK(why.empty());
    CHECK(yr::check_setting_value(gain, yr_value_double(41), &why) == YR_ERR_INVALID_ARGUMENT);
    CHECK(why == "LNA gain must be at least 0 and at most 40 dB");
    CHECK(yr::check_setting_value(gain, yr_value_int(8), &why) == YR_ERR_INVALID_ARGUMENT);
    CHECK(why.find("type") != std::string::npos);

    yr_setting_def port = gain;
    port.type = YR_TYPE_ENUM;
    port.key = "antenna";
    port.items = ports;
    port.item_count = 2;
    port.default_value = yr_value_enum(0);
    CHECK(yr::validate_setting_def(port).empty());
    CHECK(yr::check_setting_value(port, yr_value_enum(1)) == YR_OK);
    CHECK(yr::check_setting_value(port, yr_value_enum(2)) == YR_ERR_INVALID_ARGUMENT);

    yr_setting_def action = gain;
    action.type = YR_TYPE_ACTION;
    action.key = "reset";
    action.default_value = yr_value_none();
    CHECK(yr::validate_setting_def(action).empty());
    CHECK(yr::check_setting_value(action, yr_value_none()) == YR_ERR_INVALID_ARGUMENT);
    CHECK(mentions(yr::validate_setting_def(action, true), "invalid type"));

    SUBCASE("flags need their texts") {
        yr_setting_def s = gain;
        s.flags |= YR_SETTING_UNAVAILABLE | YR_SETTING_WARNING;
        const auto p = yr::validate_setting_def(s);
        CHECK(mentions(p, "unavailable_reason"));
        CHECK(mentions(p, "warning text"));
        s.unavailable_reason = "the module does not offer this";
        s.warning = "Feeds power to the antenna port.";
        CHECK(yr::validate_setting_def(s).empty());
        CHECK(yr::check_setting_value(s, yr_value_double(8), &why) == YR_ERR_INVALID_ARGUMENT);
        CHECK(why.find("not available") != std::string::npos);
    }
    SUBCASE("default out of range, bad key, min above max") {
        yr_setting_def s = gain;
        s.default_value = yr_value_double(50);
        CHECK(mentions(yr::validate_setting_def(s), "out of range"));
        s.key = "Gain LNA";
        CHECK(mentions(yr::validate_setting_def(s), "key must match"));
        s.minimum = 50;
        s.maximum = 10;
        CHECK(mentions(yr::validate_setting_def(s), "minimum above maximum"));
    }
    SUBCASE("sensors are read-only without default") {
        yr_setting_def s = gain;
        s.default_value = yr_value_none();
        CHECK(mentions(yr::validate_setting_def(s, true), "READ_ONLY"));
        s.flags = YR_SETTING_READ_ONLY;
        CHECK(yr::validate_setting_def(s, true).empty());
        CHECK(yr::check_setting_value(s, yr_value_double(1)) == YR_ERR_INVALID_ARGUMENT);
    }
}

TEST_CASE("copy_text follows the buffer rules (RAD-10)")
{
    char buf[8];
    uint32_t len = 0;
    CHECK(yr::copy_text("abc", buf, sizeof buf, &len) == YR_OK);
    CHECK(std::string_view(buf) == "abc");
    CHECK(len == 3);
    CHECK(yr::copy_text("1234567", buf, sizeof buf, &len) == YR_OK);
    CHECK(yr::copy_text("12345678", buf, sizeof buf, &len) == YR_ERR_BUFFER_TOO_SMALL);
    CHECK(len == 8);
    CHECK(std::string_view(buf) == "1234567");
    CHECK(yr::copy_text("x", nullptr, 0, &len) == YR_ERR_BUFFER_TOO_SMALL);
    CHECK(len == 1);
    CHECK(yr::copy_text("", buf, 1) == YR_OK);
    CHECK(buf[0] == '\0');
}

TEST_CASE("YR_HAS_FIELD judges versioned structs by struct_size (RAD-10)")
{
    yr_host_api host{};
    host.struct_size = static_cast<uint32_t>(offsetof(yr_host_api, get_floor_plan));
    CHECK(YR_HAS_FIELD(&host, yr_host_api, settings_changed));
    CHECK_FALSE(YR_HAS_FIELD(&host, yr_host_api, get_floor_plan));
    host.struct_size = sizeof host;
    CHECK(YR_HAS_FIELD(&host, yr_host_api, get_scene));
}

TEST_CASE("field_index finds fields by name")
{
    const yr_schema& s = *yantra_test_c_descriptor()->schemas[0];
    CHECK(yr::field_index(s, YR_FIELD_LEVEL) == static_cast<int>(YR_CORE_LEVEL));
    CHECK(yr::field_index(s, YR_FIELD_C1_DB) == 10);
    CHECK(yr::field_index(s, "nothing") == -1);
}
