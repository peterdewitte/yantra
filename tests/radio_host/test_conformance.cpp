// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The conformance kit (RAD-91) on known plugins: the beacon example
// (RAD-64) and the fake plugin pass; a refused plugin and the fake plugin
// with injected faults (YANTRA_FAKE_FAULT) fail the check that covers the
// broken rule, so the kit finds what it claims to find.
#include <doctest/doctest.h>

#include <conformance.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

using namespace yantra::radio_host;

namespace {

const CheckResult* find(const ConformanceReport& r, const std::string& id)
{
    for (const CheckResult& c : r.checks)
        if (c.id == id)
            return &c;
    return nullptr;
}

std::string result_of(const ConformanceReport& r, const std::string& id)
{
    const CheckResult* c = find(r, id);
    return c != nullptr ? c->result : "missing";
}

std::string problems_of(const ConformanceReport& r)
{
    std::string out;
    for (const CheckResult& c : r.checks)
        for (const std::string& p : c.problems)
            out += c.id + ": " + p + "\n";
    return out;
}

// Sets an environment variable for one scope.
struct ScopedEnv {
    std::string name;
    ScopedEnv(const char* n, const char* value) : name(n) { setenv(n, value, 1); }
    ~ScopedEnv() { unsetenv(name.c_str()); }
};

ConformanceOptions quick()
{
    ConformanceOptions o;
    o.stream_seconds = 1.5;
    return o;
}

} // namespace

TEST_CASE("the beacon example plugin passes the kit")
{
    const ConformanceReport r = run_conformance(BEACON_PLUGIN, quick());
    INFO(problems_of(r));
    CHECK(r.passed());
    CHECK(r.plugin_id == "beacon");
    CHECK(r.api_version == "1.0");
    CHECK(r.device == "beacon:0");
    for (const char* id : {"load", "version", "enumerate", "open_close", "settings", "persistence",
                           "actions", "presets", "sensors", "self_test", "plans", "records",
                           "plan_change", "state_machine", "stop_during_push", "sink_full",
                           "completeness", "scope", "cleanup"})
        CHECK_MESSAGE(result_of(r, id) == "pass", id);
    CHECK(result_of(r, "hotplug") == "skip"); // no HOTPLUG capability
    CHECK(result_of(r, "context") == "skip"); // no SCENE capability

    const auto j = nlohmann::json::parse(conformance_json(r));
    CHECK(j["format"] == "yantra-radio-conformance");
    CHECK(j["version"] == 1);
    CHECK(j["result"] == "pass");
    CHECK(j["plugin"]["id"] == "beacon");
    CHECK(j["counts"]["fail"] == 0);
    CHECK(j["checks"].size() == r.checks.size());
    CHECK(j["checks"][0]["requirements"].size() >= 1);
}

TEST_CASE("the fake SCENE plugin passes the kit, context check included")
{
    const ConformanceReport r = run_conformance(FAKE_PLUGIN, quick());
    INFO(problems_of(r));
    CHECK(r.passed());
    CHECK(result_of(r, "context") == "pass");
    CHECK(result_of(r, "settings") == "pass");
}

TEST_CASE("without a device the kit checks the diagnostic and skips the device checks")
{
    ScopedEnv env("YANTRA_FAKE_NO_DEVICE", "1");
    const ConformanceReport r = run_conformance(FAKE_PLUGIN, quick());
    INFO(problems_of(r));
    CHECK(r.passed());
    CHECK(result_of(r, "enumerate") == "pass");
    CHECK(result_of(r, "records") == "skip");
    const CheckResult* records = find(r, "records");
    REQUIRE(records != nullptr);
    CHECK(records->detail.find("no fake device") != std::string::npos);
}

TEST_CASE("a plugin of another major API version fails at load")
{
    const ConformanceReport r = run_conformance(FAKE_PLUGIN_V2, quick());
    CHECK_FALSE(r.passed());
    REQUIRE(r.checks.size() == 1);
    CHECK(r.checks[0].id == "load");
    CHECK(r.checks[0].problems.at(0).find("radio API 2.0") != std::string::npos);
}

TEST_CASE("the kit finds injected faults")
{
    struct Case {
        const char* fault;
        const char* check;
        const char* problem;
    };
    const Case cases[] = {
        {"no_settings_changed", "presets", "did not call settings_changed"},
        {"off_raster", "settings", "raster"},
        {"no_busy", "state_machine", "RESTART setting while streaming"},
        {"any_plan", "plans", "unsupported plan type"},
    };
    for (const Case& c : cases) {
        const std::string fault = c.fault;
        CAPTURE(fault);
        ScopedEnv env("YANTRA_FAKE_FAULT", c.fault);
        const ConformanceReport r = run_conformance(FAKE_PLUGIN, quick());
        INFO(problems_of(r));
        CHECK_FALSE(r.passed());
        const CheckResult* check = find(r, c.check);
        REQUIRE(check != nullptr);
        CHECK(check->result == "fail");
        bool named = false;
        for (const std::string& p : check->problems)
            named = named || p.find(c.problem) != std::string::npos;
        CHECK(named);
    }
    // slow_stop: every stop_stream is over the RAD-15 limit.
    ScopedEnv env("YANTRA_FAKE_FAULT", "slow_stop");
    const ConformanceReport r = run_conformance(FAKE_PLUGIN, quick());
    CHECK(result_of(r, "state_machine") == "fail");
    CHECK(problems_of(r).find("stop_stream took longer than 1 s (RAD-15)") != std::string::npos);
}
