// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator's scenarios (RAD-61, RAD-74), compiled into the plugin
// from data/scenarios (embed_scenarios.cmake), so the library needs no
// data folder (RAD-132). Scene files named by a scenario come from the
// same embedded set.
#pragma once

#include <yantra/gen/scenario.hpp>

#include <string>
#include <vector>

namespace yantra::simulator {

/// One embedded file: path relative to data/scenarios, and its text.
struct EmbeddedFile {
    const char* path;
    const char* text;
};
/// Every embedded file (generated).
const std::vector<EmbeddedFile>& embedded_files();

/// The scenarios (top-level files), sorted by id; ScenarioInfo::path is
/// the embedded file name. Parsed once.
const std::vector<gen::ScenarioInfo>& scenario_list();
/// Problems found when the embedded scenarios were parsed ("" when none).
const std::string& scenario_problems();
/// A fresh copy of the scenario `id`; gen::ScenarioError when unknown.
gen::Scenario load_scenario(const std::string& id);

} // namespace yantra::simulator
