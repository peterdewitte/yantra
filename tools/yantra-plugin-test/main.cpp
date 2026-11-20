// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-plugin-test: the plugin test kit command (PLG-13). See kUsage.
// Copied from yantra-v2 68ea20a: tools/yantra-plugin-test/main.cpp (option
// parsing, exit codes), adapted to the record-based kit.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "kit.h"

namespace {

const char* const kUsage = R"(usage:
  yantra-plugin-test check --plugin FILE.so [--records FILE.jsonl]...
                           [--radio RADIO.so [--device KEY] [--count N] [--timeout S]]
                           [--golden FILE.json [--update]] [--output FILE.json]
                           [--json REPORT.json] [--work-dir DIR]
                           [--level-offset-db DB] [--ms-power-dbm DBM]
                           [--call-limit-ms MS] [--threads N]
                           [--strict-translations] [--tolerance REL]
      Loads the plugin through Yantra's plugin loader and checks its
      descriptor, licence, schemas, band plan, UI and translations. Record
      files (radio record files) or the live records of a radio
      plugin (for example the simulated one of the technology) are mapped to
      the plugin's technology schemas and fed to its rules, feature and
      exporters on a measurement thread; the threading rules are checked
      on the way. --golden compares the output with a golden file
      (record files only); --update writes it instead (on purpose only).
      --json writes the machine-readable report.
  yantra-plugin-test info --plugin FILE.so
      Prints what the plugin declares.

Exit codes: 0 pass, 1 a check failed, 2 usage or file error.
)";

int usage_error(const std::string& message) {
    std::cerr << "error: " << message << "\n\n" << kUsage;
    return yantra::kit::kExitError;
}

bool number(const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return !text.empty() && end != nullptr && *end == '\0';
}

}  // namespace

int main(int argc, char** argv) {
    namespace kit = yantra::kit;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout << kUsage;
        return args.empty() ? kit::kExitError : kit::kExitPass;
    }
    const std::string command = args[0];
    if (command != "check" && command != "info") return usage_error("unknown command " + command);
    kit::Options o;
    std::filesystem::path report_path;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--update") {
            o.update = true;
            continue;
        }
        if (a == "--strict-translations") {
            o.strict_translations = true;
            continue;
        }
        if (i + 1 >= args.size()) return usage_error("unknown option or missing value: " + a);
        const std::string v = args[++i];
        double x = 0.0;
        const bool is_number = number(v, x);
        if (a == "--plugin") o.plugin = v;
        else if (a == "--records") o.records.emplace_back(v);
        else if (a == "--radio") o.radio = v;
        else if (a == "--device") o.device = v;
        else if (a == "--golden") o.golden = v;
        else if (a == "--output") o.output = v;
        else if (a == "--json") report_path = v;
        else if (a == "--work-dir") o.work_dir = v;
        else if (!is_number) return usage_error("not a number: " + a + " " + v);
        else if (a == "--count" && x >= 1) o.radio_records = static_cast<std::size_t>(x);
        else if (a == "--timeout" && x > 0) o.radio_timeout_s = x;
        else if (a == "--level-offset-db") {
            o.calibrated = true;
            o.level_offset_db = x;
        } else if (a == "--ms-power-dbm") o.ms_power_dbm = x;
        else if (a == "--call-limit-ms" && x > 0) o.call_limit_ms = x;
        else if (a == "--threads" && x >= 1) o.purity_threads = static_cast<unsigned>(x);
        else if (a == "--tolerance" && x >= 0) o.rel_tolerance = x;
        else return usage_error("unknown option or bad value: " + a + " " + v);
    }
    if (o.plugin.empty()) return usage_error("--plugin is required");
    if (!std::filesystem::exists(o.plugin))
        return usage_error("plugin file " + o.plugin.string() + " does not exist");
    if (command == "info") return kit::info(o.plugin, std::cout, std::cerr);

    const kit::Report report = kit::run(o);
    kit::print(report, std::cout);
    if (!report_path.empty()) {
        std::ofstream f(report_path, std::ios::binary);
        f << report.to_json().dump(2) << "\n";
        if (!f) {
            std::cerr << "error: cannot write " << report_path.string() << "\n";
            return kit::kExitError;
        }
    }
    const kit::Check* opts = report.find("options");
    if (opts != nullptr && opts->result == kit::Result::Fail) return kit::kExitError;
    return report.passed() ? kit::kExitPass : kit::kExitFail;
}
