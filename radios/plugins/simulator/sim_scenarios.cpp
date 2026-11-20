// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator's embedded scenarios (see sim_scenarios.hpp).
#include "sim_scenarios.hpp"

#include <algorithm>
#include <map>
#include <string_view>

namespace yantra::simulator {
namespace {

std::string embedded_text(std::string_view path)
{
    while (path.starts_with("./"))
        path.remove_prefix(2);
    for (const EmbeddedFile& f : embedded_files())
        if (path == f.path)
            return f.text;
    throw gen::ScenarioError("scene file " + std::string(path) + " is not in the plugin");
}

struct Catalogue {
    std::vector<gen::ScenarioInfo> list;
    std::map<std::string, gen::Scenario> scenarios;
    std::string problems;
};

const Catalogue& catalogue()
{
    static const Catalogue c = [] {
        Catalogue out;
        for (const EmbeddedFile& f : embedded_files()) {
            const std::string_view path(f.path);
            if (path.find('/') != std::string_view::npos)
                continue; // scene files
            try {
                gen::Scenario s = gen::parse_scenario(f.text, [](const std::string& file) {
                    return embedded_text(file);
                });
                gen::ScenarioInfo info;
                info.path = f.path;
                info.id = s.id;
                info.name = s.name;
                info.example_ids = s.example_ids;
                info.is_default = s.is_default;
                out.list.push_back(info);
                out.scenarios.emplace(s.id, std::move(s));
            } catch (const std::exception& e) {
                out.problems += std::string(f.path) + ": " + e.what() + "\n";
            }
        }
        std::sort(out.list.begin(), out.list.end(),
                  [](const gen::ScenarioInfo& a, const gen::ScenarioInfo& b) { return a.id < b.id; });
        return out;
    }();
    return c;
}

} // namespace

const std::vector<gen::ScenarioInfo>& scenario_list() { return catalogue().list; }
const std::string& scenario_problems() { return catalogue().problems; }

gen::Scenario load_scenario(const std::string& id)
{
    const auto& m = catalogue().scenarios;
    const auto it = m.find(id);
    if (it == m.end())
        throw gen::ScenarioError("no scenario " + id + " in the simulator");
    return it->second;
}

} // namespace yantra::simulator
