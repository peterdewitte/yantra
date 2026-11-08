// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-demo: makes a bundled demo project (DEMO-05, DEMO-06, DEMO-13;
// docs/DEMO.md) from its description, e.g. data/demo/fraunces/fraunces.json:
//
//   yantra-demo --description FILE --out DIR --gen YANTRA_GEN
//               --scenario-dir DIR --tetra-plugin TETRA_SO
//               [--work DIR] [--truth-interval S] [--threads N] [--keep-work]
//               [--sheets DIR | --reference-sheets]
//   yantra-demo --sample-report DIR --project FILE --description FILE
//               --tetra-plugin TETRA_SO --data DIR [--compare DIR] [--work DIR]
//
// --sample-report writes the sample report of a built demo project
// (DEMO-06): DIR/<id>_report.pdf and DIR/<id>_report.html, composed as
// the app's report dialog does with its defaults (the demo metric, the
// stored or default acceptance profile, the verification of the replayed
// recordings, the DEMO label, no surveyor) and dated SOURCE_DATE_EPOCH.
// --compare DIR then compares them with the committed ones in DIR within
// report::CompareTolerance (exit 1 beyond it); the ctest and CI use it,
// the target sample_report writes docs/samples.
//
// Licence-restricted sheets (the MAK example buildings, DEMO-12) are
// referenced by SHA-256, never copied: --sheets DIR checks them in DIR
// (the user's cache), --reference-sheets makes the project from the
// description alone (the build, which never has the files; DEMO-13).
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
#include <yantra/core/celldb/technology.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/report/compare.h>
#include <yantra/core/report/coverage.h>
#include <yantra/core/survey/interpolation.h>

namespace fs = std::filesystem;
namespace yd = yantra::demo;

namespace {

int usage(std::ostream& out, int code) {
    out << "usage: yantra-demo --description FILE --out DIR --gen YANTRA_GEN --scenario-dir DIR\n"
           "                   --tetra-plugin TETRA_SO [--work DIR] [--truth-interval S]\n"
           "                   [--threads N] [--keep-work] [--sheets DIR | --reference-sheets]\n"
           "       yantra-demo --sample-report DIR --project FILE --description FILE\n"
           "                   --tetra-plugin TETRA_SO --data DIR [--compare DIR]\n"
           "Makes the demo project of FILE (format yantra-demo-project) in DIR: DIR/<id>.yantra\n"
           "and DIR/recordings/ (DEMO-05, DEMO-06; docs/DEMO.md); --sample-report writes and\n"
           "compares the sample report of a built demo project (DEMO-06).\n";
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

// The TETRA technology plugin and its catalog (PLG-02).
struct Tetra {
    yantra::plugin::PluginLoader loader{yantra::plugin::PluginLoaderConfig{}};
    std::shared_ptr<yantra::measure::TechnologyCatalog> catalog = std::make_shared<yantra::measure::TechnologyCatalog>();
    std::string id;
    explicit Tetra(const fs::path& so) {
        const yantra::plugin::PluginEntry e = loader.load_file(so);
        if (e.status != yantra::plugin::PluginStatus::Enabled)
            throw std::runtime_error("technology plugin " + so.string() + ": " + e.message);
        id = e.id;
        const auto* info = loader.info(e.id);
        if (!info || !catalog->add(*info, loader.descriptor(e.id), loader.instance(e.id)))
            throw std::runtime_error("technology plugin " + e.id + " cannot map records");
    }
};

// DEMO-06: the sample report of a built demo project, and its comparison
// with the committed one.
int sample_report(std::map<std::string, std::string>& opt) {
    for (const char* need : {"sample-report", "project", "description", "tetra-plugin", "data"})
        if (!opt.count(need)) return usage(std::cerr, 2);
    const yd::DemoDescription d = yd::read_description(opt["description"]);
    const fs::path out = opt["sample-report"];
    const fs::path project_file = opt["project"];
    // The project file is live SQLite: the report reads a copy (with its
    // recordings), so the built project is never opened.
    const fs::path work = opt.count("work") ? fs::path(opt["work"]) : out / "work";
    fs::create_directories(out);
    fs::remove_all(work);
    fs::create_directories(work);
    fs::copy_file(project_file, work / project_file.filename());
    if (fs::exists(project_file.parent_path() / "recordings"))
        fs::copy(project_file.parent_path() / "recordings", work / "recordings", fs::copy_options::recursive);
    const fs::path pdf = out / (d.id + "_report.pdf");
    const fs::path html = out / (d.id + "_report.html");
    {
        auto project = yantra::project::Project::open(work / project_file.filename());
        Tetra tetra(opt["tetra-plugin"]);
        const auto tech = yantra::celldb::Technology::from_plugin(
            *tetra.loader.info(tetra.id), tetra.loader.descriptor(tetra.id), tetra.loader.instance(tetra.id));
        if (!tech) throw std::runtime_error("technology plugin " + tetra.id + " has no cell identity");
        const yantra::celldb::Technologies techs{*tech};
        std::vector<std::string> warnings;
        yantra::report::CoverageOptions o;
        o.data_folders = {fs::path(opt["data"])};
        o.metric = yd::demo_metric(*project, d);
        o.technologies = &techs;
        o.observed = yantra::report::observed_cells(*project, tetra.catalog, techs, &warnings);
        o.demo_label = d.label;
        o.created_utc_ns = yantra::report::creation_time(0);
        const auto rep = yantra::report::write_coverage_report(*project, o, pdf, html);
        for (const auto& w : warnings) std::cout << "warning: " << w << "\n";
        for (const auto& w : rep.warnings) std::cout << "warning: " << w << "\n";
        std::cout << "sample report: " << pdf.string() << ", " << html.string() << " (" << rep.floors.size()
                  << " floors)\n";
    }
    fs::remove_all(work);
    if (!opt.count("compare")) return 0;
    const fs::path ref = opt["compare"];
    bool within = true;
    for (const fs::path& f : {pdf, html}) {
        const auto r = yantra::report::compare_report_files(f, ref / f.filename());
        std::cout << f.filename().string() << ": "
                  << (r.identical ? "identical" : r.within ? "within the tolerance" : "DIFFERS") << "\n";
        for (const auto& line : r.differences) std::cout << "  " << line << "\n";
        within = within && r.within;
    }
    if (!within)
        std::cout << "The sample report differs from " << ref.string()
                  << ". An intended change: regenerate it (ninja sample_report) and commit it (DEMO-06).\n";
    return within ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> opt;
    bool keep_work = false;
    bool reference_sheets = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") return usage(std::cout, 0);
        if (a == "--keep-work") {
            keep_work = true;
            continue;
        }
        if (a == "--reference-sheets") {
            reference_sheets = true;
            continue;
        }
        if (a.rfind("--", 0) != 0 || i + 1 >= argc) return usage(std::cerr, 2);
        opt[a.substr(2)] = argv[++i];
    }
    if (opt.count("sample-report")) {
        try {
            return sample_report(opt);
        } catch (const std::exception& ex) {
            std::cerr << "yantra-demo: " << ex.what() << "\n";
            return 1;
        }
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

        yd::CreateOptions co;
        if (opt.count("sheets")) co.sheet_folder = opt["sheets"];
        co.require_restricted_sheets = !reference_sheets;
        if (reference_sheets && opt.count("sheets")) return usage(std::cerr, 2);
        auto project = yd::create_project(d, out / (d.id + ".yantra"), co);
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
        Tetra tetra(opt["tetra-plugin"]);
        const auto& catalog = tetra.catalog;

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
