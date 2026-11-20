// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin test kit (PLG-13): loads a plugin of the plugin API
// (plugin-api/include/yantra/plugin.h) through Yantra's own PluginLoader and checks
// it the way Yantra will use it:
//   - load, API version, descriptor and licence (PLG-03, PLG-04, PLG-10);
//   - its schemas and band plan, and that its technology schemas describe
//     the records of its radios (check_radio_schema, PLG-05);
//   - feeds it recorded measurement files (radio record files,
//     RAD-65) or the live records of a (simulated) radio plugin (PLG-11)
//     through the generic radio -> technology mapping, on a measurement
//     thread, and calls its technology rules (channel mapping, C1,
//     reselection, values_match, neighbour links), its feature's
//     on_record and its exporters;
//   - threading rules (PLG-07): calls on a measurement thread, host
//     services refused there, a time limit per call, and the purity of
//     the technology rules (the same answers from several threads);
//   - its UI (PLG-06, DOC-07): every window drawn through a checking
//     drawing table, every menu entry, and the translations (UI-12);
//   - its output against a golden file (deterministic inputs only).
// Every check is reported as pass, fail or skip in a machine-readable
// JSON report. Radio plugins are checked with the radio test kit of
// the radio parts (RAD-91), not with this kit.
// Copied from yantra-v2 68ea20a: tools/yantra-plugin-test/kit.h (exit
// codes, golden comparison), rewritten for plugin API 1.0 and records.
#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace yantra::kit {

// Exit codes of the command.
inline constexpr int kExitPass = 0;
inline constexpr int kExitFail = 1;   // a check failed (or the output differs from the golden file)
inline constexpr int kExitError = 2;  // usage or file error

struct Options {
    std::filesystem::path plugin;               // the plugin's shared library
    std::vector<std::filesystem::path> records; // record files (RAD-65) to feed, in order
    std::filesystem::path radio;                // radio plugin whose live records are fed
    std::string device;                         // its device key; "" = the first
    std::size_t radio_records = 20;             // records to take from the radio
    double radio_timeout_s = 30.0;
    std::filesystem::path golden;               // expected output; "" = no comparison
    bool update = false;                        // write the golden file instead of comparing
    std::filesystem::path output;               // also write the actual output here
    std::filesystem::path work_dir;             // export files; "" = a temporary folder
    bool calibrated = false;                    // levels: dBm = dBFS + offset (else UNCALIBRATED)
    double level_offset_db = 0.0;
    double ms_power_dbm = 30.0;                 // MS class power for C1 and reselection
    double call_limit_ms = 100.0;               // longest allowed plugin call (PLG-07)
    unsigned purity_threads = 4;                // threads of the purity check
    bool strict_translations = false;           // a missing translation fails instead of warns
    double rel_tolerance = 1e-6;                // golden comparison of numbers
    double abs_tolerance = 1e-9;
};

enum class Result { Pass, Fail, Skip };
const char* result_name(Result r) noexcept;

struct Check {
    std::string name;
    Result result = Result::Pass;
    std::vector<std::string> messages;  // why it failed or was skipped, or what it saw
};

struct Report {
    std::string plugin;     // the file
    std::string plugin_id;  // "" when it did not load
    std::vector<Check> checks;
    std::vector<std::string> warnings;
    nlohmann::json output;  // the plugin's output (golden-file content)
    bool passed() const;
    const Check* find(const std::string& name) const;
    nlohmann::json to_json() const;  // the machine-readable report
};

// Runs every check. Usage problems (missing files, --golden with a live
// radio) come back as a failed "options" check.
Report run(const Options& options);

// Writes the report as text (one line per check) to `out`.
void print(const Report& report, std::ostream& out);

// Prints what the plugin declares (descriptor, kinds, schemas, bands, UI).
int info(const std::filesystem::path& plugin, std::ostream& out, std::ostream& err);

// Compares two output documents; numbers match within
// abs + rel * max(|a|, |b|). Appends one line per difference (JSON
// pointer and both values) and returns true when there is none.
bool compare(const nlohmann::json& expected, const nlohmann::json& actual, double rel_tolerance,
             double abs_tolerance, std::vector<std::string>& differences);

}  // namespace yantra::kit
