// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The About dialog's credits (UI-15), see credits.h.
#include "yantra/app/credits.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

/// The cells of a Markdown table row "| a | b |".
std::vector<std::string> cells(const std::string& line) {
    std::vector<std::string> out;
    std::string t = trim(line);
    if (t.size() < 2 || t.front() != '|') return out;
    t = t.substr(1);
    if (!t.empty() && t.back() == '|') t.pop_back();
    std::string cell;
    for (char c : t) {
        if (c == '|') {
            out.push_back(trim(cell));
            cell.clear();
        } else if (c != '`') {
            cell += c;
        }
    }
    out.push_back(trim(cell));
    return out;
}

bool separator_row(const std::vector<std::string>& c) {
    for (const std::string& s : c)
        if (s.find_first_not_of("-: ") != std::string::npos) return false;
    return !c.empty();
}

}  // namespace

Credits parse_credits(const std::string& markdown) {
    Credits out;
    std::istringstream in(markdown);
    std::string line;
    std::vector<CreditRow>* table = nullptr;
    int name_col = -1, licence_col = -1, source_col = -1;
    bool header = false;
    while (std::getline(in, line)) {
        if (line.rfind("## ", 0) == 0) {
            const std::string title = trim(line.substr(3));
            table = title == "Libraries" ? &out.libraries : title == "Data" ? &out.data : nullptr;
            header = false;
            continue;
        }
        if (table == nullptr) continue;
        const std::vector<std::string> c = cells(line);
        if (c.empty()) continue;
        if (!header) {
            name_col = licence_col = source_col = -1;
            for (std::size_t i = 0; i < c.size(); ++i) {
                if (c[i] == "Component" || c[i] == "Item") name_col = static_cast<int>(i);
                if (c[i] == "Licence") licence_col = static_cast<int>(i);
                if (c[i] == "Source") source_col = static_cast<int>(i);
            }
            header = name_col >= 0;
            continue;
        }
        if (separator_row(c)) continue;
        auto at = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < c.size() ? c[static_cast<std::size_t>(i)] : std::string(); };
        CreditRow r{at(name_col), at(licence_col), at(source_col)};
        if (!r.name.empty()) table->push_back(std::move(r));
    }
    return out;
}

std::optional<fs::path> find_third_party(const std::vector<fs::path>& data_folders) {
    std::error_code ec;
    for (const fs::path& f : data_folders) {
        for (const fs::path& p : {f / "THIRD_PARTY.md", f.parent_path() / "THIRD_PARTY.md"})
            if (fs::is_regular_file(p, ec)) return p;
    }
    return std::nullopt;
}

Credits load_credits(const std::vector<fs::path>& data_folders) {
    const auto file = find_third_party(data_folders);
    if (!file) {
        Credits c;
        c.error = "THIRD_PARTY.md is not installed with this copy of Yantra.";
        return c;
    }
    std::ifstream in(*file);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Credits c = parse_credits(text);
    c.file = *file;
    if (c.libraries.empty() && c.data.empty()) c.error = file->string() + " has no Libraries or Data table.";
    return c;
}

}  // namespace yantra::app
