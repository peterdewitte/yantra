// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NFR-07 on the radios' scenarios: the yantra-gen record files of
// floor_change and failed_site replayed through the pipeline and the TETRA
// plugin do not raise the self-interference warning (their noise is the
// scenario's, about 10 dB below the thermal floor of a 15 dB front end);
// the same floor_change records with every SNR lowered by 20 dB (20 dB
// more noise next to each carrier, as a laptop close to the antenna adds)
// raise it after three cycles, with the advice text. Cases skip when the
// radio tools are not built.

#include <doctest/doctest.h>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include <yantra/core/measure/self_interference.h>

#include "e2e_helpers.h"
#include "measure_rig.h"

using namespace mh;

namespace {

// The cycles' warnings: how many cycles had it active, and the first one.
struct Seen {
    std::size_t active = 0;
    std::size_t first = 0;
    std::size_t cycles = 0;
    std::string text;
    std::optional<double> noise;
};

Seen watch(FileRun& run) {
    Seen s;
    ym::SelfInterferenceMonitor m;
    for (const ym::MeasurementCycle& c : run.cycles()) {
        const ym::SelfInterferenceWarning& w = m.on_cycle(c);
        ++s.cycles;
        if (w.last.noise_dbm) s.noise = w.last.noise_dbm;
        if (w.active) {
            if (s.active++ == 0) {
                s.first = s.cycles;
                s.text = w.text;
            }
        }
    }
    return s;
}

}  // namespace

TEST_CASE("NFR-07 floor_change and failed_site as recorded: no self-interference warning") {
    if (!have_records("")) return;
    for (const char* id : {"floor_change", "failed_site"}) {
        CAPTURE(id);
        FileRun run;
        run.run(load(std::string(id) + ".records.jsonl"));
        const Seen s = watch(run);
        CHECK(s.cycles > 5);
        CHECK(s.active == 0);
        REQUIRE(s.noise);
        CHECK(*s.noise == doctest::Approx(-124.42).epsilon(0.001));  // the scenario's noise (truth noise_dbm)
    }
}

TEST_CASE("NFR-07 floor_change with 20 dB more noise next to every carrier: the warning comes on after three cycles") {
    if (!have_records("")) return;
    std::ifstream in(records_dir() / "floor_change.records.jsonl");
    std::stringstream text;
    text << in.rdbuf();
    std::string lowered;
    const std::regex snr("\"snr_db\":(-?[0-9.eE+-]+)");
    std::size_t n = 0;
    const std::string src = text.str();
    auto it = std::sregex_iterator(src.begin(), src.end(), snr);
    std::size_t last = 0;
    for (; it != std::sregex_iterator(); ++it, ++n) {
        lowered += src.substr(last, static_cast<std::size_t>(it->position()) - last);
        lowered += "\"snr_db\":" + std::to_string(std::stod((*it)[1].str()) - 20.0);
        last = static_cast<std::size_t>(it->position() + it->length());
    }
    lowered += src.substr(last);
    REQUIRE(n > 100);
    const fs::path dir = fs::path(MEASURE_SCRATCH_DIR) / "self-interference";
    fs::create_directories(dir);
    const fs::path file = dir / "floor_change_noisy.records.jsonl";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << lowered;
    }
    const auto parsed = ym::read_record_file(file);
    REQUIRE(parsed.ok());
    FileRun run;
    run.run(*parsed.value);
    const Seen s = watch(run);
    REQUIRE(s.noise);
    CHECK(*s.noise == doctest::Approx(-104.42).epsilon(0.001));
    CHECK(s.first == 3);
    CHECK(s.active == s.cycles - 2);
    CHECK(s.text.find("Possible self-interference: the noise floor is") == 0);
    CHECK(s.text.find("above the receiver's thermal floor") != std::string::npos);
    fs::remove_all(dir);
}
