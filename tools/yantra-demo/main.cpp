// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-demo: makes a bundled demo project (DEMO-05, DEMO-06, DEMO-13;
// docs/DEMO.md) from its description, e.g. data/demo/fraunces/fraunces.json:
//
//   yantra-demo --description FILE --out DIR --gen YANTRA_GEN
//               --scenario-dir DIR --tetra-plugin TETRA_SO
//               [--work DIR] [--truth-interval S] [--threads N] [--keep-work]
//
// DIR/<id>.yantra is the project, DIR/recordings/ its walk recordings.
// Per floor, the scenario that the floor's example id selects in the
// radios' scenario folder (RAD-18, RAD-74) gets the floor's scene and walk
// (demo::walk_scenario); yantra-gen of yantra-radios (RAD-71) writes its
// ground truth as a record file (its I/Q output is discarded: Yantra
// reads records only, RAD-26); the walk is replayed into the project and
// recorded (demo::record_walk); then each floor is interpolated
// (IND-40..45) and the result stored. The same inputs give the same
// project content and recording files.
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <yantra/core/demo/demo_project.h>
#include <yantra/core/measure/catalog.h>
#include <yantra/core/measure/record_file.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/survey/interpolation.h>

namespace fs = std::filesystem;
namespace yd = yantra::demo;

namespace {

int usage(std::ostream& out, int code) {
    out << "usage: yantra-demo --description FILE --out DIR --gen YANTRA_GEN --scenario-dir DIR\n"
           "                   --tetra-plugin TETRA_SO [--work DIR] [--truth-interval S]\n"
           "                   [--threads N] [--keep-work]\n"
           "Makes the demo project of FILE (format yantra-demo-project) in DIR: DIR/<id>.yantra\n"
           "and DIR/recordings/ (DEMO-05, DEMO-06; docs/DEMO.md).\n";
    return code;
}

// A path for the shell, in single quotes.
std::string quoted(const fs::path& p) {
    std::string out = "'";
    for (char c : p.string()) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + file.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// yantra-gen on a scenario file: the ground-truth record file only (the
// samples go to the null device).
void run_gen(const fs::path& gen, const fs::path& scenario, const fs::path& records) {
#ifdef _WIN32
    const char* null_device = "NUL";
#else
    const char* null_device = "/dev/null";
#endif
    const std::string cmd = quoted(gen) + " --scenario " + quoted(scenario) + " --format cs8 --out - --records " +
                            quoted(records) + " --quiet > " + null_device;
    const int rc = std::system(cmd.c_str());
    if (rc != 0) throw std::runtime_error("yantra-gen failed (" + std::to_string(rc) + ") on " + scenario.string());
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> opt;
    bool keep_work = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") return usage(std::cout, 0);
        if (a == "--keep-work") {
            keep_work = true;
            continue;
        }
        if (a.rfind("--", 0) != 0 || i + 1 >= argc) return usage(std::cerr, 2);
        opt[a.substr(2)] = argv[++i];
    }
    for (const char* need : {"description", "out", "gen", "scenario-dir", "tetra-plugin"})
        if (!opt.count(need)) return usage(std::cerr, 2);
    try {
        const yd::DemoDescription d = yd::read_description(opt["description"]);
        const fs::path out = opt["out"];
        const fs::path work = opt.count("work") ? fs::path(opt["work"]) : out / "work";
        const double truth_interval = opt.count("truth-interval") ? std::stod(opt["truth-interval"]) : 0.25;
        const unsigned threads = opt.count("threads") ? static_cast<unsigned>(std::stoul(opt["threads"])) : 0;
        fs::create_directories(out);
        const fs::path recordings = out / "recordings";
        fs::remove_all(recordings);
        fs::remove_all(work);
        fs::create_directories(work);

        auto project = yd::create_project(d, out / (d.id + ".yantra"));
        const yantra::building::Building b = project->building();

        // The walks' ground truth, every floor at once.
        std::vector<fs::path> record_files;
        std::vector<std::future<void>> runs;
        for (const yd::DemoFloor& f : d.floors) {
            const auto base = yd::scenario_for_example(opt["scenario-dir"], f.example_id);
            if (!base) throw std::runtime_error("no scenario of " + opt["scenario-dir"] + " lists " + f.example_id);
            const fs::path dir = work / f.id;
            fs::create_directories(dir);
            const fs::path scenario = dir / base->filename();  // the file is named by its id
            std::ofstream(scenario, std::ios::binary)
                << yd::walk_scenario(read_text(*base), b, f, truth_interval) << "\n";
            record_files.push_back(dir / "walk.records.jsonl");
            std::cout << f.name << " (" << f.example_id << "): scenario " << base->stem().string() << "\n";
            runs.push_back(std::async(std::launch::async, run_gen, fs::path(opt["gen"]), scenario, record_files.back()));
        }
        for (auto& r : runs) r.get();

        // The TETRA technology plugin maps the records (PLG-02).
        yantra::plugin::PluginLoader loader{yantra::plugin::PluginLoaderConfig{}};
        const yantra::plugin::PluginEntry e = loader.load_file(opt["tetra-plugin"]);
        if (e.status != yantra::plugin::PluginStatus::Enabled)
            throw std::runtime_error("technology plugin " + opt["tetra-plugin"] + ": " + e.message);
        auto catalog = std::make_shared<yantra::measure::TechnologyCatalog>();
        const auto* info = loader.info(e.id);
        if (!info || !catalog->add(*info, loader.descriptor(e.id), loader.instance(e.id)))
            throw std::runtime_error("technology plugin " + e.id + " cannot map records");

        for (std::size_t i = 0; i < d.floors.size(); ++i) {
            const auto parsed = yantra::measure::read_record_file(record_files[i]);
            if (!parsed.ok())
                throw std::runtime_error(record_files[i].string() + ": " +
                                         (parsed.errors.empty() ? "unreadable" : parsed.errors.front()));
            const yd::WalkRecording w = yd::record_walk(project, d, d.floors[i], *parsed.value, catalog, recordings);
            std::cout << "  " << d.floors[i].walk.name << ": " << w.points << " points, recording "
                      << w.recording_session << " (" << w.files.size() << " file)\n";
        }
        for (const yd::DemoFloor& f : d.floors) {
            yantra::raytrace::RunControl run;
            run.threads = threads;
            const auto r = yantra::survey::interpolate(*project, yd::interpolation_request(*project, d, f), run);
            if (!r) throw std::runtime_error("interpolation of " + f.id + " was cancelled");
            yantra::survey::store(*project, *r);
            std::cout << "  interpolation " << f.id << ": " << r->quality.points << " points, rms "
                      << r->quality.rms_db << " dB\n";
        }
        project->close();
        if (!keep_work) fs::remove_all(work);
        std::cout << "demo project: " << (out / (d.id + ".yantra")).string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "yantra-demo: " << ex.what() << "\n";
        return 1;
    }
}
