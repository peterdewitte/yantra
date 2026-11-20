// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The decoder sensitivity sweep (RAD-126, RAD-124) and its phase noise
// model (the TU50/HT200 channels are the generator's, tests/gen_sim).
// Copied from yantra 68ea20a: tests/sens/test_sens.cpp, adapted.

#include "receive.hpp"
#include "yantra/sens/sweep.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace sens = yantra::sens;
using cf = std::complex<float>;


TEST_CASE("channel: Wiener phase noise has the configured increment variance")
{
    std::vector<cf> x(200000, cf(1.0f, 0.0f));
    sens::detail::PhaseNoise(10.0, 72000.0, 5).apply(x.data(), x.size());
    double var = 0.0;
    for (std::size_t i = 1; i < x.size(); ++i) {
        const double d = std::arg(x[i] * std::conj(x[i - 1]));
        var += d * d;
    }
    var /= static_cast<double>(x.size() - 1);
    CHECK(var == doctest::Approx(2.0 * std::numbers::pi * 10.0 / 72000.0).epsilon(0.05));
    CHECK(std::abs(x.back()) == doctest::Approx(1.0).epsilon(1e-5));
}

TEST_CASE("sweep: scenarios, tiers and metrics")
{
    const auto all = sens::scenarios();
    std::vector<std::string> names;
    for (const auto& s : all)
        names.push_back(s.name);
    CHECK(names == std::vector<std::string>{"awgn", "cfo2", "cfo20", "cfo50", "offcentre", "clock50", "tu50",
                                            "ht200", "phasenoise", "combined"});
    CHECK(sens::scenario("cfo20").wideband);
    CHECK_THROWS_AS(sens::scenario("nope"), std::invalid_argument);
    CHECK(sens::tier("full").snr_db.size() == 23);
    CHECK_THROWS_AS(sens::tier("slow"), std::invalid_argument);
    CHECK_THROWS_AS(sens::metric(sens::PointResult{}, "x"), std::invalid_argument);
    // Never locked: lock time "infinite", so a max bound fails.
    CHECK(sens::metric(sens::PointResult{}, "lock_ms") > 1e6);
}

TEST_CASE("sweep: a clean signal decodes completely, deterministically")
{
    for (const char* name : {"awgn", "offcentre"}) {
        INFO(name);
        const sens::Scenario s = sens::scenario(name);
        const sens::PointResult a = sens::run_once(s, 30.0, 2.5, 0, 42);
        const sens::PointResult b = sens::run_once(s, 30.0, 2.5, 0, 42);
        CHECK(a.bsch.sent > 0);
        CHECK(a.bnch.sent == a.bsch.sent);
        CHECK(a.schf.sent > 100);
        CHECK(a.bsch.ok == a.bsch.sent);
        CHECK(a.bnch.ok == a.bnch.sent);
        CHECK(a.schf.ok == a.schf.sent);
        CHECK(a.aach.ok == a.aach.sent);
        CHECK(a.sync.ok == a.sync.sent);
        CHECK(a.crc_errors == 0);
        CHECK(a.bits > 2000);
        CHECK(a.bit_errors == 0);
        CHECK(a.locked_runs == 1);
        CHECK(a.lock_ms() < 200.0);  // frame 18 starts 2 frames (113 ms) in
        CHECK(a.mer_db() > 25.0);
        CHECK(a.schf.ok == b.schf.ok);
        CHECK(a.bit_errors == b.bit_errors);
        CHECK(a.mer_db_sum == b.mer_db_sum);
    }
    // Noise alone: nothing decodes, half the bits are wrong.
    const sens::PointResult n = sens::run_once(sens::scenario("awgn"), -20.0, 2.0, 0, 42);
    CHECK(n.bnch.ok == 0);
    CHECK(n.schf.ok == 0);
    CHECK(n.ber() == doctest::Approx(0.5).epsilon(0.1));
}

TEST_CASE("sweep: CSV, Markdown and the baseline format")
{
    const sens::Tier t{"test", {30.0}, 1, 1.5};
    const auto r = sens::run_sweep({{sens::scenario("awgn"), 30.0}, {sens::scenario("cfo2"), 30.0}}, t, 1, 2);
    REQUIRE(r.size() == 2);
    CHECK(r[1].scenario == "cfo2");
    std::ostringstream csv;
    sens::write_csv(csv, r);
    CHECK(csv.str().rfind("scenario,snr_db,runs,", 0) == 0);
    CHECK(csv.str().find("\nawgn,30.0,1,") != std::string::npos);
    std::ostringstream md;
    sens::write_markdown(md, r, t, 1);
    CHECK(md.str().find("| awgn | 30 | 30 | 30 | 30 | 0.00 % |") != std::string::npos);
    CHECK(md.str().find("### cfo2") != std::string::npos);

    std::istringstream good("# comment\nscenario,snr_db,metric,bound,limit\nawgn,12,bnch,min,99\nht200,20,ber,max,4\n");
    const auto e = sens::parse_baseline(good);
    REQUIRE(e.size() == 2);
    CHECK(e[0].is_min);
    CHECK(e[1].metric == "ber");
    CHECK_FALSE(e[1].is_min);
    std::istringstream bad1("awgn,12,bnch,above,99\n");
    CHECK_THROWS_WITH_AS(sens::parse_baseline(bad1), doctest::Contains("line 1"), std::runtime_error);
    std::istringstream bad2("awgn,12,bnch,min,99\nnope,12,bnch,min,99\n");
    CHECK_THROWS_WITH_AS(sens::parse_baseline(bad2), doctest::Contains("line 2"), std::runtime_error);
    std::istringstream bad3("awgn,12,speed,min,99\n");
    CHECK_THROWS_AS(sens::parse_baseline(bad3), std::runtime_error);

    const auto v = sens::check_baseline({{"awgn", 30.0, "schf", true, 99.0}, {"awgn", 30.0, "ber", false, 0.0}}, t, 1, 2);
    REQUIRE(v.size() == 2);
    CHECK(v[0].pass);
    CHECK(v[1].pass);
    CHECK(v[0].measured == doctest::Approx(100.0));
}
