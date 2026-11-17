// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target project (TST-12, TST-23): a damaged indoor project file
// (docs/formats/PROJECT.md). Opening it and reading everything gives
// ProjectError or results, never a crash; an opened file takes a change
// and shows it.
// Adapted from yantra-v2 68ea20a: tests/fuzz/target_project.cpp, for the
// project API of schema 7.

#include "fuzz.hpp"

#include "yantra/core/project/project.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace pj = yantra::project;
using yantra::fuzz::fail;

namespace {

void exercise(const std::filesystem::path& file) {
    auto p = pj::Project::open(file);
    (void)p->meta();
    const auto floors = p->floors();
    (void)p->building();
    (void)p->interpolation_settings();
    (void)p->wall_classes();
    (void)p->cells();
    (void)p->cell_database();
    (void)p->recordings();
    (void)p->acceptance_profiles();
    for (const pj::SourceFile& s : p->sources()) {
        try {
            (void)p->read_source(s.plan.id);
            (void)p->source_pages(s.plan.id);
        } catch (const pj::ProjectError&) {
        }
    }
    for (const auto& f : floors) {
        (void)p->walls(f.id);
        (void)p->antennas(f.id);
        (void)p->point_count(f.id);
        for (const auto& r : p->interpolations(f.id, true)) (void)p->interpolation(r.id);
        for (const auto& s : p->sessions(f.id)) (void)p->points(s.id);
    }
    (void)p->check();
    // A change on the opened file.
    int level = 1000;
    for (const auto& f : floors) level = std::max(level, f.level + 1);
    yantra::building::Floor f;
    f.name = "Fuzz floor";
    f.level = level;
    const std::string id = p->add_floor(f);
    const auto back = p->floor(id);
    if (!back || back->name != f.name) fail("a floor added to an opened project is not there");
    const std::string a = p->add_antenna(id, yantra::building::Antenna{});
    const auto antennas = p->antennas(id);
    if (antennas.size() != 1 || antennas.front().id != a) fail("an antenna added to an opened project is not there");
    p->close();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;
    yantra::fuzz::ScratchDir dir("project");
    const auto file = dir.path() / "input.yantra";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    try {
        exercise(file);
    } catch (const pj::ProjectError&) {
        // The documented error of a damaged or foreign file.
    }
    return 0;
}
