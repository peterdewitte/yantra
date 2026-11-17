// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers of the demo project tests (test_measure_demo.cpp for Fraunces,
// test_measure_demo_mak.cpp for the MAK example buildings): scratch
// folders, file hashes, the TETRA catalog, a floor's point list, running
// yantra-demo and comparing two made projects.
#pragma once

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <yantra/core/demo/demo_project.h>
#include <yantra/core/measure/catalog.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/project/project.h>
#include <yantra/core/survey/interpolation.h>
#include <yantra/core/survey/interpolation.h>
#include <yantra/core/survey/points.h>
#include <yantra/core/util/sha256.h>

#include "test_platform.hpp"

namespace demo_test {

namespace fs = std::filesystem;
namespace yd = yantra::demo;
namespace yj = yantra::project;
namespace ys = yantra::survey;

inline fs::path scratch(const std::string& name) {
    const fs::path p = fs::path(MEASURE_SCRATCH_DIR) / "demo" / (name + "." + std::to_string(getpid()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

inline std::string file_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

inline std::string file_sha256(const fs::path& p) { return yantra::util::Sha256::of(file_bytes(p)); }

inline std::shared_ptr<yantra::measure::TechnologyCatalog> tetra_catalog(yantra::plugin::PluginLoader& loader) {
    const yantra::plugin::PluginEntry e = loader.load_file(TETRA_PLUGIN_PATH);
    REQUIRE(e.status == yantra::plugin::PluginStatus::Enabled);
    auto catalog = std::make_shared<yantra::measure::TechnologyCatalog>();
    REQUIRE(catalog->add(*loader.info(e.id), loader.descriptor(e.id), loader.instance(e.id)));
    return catalog;
}

inline ys::PointList points_of(const yj::Project& p, const yd::DemoDescription& d, const std::string& floor) {
    ys::PointQuery q;
    q.floor_id = floor;
    q.metric = yd::demo_metric(p, d);
    return ys::point_list(p, q);
}

/// Worker threads of the interpolations a demo case runs: the ctest
/// PROCESSORS of test_measure_host's cases (tests/measure_host/
/// CMakeLists.txt). With all hardware threads (the default) the demo cases
/// starved the real-time simulator cases running next to them under ctest
/// -j (the TSan tree). The results do not depend on it (IND-45).
inline constexpr unsigned kDemoThreads = 4;

inline yantra::survey::RunControl demo_run() {
    yantra::survey::RunControl run;
    run.threads = kDemoThreads;
    return run;
}

/// Whether the build's demo projects were made by this platform's
/// programs. Not on Windows: the Windows tree takes them from the Linux
/// tree (YANTRA_HOST_BUILD_DIR), and the C runtimes' math functions differ
/// there in the last bit (yantra-radios keeps per-platform generator
/// goldens for the same reason), so "made again gives the same project"
/// compares two runs on Windows instead.
#ifdef _WIN32
inline constexpr bool kDemoMadeHere = false;
#else
inline constexpr bool kDemoMadeHere = true;
#endif

/// Runs yantra-demo on `description` into `out` (one extra option
/// appended); true when it succeeded. Its output goes to out/log.txt.
inline bool run_yantra_demo(const fs::path& description, const fs::path& out, const std::string& extra = {}) {
    fs::create_directories(out);
    std::vector<std::string> argv = {YANTRA_DEMO_PATH, "--description", description.string(), "--out", out.string(),
                                     "--gen", DEMO_GEN, "--scenario-dir", DEMO_SCENARIO_DIR,
                                     "--scene-dir", (fs::path(DEMO_DATA_DIR) / "scenes").string(), "--tetra-plugin",
                                     TETRA_PLUGIN_PATH, "--threads", std::to_string(kDemoThreads)};
    if (!extra.empty()) argv.push_back(extra);
    const bool ok = yantra::test::run_program(argv, out / "log.txt") == 0;
    if (!ok) MESSAGE(file_bytes(out / "log.txt"));
    return ok;
}

/// The project content that must not depend on when or where it was
/// made: everything but the creation times and the sources' path hints.
inline void check_same_content(const yj::Project& a, const yj::Project& b, const yd::DemoDescription& d) {
    CHECK(a.meta().info == b.meta().info);
    CHECK(a.building() == b.building());
    CHECK(a.cell_database().entries() == b.cell_database().entries());
    auto sources = [](const yj::Project& p) {
        auto s = p.sources();
        for (auto& f : s) f.added_utc.clear(), f.reference.clear();
        return s;
    };
    CHECK(sources(a) == sources(b));
    for (const auto& s : a.sources()) CHECK(a.source_pages(s.plan.id) == b.source_pages(s.plan.id));
    auto recordings = [](const yj::Project& p) {
        auto r = p.recordings();
        for (auto& f : r) f.added_utc.clear();
        return r;
    };
    CHECK(recordings(a) == recordings(b));
    CHECK(a.sessions() == b.sessions());
    for (const yd::DemoFloor& f : d.floors) {
        CAPTURE(f.id);
        CHECK(points_of(a, d, f.id) == points_of(b, d, f.id));
        auto ia = a.interpolations(f.id, true);
        auto ib = b.interpolations(f.id, true);
        REQUIRE(ia.size() == ib.size());
        for (std::size_t i = 0; i < ia.size(); ++i) {
            ia[i].created_utc.clear();
            ib[i].created_utc.clear();
            CHECK(ia[i] == ib[i]);
        }
    }
}

}  // namespace demo_test
