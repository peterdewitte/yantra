// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugin test kit library in process (PLG-13): the golden comparison,
// the machine-readable report, and the checks on the example technology
// plugin (PLG-15), the C test plugin and an impure variant.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "kit.h"

namespace fs = std::filesystem;
namespace kit = yantra::kit;
using nlohmann::json;

namespace {

const fs::path kSource = YANTRA_SOURCE_DIR;
const fs::path kBeaconRecords = kSource / "plugins/example_beacon/testdata/beacon.records.jsonl";

std::string messages(const kit::Report& r) {
    std::string s;
    for (const kit::Check& c : r.checks)
        for (const std::string& m : c.messages) s += c.name + ": " + m + "\n";
    return s;
}

kit::Result result(const kit::Report& r, const char* check) {
    const kit::Check* c = r.find(check);
    REQUIRE(c != nullptr);
    return c->result;
}

}  // namespace

TEST_CASE("plugin kit: golden comparison with tolerances") {
    std::vector<std::string> d;
    CHECK(kit::compare(json{{"a", 1.0}, {"b", {1, 2}}}, json{{"a", 1.0 + 1e-9}, {"b", {1, 2}}},
                       1e-6, 1e-9, d));
    CHECK(d.empty());
    CHECK_FALSE(kit::compare(json{{"a", 1.0}}, json{{"a", 1.1}}, 1e-6, 1e-9, d));
    CHECK_FALSE(kit::compare(json{{"n", 3}}, json{{"n", 4}}, 1.0, 1.0, d));  // integers exact
    CHECK_FALSE(kit::compare(json{{"x", "a"}}, json{{"y", "a"}}, 0, 0, d));
    CHECK_FALSE(kit::compare(json::array({1, 2}), json::array({1}), 0, 0, d));
    REQUIRE(d.size() == 5);
    CHECK(d[0].find("/a: expected 1.0, got 1.1") == 0);
    CHECK(d[2] == "/x: missing in the output");
    CHECK(d[3] == "/y: not in the golden file");
    CHECK(d[4] == ": 2 entries expected, got 1");
}

TEST_CASE("plugin kit: the example technology plugin passes every check (PLG-15)") {
    kit::Options o;
    o.plugin = KIT_EXAMPLE_PATH;
    o.records = {kBeaconRecords};
    o.strict_translations = true;
    const kit::Report r = kit::run(o);
    INFO(messages(r));
    CHECK(r.passed());
    CHECK(r.plugin_id == "yantra.example.beacon");
    for (const char* name : {"load", "descriptor", "licence", "schemas", "band_plan",
                             "radio_schema", "records", "rules", "neighbours", "ui", "threading"})
        CHECK_MESSAGE(result(r, name) == kit::Result::Pass, name);
    CHECK(result(r, "feature") == kit::Result::Skip);
    CHECK(result(r, "exporter") == kit::Result::Skip);
    CHECK(result(r, "golden") == kit::Result::Skip);
    // The output holds every record, with the channel and C1 of the rules.
    const json& out = r.output;
    REQUIRE(out.contains("records"));
    CHECK(out["records"].size() == 30);
    const json& first = out["records"][0];
    CHECK(first["schema"] == "beacon");
    CHECK(first["key"] == "mnc=7;la=101");
    CHECK(first["channel"] == 1);
    CHECK(first["flags"] == json::array({"uncalibrated", "simulated"}));
    // -30 dBFS uncalibrated, TX 20 dBm, MS 30 dBm: C1 = 70 - 10 = 60 dB.
    CHECK(first["c1_db"].get<double>() == doctest::Approx(60.0).epsilon(1e-3));
    CHECK(out["neighbours"].size() > 0);
    CHECK(out["ui"]["windows"][0]["id"] == "beacon_channels");
    // The report is machine-readable.
    const json report = r.to_json();
    CHECK(report["format"] == "yantra-plugin-test-report");
    CHECK(report["result"] == "pass");
    CHECK(report["checks"].size() == r.checks.size());
}

TEST_CASE("plugin kit: a calibration offset turns dBFS into dBm") {
    kit::Options o;
    o.plugin = KIT_EXAMPLE_PATH;
    o.records = {kBeaconRecords};
    o.calibrated = true;
    o.level_offset_db = -50.0;
    const kit::Report r = kit::run(o);
    INFO(messages(r));
    REQUIRE(r.passed());
    const json& first = r.output["records"][0];
    CHECK(first["flags"] == json::array({"simulated"}));
    CHECK(first["values"]["level"].get<double>() == doctest::Approx(-80.0).epsilon(1e-3));
}

TEST_CASE("plugin kit: the C test plugin's feature and exporter are run") {
    kit::Options o;
    o.plugin = KIT_CTEST_PATH;
    o.records = {kSource / "tests/plugin_kit/data/ctest.records.jsonl"};
    const kit::Report r = kit::run(o);
    INFO(messages(r));
    CHECK(r.passed());
    CHECK(result(r, "feature") == kit::Result::Pass);
    CHECK(result(r, "exporter") == kit::Result::Pass);
    CHECK(r.output["feature"]["records_delivered"] == 15);
    CHECK(r.output["exports"][0]["format"] == "ctest_rows");
    // Its UI texts are not translated: a warning, a failure only when strict.
    CHECK_FALSE(r.warnings.empty());
    o.strict_translations = true;
    CHECK(result(kit::run(o), "ui") == kit::Result::Fail);
}

TEST_CASE("plugin kit: impure rules and bad options fail with the reason") {
    kit::Options o;
    o.plugin = KIT_IMPURE_PATH;
    o.records = {kBeaconRecords};
    const kit::Report r = kit::run(o);
    CHECK_FALSE(r.passed());
    REQUIRE(result(r, "threading") == kit::Result::Fail);
    CHECK(r.find("threading")->messages[0].find("not pure") != std::string::npos);

    kit::Options bad;
    bad.plugin = KIT_EXAMPLE_PATH;
    bad.radio = KIT_EXAMPLE_PATH;
    bad.golden = "x.json";
    const kit::Report b = kit::run(bad);
    CHECK(result(b, "options") == kit::Result::Fail);
    CHECK(result(b, "load") == kit::Result::Skip);
    CHECK(b.find("options")->messages[0].find("deterministic") != std::string::npos);
}
