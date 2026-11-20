// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario engine (RAD-74 with RAD-72/73, RAD-93): sample power against
// the ground-truth level, noise and SNR accuracy, events, frequency
// errors, sample drops, determinism and read-size invariance, multi-wall
// and site levels along the path of the DEMO-09 scenarios, and seeded
// golden digests of the bundled scenarios (TEST_SUITE "sim-golden",
// rewrite with --update).
#include <doctest/doctest.h>

#include "../golden.hpp"
#include "yantra/gen/digest.hpp"
#include "yantra/gen/scenario_engine.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <vector>

using namespace yantra::gen;
namespace fs = std::filesystem;
namespace radios = yantra::radios;
using json = nlohmann::ordered_json;
using cf = std::complex<float>;

namespace {

const std::string kDir = YANTRA_SCENARIO_DIR;

/// One cell at a fixed level, no noise, no position.
json single(double level_dbm)
{
    json j = {{"format", "yantra-radios-scenario"},
              {"version", 1},
              {"id", "single"},
              {"seed", 7},
              {"duration_s", 2.0},
              {"cells", json::array({{{"id", "A"}, {"main_carrier", 3600}, {"level_dbm", level_dbm}}})},
              {"receiver", {{"noise", false}}}};
    return j;
}

Scenario from(const json& j) { return parse_scenario(j.dump()); }

std::vector<cf> run(ScenarioEngine& e, size_t n, size_t step = 4096)
{
    std::vector<cf> x(n);
    size_t got = 0;
    while (got < n) {
        const size_t k = e.read(x.data() + got, std::min(step, n - got));
        if (k == 0)
            break;
        got += k;
    }
    x.resize(got);
    return x;
}

double power(const std::vector<cf>& x, size_t a, size_t b)
{
    double p = 0.0;
    for (size_t i = a; i < b; ++i)
        p += std::norm(std::complex<double>(x[i]));
    return p / static_cast<double>(b - a);
}

double db(double p) { return 10.0 * std::log10(p); }

/// |a - b| <= tol (dB comparisons).
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::string digest(const std::vector<cf>& x) { return sha256_hex(x.data(), x.size() * sizeof(cf)); }

}  // namespace

TEST_CASE("engine: sample power follows the ground-truth level")
{
    for (double level : {-30.0, -60.0, -95.0}) {
        CAPTURE(level);
        ScenarioEngine e(from(single(level)));
        const auto x = run(e, 500000);
        // Reference -20 dBm = power 1: -60 dBm is 1e-4.
        CHECK(near(db(power(x, 0, x.size())), level + 20.0, 0.05));
        const auto truth = e.take_truth();
        REQUIRE(truth.size() == 10);  // t = 0, 0.1, ..., 0.9 s
        CHECK(truth.front().t == 0.0);
        CHECK(truth.back().t == doctest::Approx(0.9));
        CHECK(truth[3].sample == 150000);
        CHECK(truth[3].cells.at(0).level_dbm == level);
        CHECK(truth[3].cells.at(0).source == LevelSource::fixed);
        CHECK(std::isinf(truth[3].cells.at(0).snr_db));
        CHECK(std::isinf(truth[3].noise_dbm));
    }
}

TEST_CASE("engine: noise power and SNR from the noise figure")
{
    json j = single(-100.0);
    j["cells"][0]["on_air"] = false;
    j["receiver"] = {{"noise_figure_db", 9.0}, {"reference_level_dbm", -80.0}};
    j["seed"] = 11;
    ScenarioEngine e(from(j));
    // N = -173.975 + 10 log10(500000) + 9 = -107.985 dBm, relative to -80.
    const double expect_dbm = kThermalNoiseDbmPerHz + 10.0 * std::log10(500e3) + 9.0;
    CHECK(db(e.noise_power()) == doctest::Approx(expect_dbm + 80.0).epsilon(1e-9));
    const auto x = run(e, 400000);
    CHECK(near(db(power(x, 0, x.size())), expect_dbm + 80.0, 0.03));
    const auto truth = e.take_truth();
    REQUIRE_FALSE(truth.empty());
    const double n18 = kThermalNoiseDbmPerHz + 10.0 * std::log10(18000.0) + 9.0;
    CHECK(truth[0].noise_dbm == doctest::Approx(n18));
    CHECK(truth[0].noise_dbm == doctest::Approx(-122.42).epsilon(1e-4));
    CHECK(truth[0].cells[0].source == LevelSource::off);
    CHECK(std::isinf(truth[0].cells[0].level_dbm));

    // SNR of an on-air cell: level - noise in 18 kHz, and the power adds.
    j["cells"][0]["on_air"] = true;
    j["cells"][0]["level_dbm"] = -110.0;
    ScenarioEngine on(from(j));
    const TruthRow r = on.truth_at(0.5);
    CHECK(r.cells[0].snr_db == doctest::Approx(-110.0 - n18));
    const auto y = run(on, 400000);
    const double expect = 10.0 * std::log10(std::pow(10.0, (-110.0 + 80.0) / 10.0) + e.noise_power());
    CHECK(near(db(power(y, 0, y.size())), expect, 0.03));
}

TEST_CASE("engine: events act from their sample on")
{
    json j = single(-40.0);
    j["events"] = json::array({{{"t", 0.5}, {"type", "cell_level"}, {"cell", "A"}, {"db", -10.0}},
                               {{"t", 1.0}, {"type", "cell_off"}, {"cell", "A"}},
                               {{"t", 1.2}, {"type", "cell_on"}, {"cell", "A"}},
                               {{"t", 1.5}, {"type", "rx_gain"}, {"db", 6.0}}});
    for (size_t step : {size_t(4096), size_t(777)}) {
        CAPTURE(step);
        ScenarioEngine e(from(j));
        const auto x = run(e, 1000000, step);
        REQUIRE(x.size() == 1000000);
        CHECK(near(db(power(x, 10000, 240000)), -20.0, 0.05));
        CHECK(near(db(power(x, 260000, 490000)), -30.0, 0.05));
        CHECK(power(x, 500000, 600000) == 0.0);
        CHECK(power(x, 499990, 500000) > 0.0);
        CHECK(near(db(power(x, 610000, 740000)), -30.0, 0.05));
        CHECK(near(db(power(x, 760000, 990000)), -24.0, 0.05));
        const auto truth = e.take_truth();
        REQUIRE(truth.size() == 20);
        CHECK(truth[4].cells[0].level_dbm == -40.0);
        CHECK(truth[5].cells[0].level_dbm == -50.0);
        CHECK_FALSE(truth[10].cells[0].on_air);
        CHECK(truth[12].cells[0].on_air);
        CHECK(truth[12].cells[0].level_dbm == -50.0);
        CHECK(truth[14].rx_gain_db == 0.0);
        CHECK(truth[15].rx_gain_db == 6.0);
    }
}

TEST_CASE("engine: frequency errors, time offsets and the span")
{
    json j = single(-40.0);
    j["cells"].push_back({{"id", "B"},
                          {"cell_number", 2},
                          {"main_carrier", 3604},
                          {"colour_code", 2},
                          {"ppm", 2.0},
                          {"time_offset_s", 1.0}});
    j["receiver"]["ppm"] = -1.0;
    j["channel"] = {{"doppler_shift_hz", 30.0}};
    ScenarioEngine e(from(j));
    CHECK(e.centre_frequency_hz() == 390.05e6);
    CHECK(e.cell_offset_hz(0) == -50e3);
    CHECK(e.cell_offset_hz(1) == 50e3);
    // cell ppm x f - receiver ppm x centre + Doppler shift
    CHECK(e.cell_cfo_hz(0) == doctest::Approx(390.05 + 30.0));
    CHECK(e.cell_cfo_hz(1) == doctest::Approx(2.0 * 390.1 + 390.05 + 30.0));
    // 1 s = 70.588 slots: the cell starts 71 slots on (TN 4 of frame 18).
    const TdmaTime t1 = e.cell_tdma_time(1);
    TdmaTime expect;
    for (int i = 0; i < 71; ++i)
        expect.advance();
    CHECK(t1.tn == expect.tn);
    CHECK(t1.fn == expect.fn);
    CHECK(e.cell_tdma_time(0).fn == 1);
    const TruthRow r = e.truth_at(0.0);
    CHECK(r.cells[1].cfo_hz == e.cell_cfo_hz(1));
    CHECK(r.cells[1].offset_hz == 50e3);
    CHECK(r.cells[1].frequency_hz == 390.1e6);
    CHECK(r.cells[1].colour_code == 2);
}

TEST_CASE("engine: sample-clock error and sample drops")
{
    json j = single(-40.0);
    j["receiver"]["clock_ppm"] = 100.0;
    j["receiver"]["drops"] = {{"first_s", 0.1}, {"interval_s", 0.2}, {"samples", 500}};
    j["duration_s"] = 1.0;
    ScenarioEngine e(from(j));
    CHECK(e.true_sample_rate() == doctest::Approx(500e3 * 1.0001));
    CHECK(e.total_samples() == 500000);
    const auto x = run(e, 600000);
    CHECK(e.done());
    // Drops at 50005, 150015, 250025, 350035, 450045: 5 x 500.
    CHECK(e.dropped() == 2500);
    CHECK(x.size() == 500000 - 2500);
    const auto truth = e.take_truth();
    REQUIRE(truth.size() == 10);
    CHECK(truth[0].output_sample == 0);
    CHECK(truth[3].sample == 150015);
    CHECK(truth[3].output_sample == 150015 - 500);  // the drop at 150015 starts at that sample
}

TEST_CASE("engine: determinism and read-size invariance")
{
    const Scenario s = load_scenario_file(kDir + "/weak_signal.json");
    std::string ref;
    for (size_t step : {size_t(4096), size_t(1000), size_t(7)}) {
        CAPTURE(step);
        ScenarioEngine e(s);
        const auto x = run(e, 60000, step);
        const std::string d = digest(x);
        if (ref.empty())
            ref = d;
        CHECK(d == ref);
    }
    Scenario other = s;
    other.seed = s.seed + 1;
    ScenarioEngine e(other);
    CHECK(digest(run(e, 60000)) != ref);
}

TEST_CASE("engine: multi-wall levels along the DEMO-09 paths (RAD-73)")
{
    const Scenario s = load_scenario_file(kDir + "/indoor_good_das.json");
    ScenarioEngine e(s);
    MultiWallParams p = s.channel.multiwall;
    p.receiver_height_m = s.receiver.height_m;
    p.seed = derive_seed(s.seed, {string_tag("multiwall")});
    MultiWallModel model(p);
    for (double t : {0.0, 2.5, 5.0, 9.9}) {
        CAPTURE(t);
        const TruthRow r = e.truth_at(t);
        REQUIRE(r.position.has_value());
        CHECK(r.position->floor == "f0");
        CHECK(r.position->x_m == doctest::Approx(2.0 + 3.6 * t));
        const CellLevel d0 = model.cell(*s.find_scene("f0"), "D0", {r.position->x_m, r.position->y_m});
        CHECK(r.cells[0].source == LevelSource::scene);
        CHECK(r.cells[0].level_dbm == doctest::Approx(d0.level_dbm).epsilon(1e-12));
        CHECK(r.cells[0].level_dbm > -60.0);
        CHECK(r.cells[1].source == LevelSource::scene);
        CHECK(r.cells[1].level_dbm < r.cells[0].level_dbm - 10.0);
        CHECK(r.cells[2].source == LevelSource::site);
        CHECK(r.cells[2].level_dbm < -80.0);
    }
    // At (10, 10), under antenna a0 (2.7 m, 15 dBm): FSPL over 1.2 m plus
    // a1 at 20 m behind two brick walls? No: the corridor is free, so a1
    // adds 15 - FSPL(20.036 m).
    const auto l = e.levels_at(ReceiverPosition{"f0", 10.0, 10.0}, 0.0);
    const double a0 = 15.0 - free_space_path_loss_db(1.2, 390e6);
    const double a1 = 15.0 - free_space_path_loss_db(std::hypot(20.0, 1.2), 390e6);
    CHECK(l[0].level_dbm == doctest::Approx(10.0 * std::log10(std::pow(10.0, a0 / 10) + std::pow(10.0, a1 / 10))));
    CHECK(l[0].best_antenna == "a0");
    CHECK(l[0].walls == 0);

    // Site cell: EIRP + pattern - log-distance - penetration - shadowing.
    const ScenarioSite& site = *s.find_cell("M1")->site;
    const double d = std::sqrt(std::pow(10.0 - 20.0, 2) + std::pow(10.0 + 600.0, 2) + std::pow(30.0 - 1.5, 2));
    const double pl = s.channel.path_loss.loss_db(d, 390.3e6);
    const double g = pattern_gain_db(site.pattern, bearing_deg({20.0, -600.0}, {10.0, 10.0}));
    CHECK(g > -1.0);
    const double no_shadow = site.eirp_dbm + g - pl - 12.0;
    CHECK(std::fabs(l[2].level_dbm - no_shadow) < 4.0 * 4.0);  // within 4 sigma of shadowing
    CHECK(l[2].level_dbm != no_shadow);

    // Without a position every cell has its fixed level.
    const auto fixed = e.levels_at(std::nullopt, 0.0);
    CHECK(fixed[0].level_dbm == -60.0);
    CHECK(fixed[0].source == LevelSource::fixed);
}

TEST_CASE("engine: the serving cell changes with the floor (DEMO-09)")
{
    ScenarioEngine e(load_scenario_file(kDir + "/floor_change.json"));
    const TruthRow before = e.truth_at(3.0);
    const TruthRow after = e.truth_at(9.0);
    CHECK(before.position->floor == "f0");
    CHECK(after.position->floor == "f1");
    CHECK(before.cells[0].level_dbm > before.cells[1].level_dbm + 10.0);
    CHECK(after.cells[1].level_dbm > after.cells[0].level_dbm + 10.0);
    // Between 6 and 6.5 s the path changes floor: held, not interpolated.
    CHECK(e.receiver_at(6.25)->floor == "f0");
    CHECK(e.receiver_at(6.25)->x_m == 36.0);
}

TEST_CASE("engine: simulator hooks (position override and host scene)")
{
    Scenario s = load_scenario_file(kDir + "/indoor_good_das.json");
    ScenarioEngine e(s);
    e.set_position(ReceiverPosition{"f1", 10.0, 10.0});
    const TruthRow r = e.truth_at(1.0);
    CHECK(r.position->floor == "f1");
    CHECK(r.cells[1].level_dbm > r.cells[0].level_dbm);
    e.set_position(std::nullopt);
    CHECK(e.truth_at(1.0).position->floor == "f0");

    // A host scene replaces the floor and gets the antenna overrides.
    radios::Scene host = *s.find_scene("f0");
    for (auto& a : host.antennas)
        a.eirp_dbm = 30.0;
    const double before = e.levels_at(ReceiverPosition{"f0", 20.0, 10.0}, 0.0)[0].level_dbm;
    e.set_scene(host);
    const double after = e.levels_at(ReceiverPosition{"f0", 20.0, 10.0}, 0.0)[0].level_dbm;
    CHECK(after == doctest::Approx(before + 15.0));
    s.antennas = {{"a0", 15.0}, {"a1", 15.0}};
    ScenarioEngine o(s);
    o.set_scene(host);
    CHECK(o.levels_at(ReceiverPosition{"f0", 20.0, 10.0}, 0.0)[0].level_dbm == doctest::Approx(before));

    // Endless scenario: never done.
    s.duration_s = 0.0;
    ScenarioEngine endless(s);
    CHECK(endless.total_samples() == 0);
    run(endless, 10000);
    CHECK_FALSE(endless.done());
}

TEST_CASE("engine: flat fading in the ground truth")
{
    json j = single(-50.0);
    j["channel"] = {{"fading", {{"model", "rayleigh"}, {"doppler_hz", 10.0}}}};
    j["duration_s"] = 4.0;
    ScenarioEngine e(from(j));
    const auto x = run(e, 2000000);
    // Mean over 4 s at 10 Hz Doppler: about the mean level.
    CHECK(near(db(power(x, 0, x.size())), -30.0, 2.0));
    const auto truth = e.take_truth();
    double spread = 0.0;
    for (const TruthRow& r : truth) {
        CHECK(r.cells[0].level_dbm == -50.0);
        spread = std::max(spread, std::fabs(r.cells[0].level_inst_dbm + 50.0));
    }
    CHECK(spread > 3.0);
    // The truth's instantaneous level matches the samples around it.
    for (size_t k = 5; k < 35; k += 6) {
        const size_t c = static_cast<size_t>(truth[k].sample);
        CHECK(near(db(power(x, c - 500, c + 500)), truth[k].cells[0].level_inst_dbm + 20.0, 1.0));
    }
}

TEST_SUITE("sim-golden")
{
    TEST_CASE("engine: golden digests of the bundled scenarios")
    {
        // First 0.2 s of every scenario (endless ones too), samples and
        // ground truth. Rewrite on purpose only: test_gen_sim --update.
        const bool update = yantra::tests::update_golden();
        const fs::path dir = YANTRA_GEN_SIM_GOLDEN_DIR;
        for (const ScenarioInfo& info : list_scenarios(kDir)) {
            CAPTURE(info.id);
            Scenario s = load_scenario_file(info.path);
            s.truth_interval_s = 0.05;
            ScenarioEngine e(s);
            const auto x = run(e, 100000);
            std::ostringstream out;
            out << "samples " << x.size() << " " << digest(x) << "\n";
            out.precision(17);
            for (const TruthRow& r : e.take_truth()) {
                out << "t " << r.t << " sample " << r.sample << " out " << r.output_sample << "\n";
                for (const CellTruth& c : r.cells)
                    out << "  " << c.cell << " " << to_string(c.source) << " " << c.level_dbm << " "
                        << c.level_inst_dbm << " " << c.walls << " " << c.best_antenna << "\n";
            }
            const fs::path file = dir / (info.id + ".txt");
            if (update) {
                std::ofstream(file, std::ios::binary) << out.str();
                MESSAGE("rewrote " << file.string());
                continue;
            }
            std::ifstream in(file, std::ios::binary);
            REQUIRE_MESSAGE(in.good(), "missing golden file; create it with test_gen_sim --update");
            std::stringstream text;
            text << in.rdbuf();
            CHECK(text.str() == out.str());
        }
    }
}
