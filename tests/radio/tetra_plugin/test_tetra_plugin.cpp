// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plugins/tetra_common: plan checks (RAD-32), C1 with a known calibration
// (RAD-22), settings helpers (RAD-40..45) and the GSMTAP settings (RAD-115).
#include <doctest/doctest.h>

#include <yantra/plugins/tetra_plugin.hpp>

#include <cmath>
#include <cstring>

using namespace yantra;
using namespace yantra::plugins;

TEST_CASE("tetra plugin: the schema table matches the engine's schemas")
{
    const TetraSchemaTable t(radios::TetraSchemaOptions{false, true, false});
    receiver::tetra::EngineConfig cfg;
    cfg.simulated = true;
    const receiver::tetra::MeasurementEngine e(cfg);
    REQUIRE(t.count() == e.schemas().size());
    for (uint32_t i = 0; i < t.count(); ++i) {
        CHECK(std::string(t.schemas()[i]->id) == e.schemas()[i].id);
        CHECK(t.schemas()[i]->field_count == e.schemas()[i].fields.size());
    }
}

TEST_CASE("tetra plugin: plans are checked against the span (RAD-32)")
{
    yr_plan p{};
    p.struct_size = sizeof p;
    PlanSetup out;
    std::string why;
    p.type = YR_PLAN_SERVING;
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, false, out, why) == YR_OK);
    CHECK(out.search);
    CHECK_FALSE(out.serving_hz);
    p.serving_hz = 390'500'000;
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, false, out, why) == YR_OK);
    CHECK(*out.serving_hz == 390.5e6);
    CHECK(out.carriers == std::vector<double>{390.5e6});
    p.serving_hz = 392'000'000;
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, false, out, why) == YR_ERR_INVALID_ARGUMENT);
    CHECK(why.find("outside the span") != std::string::npos);
    p.type = YR_PLAN_CARRIERS;
    CHECK(plan_setup(p, {390'000'000, 390'025'000}, 390e6, 391e6, 4, false, out, why) == YR_OK);
    CHECK_FALSE(out.search);
    CHECK(out.carriers.size() == 2);
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, false, out, why) == YR_ERR_INVALID_ARGUMENT);
    CHECK(plan_setup(p, {390'000'000, 390'025'000}, 390e6, 391e6, 1, false, out, why) ==
          YR_ERR_INVALID_ARGUMENT);
    CHECK(plan_setup(p, {389'000'000}, 390e6, 391e6, 4, false, out, why) == YR_ERR_INVALID_ARGUMENT);
    p.type = YR_PLAN_SCAN;
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, false, out, why) == YR_ERR_UNSUPPORTED);
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, true, out, why) == YR_OK);
    receiver::tetra::EngineConfig cfg;
    apply_plan(out, cfg);
    CHECK(cfg.scan_records);
    p.type = 99;
    CHECK(plan_setup(p, {}, 390e6, 391e6, 4, true, out, why) == YR_ERR_INVALID_ARGUMENT);
}

TEST_CASE("tetra plugin: C1 from a record's level and broadcast (RAD-22, clause 23.3)")
{
    const radios::SchemaDef cell = radios::tetra_cell_schema({false, true, false});
    const radios::RecordFiller f(cell, 0);
    radios::Record r = f.start(yr_time{}, 0);
    f.set(r, YR_FIELD_LEVEL, radios::value_double(-50.0));
    CHECK_FALSE(set_c1(r, f, -20.0, 30.0)); // no broadcast yet
    f.set(r, "rxlev_access_min_dbm", radios::value_int(-110));
    f.set(r, "ms_txpwr_max_cell_dbm", radios::value_int(40));
    REQUIRE(set_c1(r, f, -20.0, 30.0));
    const int c1 = cell.field_index(YR_FIELD_C1_DB);
    // C1 = (-50 - 20) - (-110) - max(0, 40 - 30) = 30 dB.
    CHECK(r.values[static_cast<std::size_t>(c1)].f64 == doctest::Approx(30.0));
    REQUIRE(set_c1(r, f, -20.0, 40.0));
    CHECK(r.values[static_cast<std::size_t>(c1)].f64 == doctest::Approx(40.0));
    CHECK((r.present & YR_GROUP_C1) != 0);
    const yr_setting_def s = ms_max_power_setting("Receiver", 1);
    CHECK(radio::validate_setting_def(s).empty());
    CHECK(s.default_value.u.f64 == 30.0);
}

TEST_CASE("tetra plugin: settings helpers (RAD-40..45)")
{
    yr_setting_def d = make_setting("gain_db", YR_TYPE_DOUBLE, "Gain", "dB", "Gain", "help", 1);
    d.minimum = 0.0;
    d.maximum = 40.0;
    d.step = 0.5;
    d.default_value = yr_value_double(0.0);
    CHECK(radio::validate_setting_def(d).empty());
    CHECK(on_raster(d, 12.3) == 12.5);
    CHECK(on_raster(d, 99.0) == 40.0);
    CHECK(number_of(yr_value_uint(7)) == 7.0);
    CHECK(std::isnan(number_of(yr_value_none())));
    const yr_setting_def s = make_sensor("t", YR_TYPE_DOUBLE, "T", "", "G", "h", 1);
    CHECK(radio::validate_setting_def(s, true).empty());
    std::vector<yr_setting_def> defs = {d, s};
    CHECK(find_setting(defs, "t") == &defs[1]);
    CHECK(find_setting(defs, "x") == nullptr);
    char buf[4];
    yr_value v{};
    CHECK(string_value("abc", &v, buf, sizeof buf) == YR_OK);
    CHECK(std::strcmp(v.u.str, "abc") == 0);
    CHECK(string_value("abcd", &v, buf, sizeof buf) == YR_ERR_BUFFER_TOO_SMALL);
}

TEST_CASE("tetra plugin: GSMTAP settings (RAD-115)")
{
    std::vector<yr_setting_def> defs;
    add_gsmtap_settings(defs, "Receiver", 10);
    REQUIRE(defs.size() == 3);
    for (const yr_setting_def& d : defs) {
        CHECK(radio::validate_setting_def(d).empty());
        CHECK((d.flags & YR_SETTING_RESTART) != 0);
    }
    GsmtapSettings g;
    std::string warning;
    CHECK_FALSE(gsmtap_options(g, warning).enabled);
    REQUIRE(gsmtap_set(g, receiver::tetra::kGsmtapEnabledKey, yr_value_bool(1)));
    REQUIRE(gsmtap_set(g, receiver::tetra::kGsmtapPortKey, yr_value_uint(5000)));
    auto o = gsmtap_options(g, warning);
    CHECK(o.enabled);
    CHECK(o.port == 5000);
    CHECK(warning.empty());
    REQUIRE(gsmtap_set(g, receiver::tetra::kGsmtapHostKey, yr_value_string("not-an-address")));
    o = gsmtap_options(g, warning);
    CHECK_FALSE(o.enabled);
    CHECK_FALSE(warning.empty());
    char text[64];
    yr_value v{};
    REQUIRE(gsmtap_get(g, receiver::tetra::kGsmtapHostKey, &v, text, sizeof text));
    CHECK(std::string(v.u.str) == "not-an-address");
    CHECK_FALSE(gsmtap_get(g, "other", &v, text, sizeof text));
    CHECK_FALSE(gsmtap_set(g, "other", v));
}
