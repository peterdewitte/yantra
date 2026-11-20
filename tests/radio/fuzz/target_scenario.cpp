// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target scenario (RAD-96): a yantra-radios-scenario document (user
// files, RAD-74, docs/SCENARIO_FORMAT.md). What parse_scenario accepts
// must give every cell's broadcast configuration, and the scenario engine
// either refuses it with its documented errors or produces samples and
// ground truth. Inputs naming scene files ("file") are skipped: the
// target never reads other files.
// Adapted from yantra 68ea20a: tests/fuzz/target_scenario.cpp.

#include "fuzz.hpp"

#include "yantra/gen/scenario.hpp"
#include "yantra/gen/scenario_engine.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace gen = yantra::gen;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 256 * 1024)
        return 0;
    const std::string text = yantra::fuzz::text_of(data, size);
    if (text.find("\"file\"") != std::string::npos)
        return 0;
    gen::Scenario s;
    try {
        s = gen::parse_scenario(text, "/nonexistent-fuzz-base");
    } catch (const gen::ScenarioError&) {
        return 0;
    }
    try {
        for (const gen::ScenarioCell& c : s.cells) {
            (void)gen::cell_config(s, c);
            if (!std::isfinite(gen::cell_frequency_hz(c)))
                fail("cell frequency not finite");
        }
        (void)gen::centre_frequency_hz(s);
    } catch (const gen::ScenarioError&) {
        return 0;  // e.g. a carrier off the raster
    }
    // Keep the engine run small: the rate decides the work per sample.
    if (!(s.sample_rate_hz <= 2.0e6))
        return 0;
    try {
        gen::ScenarioEngine e(s);
        std::vector<std::complex<float>> x(512);
        for (int k = 0; k < 4 && !e.done(); ++k)
            (void)e.read(x.data(), x.size());
        for (const gen::TruthRow& row : e.take_truth())
            for (const auto& c : row.cells)
                if (c.on_air && std::isnan(c.level_dbm))
                    fail("on-air cell without a level");
    } catch (const gen::ScenarioError&) {
    } catch (const std::invalid_argument&) {
    }
    return 0;
}
