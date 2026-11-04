// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the public plugin header (PLG-02..04, PLG-07): C and C++ agree
// on the layout and the version rule, the value helpers and
// YANTRA_HAS_FIELD work, the plugin.hpp helpers behave by the buffer and
// refusal rules, and the plugin written in C (header_c.c) works when
// called the way Yantra calls it.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "header_c.h"
#include <yantra/plugin.h>
#include <yantra/plugin.hpp>

namespace pa = yantra::plugin_api;

namespace {

// A minimal host table that records log lines.
struct FakeHost {
    std::vector<std::string> log;
};

void fake_log(yantra_host* host, uint32_t /*level*/, const char* message)
{
    reinterpret_cast<FakeHost*>(host)->log.emplace_back(message);
}

yantra_host_api make_host(FakeHost& host)
{
    yantra_host_api api{};
    api.struct_size = sizeof api;
    api.api_version = yantra_api_version_compiled();
    api.host = reinterpret_cast<yantra_host*>(&host);
    api.log = &fake_log;
    return api;
}

// A recording UI drawing table: notes the calls, clicks every button.
struct FakeUi {
    std::vector<std::string> calls;
};
FakeUi* ui_of(yantra_ui* ui)
{
    return reinterpret_cast<FakeUi*>(ui);
}
void ui_heading(yantra_ui* ui, const char* text, const char*)
{
    ui_of(ui)->calls.push_back(std::string("heading:") + text);
}
void ui_value(yantra_ui* ui, const yantra_ui_control* c, const char* value, const char*)
{
    ui_of(ui)->calls.push_back(std::string("value:") + c->id + "=" + value);
}
int ui_button(yantra_ui* ui, const yantra_ui_control* c)
{
    ui_of(ui)->calls.push_back(std::string("button:") + c->id);
    return 1;
}

yantra_value cell_values[10];
yantra_record make_cell(double rx_level_min)
{
    cell_values[0] = yantra_value_uint(430025000u);
    cell_values[1] = yantra_value_uint(101);
    cell_values[2] = yantra_value_enum(1);
    cell_values[3] = yantra_value_double(-70.0);
    cell_values[4] = yantra_value_uint(901);
    cell_values[5] = yantra_value_uint(1);
    cell_values[6] = yantra_value_uint(7);
    cell_values[7] = yantra_value_uint(3);
    cell_values[8] = std::isnan(rx_level_min) ? yantra_value_none()
                                              : yantra_value_double(rx_level_min);
    cell_values[9] = yantra_value_none();
    yantra_record r{};
    r.struct_size = sizeof r;
    r.schema_index = 0;
    r.value_count = 10;
    r.values = cell_values;
    return r;
}

} // namespace

TEST_CASE("C and C++ see the same struct sizes (PLG-03)")
{
    CHECK(yantra_test_c_sizeof(0) == sizeof(yantra_plugin_descriptor));
    CHECK(yantra_test_c_sizeof(1) == sizeof(yantra_host_api));
    CHECK(yantra_test_c_sizeof(2) == sizeof(yantra_value));
    CHECK(yantra_test_c_sizeof(3) == sizeof(yantra_record));
    CHECK(yantra_test_c_sizeof(4) == sizeof(yantra_technology_api));
    CHECK(yantra_test_c_sizeof(5) == sizeof(yantra_ui_api));
    CHECK(yantra_test_c_sizeof(6) == sizeof(yantra_reselection_result));
}

TEST_CASE("API version: macros, compiled version and compatibility rule (PLG-04)")
{
    const yantra_api_version v = yantra_api_version_compiled();
    CHECK(v.major == YANTRA_PLUGIN_API_VERSION_MAJOR);
    CHECK(v.minor == YANTRA_PLUGIN_API_VERSION_MINOR);
    CHECK(v.major == 1);
    CHECK(std::string_view{YANTRA_PLUGIN_ENTRY_SYMBOL} == "yantra_plugin_entry");

    const yantra_api_version host{1, 3};
    CHECK(yantra_api_compatible(host, {1, 0}));
    CHECK(yantra_api_compatible(host, {1, 3}));
    CHECK_FALSE(yantra_api_compatible(host, {1, 4}));
    CHECK_FALSE(yantra_api_compatible(host, {0, 3}));
    CHECK_FALSE(yantra_api_compatible(host, {2, 0}));
    CHECK(yantra_test_c_compatible(1, 3, 1, 0) == 1);
    CHECK(yantra_test_c_compatible(1, 3, 1, 4) == 0);
    CHECK(yantra_test_c_compatible(1, 3, 2, 0) == 0);
}

TEST_CASE("value helpers set type and payload")
{
    CHECK(yantra_value_none().type == YANTRA_TYPE_NONE);
    CHECK(yantra_value_bool(7).u.i64 == 1);
    CHECK(yantra_value_int(-3).u.i64 == -3);
    CHECK(yantra_value_uint(42u).u.u64 == 42u);
    CHECK(yantra_value_double(1.5).u.f64 == 1.5);
    CHECK(std::string_view{yantra_value_string("x").u.str} == "x");
    const yantra_value e = yantra_value_enum(2);
    CHECK(e.type == YANTRA_TYPE_ENUM);
    CHECK(e.u.i64 == 2);
    CHECK(e.reserved == 0);
}

TEST_CASE("YANTRA_HAS_FIELD judges by struct_size (PLG-03)")
{
    yantra_host_api h{};
    h.struct_size = static_cast<uint32_t>(offsetof(yantra_host_api, timer_start));
    CHECK(YANTRA_HAS_FIELD(&h, yantra_host_api, now));
    CHECK_FALSE(YANTRA_HAS_FIELD(&h, yantra_host_api, timer_start));
    h.struct_size = sizeof h;
    CHECK(YANTRA_HAS_FIELD(&h, yantra_host_api, ui_close_window));
}

TEST_CASE("plugin.hpp: refusal reasons read the frozen prefix only (PLG-04)")
{
    const yantra_api_version host{1, 2};
    CHECK(pa::refusal_reason(host, nullptr) == "the plugin returned no descriptor");

    yantra_plugin_descriptor d{};
    d.struct_size = 8;
    CHECK(pa::refusal_reason(host, &d).find("malformed") != std::string::npos);

    d.struct_size = sizeof d;
    d.api_version = {2, 0};
    const std::string major = pa::refusal_reason(host, &d);
    CHECK(major.find("plugin API 2.0") != std::string::npos);
    CHECK(major.find("1.x") != std::string::npos);

    d.api_version = {1, 3};
    CHECK(pa::refusal_reason(host, &d).find("newer than this Yantra's 1.2") != std::string::npos);

    d.api_version = {1, 1};
    CHECK(pa::refusal_reason(host, &d).empty());

    d.struct_size = static_cast<uint32_t>(offsetof(yantra_plugin_descriptor, ui));
    CHECK(pa::refusal_reason(host, &d).find("shorter") != std::string::npos);

    // A major-2 descriptor may be any size beyond the prefix: only the
    // prefix is read before the refusal.
    unsigned char raw[16] = {};
    const uint32_t size = 16;
    const yantra_api_version v2{2, 0};
    std::memcpy(raw, &size, 4);
    std::memcpy(raw + 4, &v2, 8);
    CHECK(pa::refusal_reason(host, reinterpret_cast<const yantra_plugin_descriptor*>(raw))
              .find("2.0") != std::string::npos);
}

TEST_CASE("plugin.hpp: copy_text follows the buffer rules (PLG-03)")
{
    char buf[8];
    uint32_t len = 0;
    CHECK(pa::copy_text("abc", buf, sizeof buf, &len) == YANTRA_OK);
    CHECK(std::string_view{buf} == "abc");
    CHECK(len == 3);
    CHECK(pa::copy_text("1234567", buf, sizeof buf, &len) == YANTRA_OK);
    CHECK(pa::copy_text("12345678", buf, sizeof buf, &len) == YANTRA_ERR_BUFFER_TOO_SMALL);
    CHECK(std::string_view{buf} == "1234567");
    CHECK(len == 8);
    CHECK(pa::copy_text("x", nullptr, 0, &len) == YANTRA_ERR_BUFFER_TOO_SMALL);
    CHECK(len == 1);
}

TEST_CASE("plugin.hpp: guarded keeps exceptions inside the plugin (PLG-03)")
{
    CHECK(pa::guarded([] { return YANTRA_OK; }) == YANTRA_OK);
    CHECK(pa::guarded([]() -> yantra_status { throw std::bad_alloc(); }) == YANTRA_ERR_NO_MEMORY);
    CHECK(pa::guarded([]() -> yantra_status { throw std::runtime_error("x"); }) ==
          YANTRA_ERR_FAILED);
    CHECK(std::string_view{pa::status_name(YANTRA_ERR_SCOPE)} == "outside the scope boundary");
    CHECK(std::string_view{pa::status_name(12345)} == "unknown status");
    CHECK(pa::version_text({1, 0}) == "1.0");
}

TEST_CASE("the C plugin: entry point exchanges the tables without side effects")
{
    FakeHost fake;
    yantra_host_api host = make_host(fake);
    const int before = yantra_test_c_entry_count();
    const yantra_plugin_descriptor* d = yantra_plugin_entry(&host);
    REQUIRE(d != nullptr);
    CHECK(yantra_test_c_entry_count() == before + 1);
    CHECK(fake.log.empty());
    CHECK(pa::refusal_reason(yantra_api_version_compiled(), d).empty());
    CHECK(d->kinds == (YANTRA_KIND_TECHNOLOGY | YANTRA_KIND_FEATURE | YANTRA_KIND_EXPORTER));
    CHECK(std::string_view{d->licence} == "GPL-3.0-or-later");

    // A host of another major version gets NULL (the plugin refuses it).
    yantra_host_api other = host;
    other.api_version = {2, 0};
    CHECK(yantra_plugin_entry(&other) == nullptr);
}

TEST_CASE("the C plugin: init, technology rules, feature, exporter, UI, shutdown")
{
    FakeHost fake;
    yantra_host_api host = make_host(fake);
    const yantra_plugin_descriptor* d = yantra_plugin_entry(&host);
    REQUIRE(d != nullptr);

    // A host table older than API 1.0 is refused by init.
    yantra_host_api short_host = host;
    short_host.struct_size = static_cast<uint32_t>(offsetof(yantra_host_api, emit_record));
    yantra_plugin_instance* instance = nullptr;
    CHECK(d->init(&short_host, &instance) == YANTRA_ERR_VERSION);

    REQUIRE(d->init(&host, &instance) == YANTRA_OK);
    REQUIRE(instance != nullptr);
    REQUIRE(fake.log.size() == 1);
    CHECK(fake.log[0] == "ctest plugin ready");

    // C1 (RF-06): level minus the broadcast minimum RX level.
    const yantra_technology_api* t = d->technology;
    yantra_record cell = make_cell(-100.0);
    yantra_c1_input in{};
    in.struct_size = sizeof in;
    in.record = &cell;
    in.level_dbm = -70.0;
    in.ms_power_dbm = 30.0;
    double c1 = 0.0;
    REQUIRE(t->compute_c1(instance, &in, &c1) == YANTRA_OK);
    CHECK(c1 == doctest::Approx(30.0));
    yantra_record no_param = make_cell(NAN);
    in.record = &no_param;
    CHECK(t->compute_c1(instance, &in, &c1) == YANTRA_ERR_UNSUPPORTED);

    // Reselection (DB-08): a neighbour 5 dB better is chosen.
    yantra_record cell2 = make_cell(-100.0);
    yantra_value values2[10];
    std::memcpy(values2, cell_values, sizeof values2);
    cell2.values = values2;
    yantra_reselection_cell serving{static_cast<uint32_t>(sizeof serving), 0, &cell, -80.0};
    yantra_reselection_cell nb{static_cast<uint32_t>(sizeof nb), 0, &cell2, -75.0};
    const yantra_reselection_cell* nbs[] = {&nb};
    yantra_reselection_input rin{static_cast<uint32_t>(sizeof rin), 1, &serving, nbs, 30.0};
    yantra_reselection_result out{};
    out.struct_size = sizeof out;
    REQUIRE(t->reselect(instance, &rin, &out) == YANTRA_OK);
    CHECK(out.chosen == 0);
    CHECK(out.serving_state == 1);
    CHECK(std::string_view{out.reason} == "a neighbour is 3 dB better");
    nb.level_dbm = -79.0;
    REQUIRE(t->reselect(instance, &rin, &out) == YANTRA_OK);
    CHECK(out.chosen == -1);

    // Feature: on_record counts; the UI panel shows the count.
    yantra_provenance prov{static_cast<uint32_t>(sizeof prov), 1, "ctest", "org.example.radio", "0.1.0", "ctest_cell"};
    d->feature->on_record(instance, &prov, t->schemas[0], &cell);
    d->feature->on_record(instance, &prov, t->schemas[0], &cell);

    FakeUi fui;
    yantra_ui_api ui{};
    ui.struct_size = sizeof ui;
    ui.ui = reinterpret_cast<yantra_ui*>(&fui);
    ui.language = "en";
    ui.heading = &ui_heading;
    ui.value = &ui_value;
    ui.button = &ui_button;
    const int draws = yantra_test_c_draw_count();
    d->ui->draw(instance, "ctest_panel", &ui);
    CHECK(yantra_test_c_draw_count() == draws + 1);
    REQUIRE(fui.calls.size() == 3);
    CHECK(fui.calls[1] == "value:records=2");
    CHECK(fui.calls[2] == "button:reset");
    fui.calls.clear();
    d->ui->draw(instance, "ctest_panel", &ui); // the click reset the counter
    CHECK(fui.calls[1] == "value:records=0");

    // Exporter: one job, rows counted, finish.
    yantra_export_request req{static_cast<uint32_t>(sizeof req), 0, "ctest_rows", "/dev/null", ""};
    yantra_export_job* job = nullptr;
    REQUIRE(d->exporter->begin(instance, &req, &job) == YANTRA_OK);
    const yantra_record* rows[] = {&cell, &cell2};
    CHECK(d->exporter->write_table(job, &prov, t->schemas[0], rows, 2) == YANTRA_OK);
    CHECK(d->exporter->finish(job) == YANTRA_OK);

    d->shutdown(instance);
}
