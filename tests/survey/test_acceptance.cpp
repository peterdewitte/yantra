// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stored acceptance profiles (EXP-06; docs/SURVEY.md "Acceptance
// profiles"): the project table of schema 7 (order, unique names, JSON
// definitions, change notice, migration of schema 1..6 files), the JSON
// definition (round trip, unknown members ignored, newer versions and bad
// values refused) and the default profile.

#include "yantra/core/project/project.h"
#include "yantra/core/survey/acceptance.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "survey_helpers.h"

namespace {

ys::AcceptanceProfile profile(const std::string& name, ys::AcceptanceProfile::Basis basis,
                              std::vector<ys::AcceptanceCriterion> criteria) {
    ys::AcceptanceProfile p;
    p.name = name;
    p.basis = basis;
    p.criteria = std::move(criteria);
    return p;
}

void check_same(const ys::AcceptanceProfile& a, const ys::AcceptanceProfile& b) {
    CHECK(a.name == b.name);
    CHECK(a.unit == b.unit);
    CHECK(a.basis == b.basis);
    CHECK(a.criteria == b.criteria);
    CHECK(a.square_m == b.square_m);
    CHECK(a.square_percentile == b.square_percentile);
    CHECK(a.min_confidence == b.min_confidence);
}

}  // namespace

TEST_CASE("EXP-06 acceptance profiles are stored in the project in order, survive a reopen and notify") {
    const fs::path dir = scratch("acceptance-store");
    const fs::path file = dir / "a.yantra";
    using B = ys::AcceptanceProfile::Basis;
    auto das = profile("DAS acceptance", B::FloorArea, {{-95.0, 95.0}, {-100.0, 99.0}});
    das.min_confidence = 0.7;
    auto walk = profile("Walk", B::Points, {{-90.0, 90.0}});
    auto tested = profile("Tested area", B::TestedArea, {{-85.5, 80.25}});
    tested.square_m = 3.0;
    tested.square_percentile = 10.0;
    {
        auto p = pj::Project::create(file, pj::ProjectInfo{"Hall", "", "", ""});
        CHECK(p->acceptance_profiles().empty());
        CHECK(ys::acceptance_profiles(*p).empty());
        std::vector<pj::Change::Kind> seen;
        p->subscribe([&](const pj::Change& c) { seen.push_back(c.kind); });
        ys::set_acceptance_profiles(*p, {walk, das, tested});
        REQUIRE(seen.size() == 1);
        CHECK(seen[0] == pj::Change::Kind::Acceptance);
        CHECK(p->check().empty());
    }
    auto p = pj::Project::open(file);
    const auto back = ys::acceptance_profiles(*p);
    REQUIRE(back.size() == 3);
    check_same(back[0], walk);
    check_same(back[1], das);
    check_same(back[2], tested);
    // Replacing keeps the new order; an empty list removes them all.
    ys::set_acceptance_profiles(*p, {tested, walk});
    const auto again = ys::acceptance_profiles(*p);
    REQUIRE(again.size() == 2);
    CHECK(again[0].name == "Tested area");
    CHECK(again[1].name == "Walk");
    ys::set_acceptance_profiles(*p, {});
    CHECK(p->acceptance_profiles().empty());
}

TEST_CASE("EXP-06 invalid acceptance profiles are refused; the stored ones stay") {
    const fs::path dir = scratch("acceptance-invalid");
    auto p = pj::Project::create(dir / "a.yantra", pj::ProjectInfo{"Hall", "", "", ""});
    using B = ys::AcceptanceProfile::Basis;
    const auto ok = profile("Walk", B::Points, {{-90.0, 90.0}});
    ys::set_acceptance_profiles(*p, {ok});
    auto refused = [&](const std::vector<ys::AcceptanceProfile>& v) {
        try {
            ys::set_acceptance_profiles(*p, v);
            FAIL("accepted");
        } catch (const pj::ProjectError& e) {
            CHECK(e.code() == pj::ProjectError::Code::Invalid);
        }
    };
    refused({profile("", B::Points, {{-90.0, 90.0}})});
    refused({profile("No criteria", B::Points, {})});
    refused({profile("Percent", B::Points, {{-90.0, 101.0}})});
    refused({profile("Threshold", B::Points, {{std::nan(""), 50.0}})});
    auto square = profile("Square", B::TestedArea, {{-90.0, 90.0}});
    square.square_m = 0.0;
    refused({square});
    auto conf = profile("Confidence", B::FloorArea, {{-90.0, 90.0}});
    conf.min_confidence = 1.5;
    refused({conf});
    refused({ok, ok});
    // The project level refuses what the survey level would not write.
    CHECK_THROWS_AS(p->set_acceptance_profiles({pj::StoredAcceptanceProfile{"X", "[1, 2]"}}), pj::ProjectError);
    CHECK_THROWS_AS(p->set_acceptance_profiles({pj::StoredAcceptanceProfile{"", "{}"}}), pj::ProjectError);
    CHECK_THROWS_AS(p->set_acceptance_profiles({pj::StoredAcceptanceProfile{"X", "{}"}, pj::StoredAcceptanceProfile{"X", "{}"}}),
                    pj::ProjectError);
    const auto back = ys::acceptance_profiles(*p);
    REQUIRE(back.size() == 1);
    check_same(back[0], ok);
    // A stored definition that cannot be read is reported as damaged.
    p->set_acceptance_profiles({pj::StoredAcceptanceProfile{"Odd", R"({"format":"something else"})"}});
    try {
        (void)ys::acceptance_profiles(*p);
        FAIL("read");
    } catch (const pj::ProjectError& e) {
        CHECK(e.code() == pj::ProjectError::Code::Damaged);
        CHECK(std::string(e.what()).find("Odd") != std::string::npos);
    }
}

TEST_CASE("EXP-06 acceptance profile JSON: round trip, unknown members ignored, newer versions and bad values refused") {
    using B = ys::AcceptanceProfile::Basis;
    auto p = profile("P", B::TestedArea, {{-95.0, 95.0}});
    p.unit = "dB";
    p.square_m = 1.5;
    const nlohmann::json j = ys::profile_json(p);
    CHECK(j.at("format") == ys::kAcceptanceFormat);
    CHECK(j.at("version") == ys::kAcceptanceVersion);
    CHECK(j.at("basis") == "tested_area");
    CHECK(!j.contains("name"));
    check_same(ys::profile_from_json("P", j), p);

    nlohmann::json extra = j;
    extra["comment"] = "from a later minor change";
    extra["criteria"][0]["label"] = "indoor";
    check_same(ys::profile_from_json("P", extra), p);
    // Members left out take the defaults.
    const auto minimal = ys::profile_from_json(
        "M", nlohmann::json::parse(R"({"format":"yantra-acceptance-profile","version":1,"criteria":[{"threshold":-90,"min_percent":90}]})"));
    CHECK(minimal.unit == "dBm");
    CHECK(minimal.basis == B::Points);
    CHECK(minimal.square_m == 2.0);

    auto bad = [&](const std::function<void(nlohmann::json&)>& change) {
        nlohmann::json k = j;
        change(k);
        CHECK_THROWS_AS(ys::profile_from_json("P", k), std::invalid_argument);
    };
    bad([](nlohmann::json& k) { k["version"] = ys::kAcceptanceVersion + 1; });
    bad([](nlohmann::json& k) { k["format"] = "other"; });
    bad([](nlohmann::json& k) { k.erase("version"); });
    bad([](nlohmann::json& k) { k["basis"] = "everywhere"; });
    bad([](nlohmann::json& k) { k["criteria"] = nlohmann::json::array(); });
    bad([](nlohmann::json& k) { k["criteria"][0].erase("min_percent"); });
    bad([](nlohmann::json& k) { k["criteria"][0]["threshold"] = "-95"; });
    bad([](nlohmann::json& k) { k["square_percentile"] = 120; });
    CHECK_THROWS_AS(ys::profile_from_json("P", nlohmann::json::array()), std::invalid_argument);

    for (B b : {B::Points, B::TestedArea, B::FloorArea}) CHECK(ys::basis_from_name(ys::basis_name(b)) == b);
    CHECK(!ys::basis_from_name("area"));

    const auto d = ys::default_acceptance_profile();
    CHECK(d.name == "Indoor acceptance");
    CHECK(d.unit == "dBm");
    CHECK(d.basis == B::Points);
    CHECK(d.criteria == std::vector<ys::AcceptanceCriterion>{{-95.0, 95.0}});
    CHECK(ys::validate(d).empty());
}

TEST_CASE("schema 1..6 files migrate to schema 7: no acceptance profiles, then stored ones") {
    const fs::path dir = scratch("migrate-7");
    for (int version : {1, 2, 3, 4, 5, 6}) {
        CAPTURE(version);
        const fs::path file = dir / ("v" + std::to_string(version) + ".yantra");
        pj::testing::create_at_version(file, version, pj::ProjectInfo{"Old", "Hall", "", ""});
        {
            auto p = pj::Project::open(file);
            CHECK(p->opened_version() == version);
            CHECK(p->meta().schema_version == pj::kSchemaVersion);
            CHECK(pj::kSchemaVersion >= 7);
            CHECK(fs::exists(file.string() + ".schema" + std::to_string(version) + ".bak"));
            CHECK(p->acceptance_profiles().empty());
            ys::set_acceptance_profiles(*p, {profile("Walk", ys::AcceptanceProfile::Basis::Points, {{-90.0, 90.0}})});
            CHECK(p->check().empty());
        }
        auto p = pj::Project::open(file);
        CHECK(p->opened_version() == pj::kSchemaVersion);
        REQUIRE(ys::acceptance_profiles(*p).size() == 1);
    }
}
