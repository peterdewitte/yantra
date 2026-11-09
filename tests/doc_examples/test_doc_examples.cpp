// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DOC-21: every Markdown code block marked `<!-- example: <kind> -->` in
// the repository's documentation is read back with the reader of its
// format (DOC-12, NFR-05), so a documented example cannot drift from the
// code. JSON replies of the remote API (DOC-13) are parsed as JSON; the
// `api-shell` blocks are run by doc_api_examples.py. An unknown kind, a
// mark without a code block or a kind with no example at all fails.
#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/core/building/model_json.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/celldb/csv.h"
#include "yantra/core/celldb/verify.h"
#include "yantra/core/export/csv.h"
#include "yantra/core/export/dxf.h"
#include "yantra/core/survey/acceptance.h"
#ifdef YANTRA_DOC_EXAMPLES_APP
#include "yantra/app/config_profile.h"
#include "yantra/app/plugin_state_file.h"
#include "yantra/app/settings.h"
#include "yantra/app/settings_reset.h"
#endif

namespace fs = std::filesystem;

namespace {

struct Example {
    std::string file;  // relative to the source folder
    int line = 0;      // of the mark
    std::string kind;
    std::string text;
};

const fs::path kSource = YANTRA_DOC_SOURCE_DIR;

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// The Markdown files of the repository: the top folder and docs/ (not
// third_party/, build trees or the submodule, which have their own docs).
std::vector<fs::path> markdown_files() {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(kSource))
        if (e.is_regular_file() && e.path().extension() == ".md") out.push_back(e.path());
    for (const auto& e : fs::recursive_directory_iterator(kSource / "docs"))
        if (e.is_regular_file() && e.path().extension() == ".md") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// Marks and the code block after each; a mark without a block is an
// example with an empty kind's text and `line` set (checked by the test).
std::vector<Example> examples(std::vector<std::string>& problems) {
    const std::string mark = "<!-- example:";
    std::vector<Example> out;
    for (const fs::path& f : markdown_files()) {
        std::istringstream in(slurp(f));
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);) lines.push_back(l);
        const std::string rel = fs::relative(f, kSource).generic_string();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].rfind(mark, 0) != 0) continue;
            const std::size_t end = lines[i].find("-->");
            if (end == std::string::npos) {
                problems.push_back(rel + ":" + std::to_string(i + 1) + ": unterminated example mark");
                continue;
            }
            Example ex;
            ex.file = rel;
            ex.line = static_cast<int>(i + 1);
            std::istringstream k(lines[i].substr(mark.size(), end - mark.size()));
            k >> ex.kind;
            if (i + 1 >= lines.size() || lines[i + 1].rfind("```", 0) != 0) {
                problems.push_back(rel + ":" + std::to_string(i + 1) + ": the mark is not followed by a code block");
                continue;
            }
            std::size_t j = i + 2;
            for (; j < lines.size() && lines[j].rfind("```", 0) != 0; ++j) ex.text += lines[j] + "\n";
            if (j >= lines.size()) {
                problems.push_back(rel + ":" + std::to_string(i + 1) + ": unterminated code block");
                continue;
            }
            out.push_back(std::move(ex));
            i = j;
        }
    }
    return out;
}

fs::path scratch(const std::string& name) {
    const fs::path d = fs::temp_directory_path() / ("yantra_doc_examples_" + std::to_string(getpid())) / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

using Reader = std::function<std::string(const std::string&)>;  // "" = read fine, else why not

std::map<std::string, Reader> readers() {
    std::map<std::string, Reader> r;
    r["json"] = [](const std::string& t) {
        try {
            const nlohmann::json j = nlohmann::json::parse(t);
            return j.is_discarded() ? std::string("not JSON") : std::string{};
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    };
    r["api-shell"] = [](const std::string& t) {
        // Run by doc_api_examples.py; here only: every command is curl.
        return t.find("curl ") == std::string::npos ? std::string("no curl command") : std::string{};
    };
    r["yantra-building"] = [](const std::string& t) {
        yantra::building::Building b;
        std::string error;
        return yantra::building::parse_building(t, b, error) ? std::string{} : error;
    };
    r["yantra-calibration"] = [](const std::string& t) {
        yantra::calibration::Table table;
        std::string error;
        return yantra::calibration::Table::parse(t, table, error) ? std::string{} : error;
    };
    r["yantra-acceptance-profile"] = [](const std::string& t) {
        try {
            (void)yantra::survey::profile_from_json("example", nlohmann::json::parse(t));
            return std::string{};
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    };
    r["yantra-cells"] = [](const std::string& t) {
        try {
            const auto in = yantra::celldb::import_csv(t);
            if (!in.problems.empty()) return in.problems.front();
            return in.entries.empty() ? std::string("no cell") : std::string{};
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    };
    auto csv = [](const std::string& t) {
        try {
            const auto table = yantra::exports::read_csv(t);
            return table.rows.empty() ? std::string("no row") : std::string{};
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    };
    r["csv:points"] = csv;
    r["csv:difference-log"] = [csv](const std::string& t) {
        // The header is the one the verification writes (DB-04).
        // The file has CRLF line ends (the Markdown block shows LF).
        const std::string header = yantra::celldb::log_header();
        const std::string first = t.substr(0, t.find('\n'));
        if (header.substr(0, header.find_first_of("\r\n")) != first) return "header differs from log_header(): " + first;
        return csv(t);
    };
    r["dxf"] = [](const std::string& t) {
        try {
            const auto d = yantra::exports::read_dxf(t);
            // The example is exactly what the writer makes of it.
            return yantra::exports::dxf_text(d) == t ? std::string{} : std::string("dxf_text() differs");
        } catch (const std::exception& e) {
            return std::string(e.what());
        }
    };
#ifdef YANTRA_DOC_EXAMPLES_APP
    r["yantra-app-settings/1"] = [](const std::string& t) {
        std::string error;
        const auto s = yantra::app::settings_from_json(t, error);
        if (!error.empty()) return error;
        return yantra::app::check_settings(s);
    };
    r["yantra-plugin-state"] = [](const std::string& t) {
        const fs::path f = scratch("plugin_state") / "plugin_state.json";
        write(f, t);
        return yantra::app::PluginStateFile(f).load_error();
    };
    r["yantra-profile/1"] = [](const std::string& t) {
        const fs::path d = scratch("profile");
        yantra::app::SettingsFiles files{"", d / "plugin_state.json", d / "calibration.json"};
        yantra::app::AppSettings out;
        std::string error;
        return yantra::app::apply_profile(t, yantra::app::AppSettings{}, files, out, error) ? std::string{} : error;
    };
#endif
    return r;
}

}  // namespace

TEST_CASE("DOC-21: every marked example of the documentation is read back by its reader") {
    std::vector<std::string> problems;
    const std::vector<Example> all = examples(problems);
    for (const std::string& p : problems) FAIL_CHECK(p);
    const auto r = readers();
    std::map<std::string, int> count;
    for (const Example& ex : all) {
        CAPTURE(ex.file);
        CAPTURE(ex.line);
        CAPTURE(ex.kind);
        const auto it = r.find(ex.kind);
#ifndef YANTRA_DOC_EXAMPLES_APP
        if (it == r.end() && ex.kind.rfind("yantra-", 0) == 0) continue;  // app readers: not in this tree
#endif
        if (it == r.end()) {
            FAIL_CHECK("unknown example kind (add its reader to tests/doc_examples)");
            continue;
        }
        const std::string why = it->second(ex.text);
        CHECK_MESSAGE(why.empty(), why);
        ++count[ex.kind];
    }
    // Every reader has at least one example: a mark removed by accident
    // shows up here.
    for (const auto& [kind, reader] : r) {
        CAPTURE(kind);
        CHECK(count[kind] > 0);
    }
    fs::remove_all(fs::temp_directory_path() / ("yantra_doc_examples_" + std::to_string(getpid())));
}

TEST_CASE("DOC-21: a broken example is refused") {
    const auto r = readers();
    CHECK_FALSE(r.at("json")("{\"a\":").empty());
    CHECK_FALSE(r.at("yantra-building")("{}").empty());
    CHECK_FALSE(r.at("yantra-calibration")("{\"format\":\"other\"}").empty());
    CHECK_FALSE(r.at("yantra-acceptance-profile")("{}").empty());
    CHECK_FALSE(r.at("dxf")("garbage").empty());
}
