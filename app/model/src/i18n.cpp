// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI languages, see i18n.h. The language files are embedded by cmake
// (app/CMakeLists.txt, i18n_tables.cpp.in) as pieces of raw text.
#include "yantra/app/i18n.h"

#include <array>
#include <atomic>
#include <mutex>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace yantra::app {

namespace i18n_data {
// Defined in the generated i18n_tables.cpp: the language files' text in
// pieces (a single literal would pass the ISO C++ length limit).
extern const char* const kNl[];
extern const std::size_t kNlCount;
extern const char* const kFr[];
extern const std::size_t kFrCount;
extern const char* const kDe[];
extern const std::size_t kDeCount;
}  // namespace i18n_data

namespace {

using Table = std::map<std::string, std::string, std::less<>>;

struct Tables {
    std::array<Table, 4> table;
    std::string error;
};

std::string join(const char* const* pieces, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s += pieces[i];
    return s;
}

void load(Tables& t, Language lang, const std::string& text) {
    const char* key = language_key(lang);
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        if (j.value("language", std::string()) != key) {
            t.error += std::string("i18n/") + key + ".json: wrong \"language\". ";
            return;
        }
        const auto it = j.find("strings");
        if (it == j.end() || !it->is_object()) {
            t.error += std::string("i18n/") + key + ".json: no \"strings\" object. ";
            return;
        }
        Table& out = t.table[static_cast<std::size_t>(lang)];
        for (const auto& [en, v] : it->items())
            if (v.is_string() && !v.get<std::string>().empty()) out.emplace(en, v.get<std::string>());
    } catch (const std::exception& e) {
        t.error += std::string("i18n/") + key + ".json: " + e.what() + " ";
    }
}

const Tables& tables() {
    static const Tables t = [] {
        Tables x;
        load(x, Language::Nl, join(i18n_data::kNl, i18n_data::kNlCount));
        load(x, Language::Fr, join(i18n_data::kFr, i18n_data::kFrCount));
        load(x, Language::De, join(i18n_data::kDe, i18n_data::kDeCount));
        return x;
    }();
    return t;
}

std::atomic<int> g_language{0};
std::atomic<bool> g_record{false};

// Texts with a "###id" suffix and texts the table lacks need a stable
// string of their own: kept per language for the whole run.
struct Cache {
    std::mutex mutex;
    std::array<std::unordered_map<std::string, std::string>, 4> composed;
    std::set<std::string> missing;
};
Cache& cache() {
    static Cache c;
    return c;
}

const std::string* lookup(Language lang, std::string_view english) {
    const Table& t = tables().table[static_cast<std::size_t>(lang)];
    const auto it = t.find(english);
    return it == t.end() ? nullptr : &it->second;
}

void note_missing(std::string_view english) {
    if (!g_record.load(std::memory_order_relaxed) || english.empty()) return;
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    c.missing.emplace(english);
}

}  // namespace

const char* language_key(Language language) {
    switch (language) {
    case Language::Nl: return "nl";
    case Language::Fr: return "fr";
    case Language::De: return "de";
    case Language::En: break;
    }
    return "en";
}

const char* language_name(Language language) {
    switch (language) {
    case Language::Nl: return "Nederlands";
    case Language::Fr: return "Français";
    case Language::De: return "Deutsch";
    case Language::En: break;
    }
    return "English";
}

std::optional<Language> parse_language(std::string_view key) {
    for (Language l : kLanguages)
        if (key == language_key(l)) return l;
    return std::nullopt;
}

void set_language(Language language) { g_language.store(static_cast<int>(language)); }
Language language() { return static_cast<Language>(g_language.load()); }

const char* tr(const char* english) {
    const Language lang = language();
    if (lang == Language::En || english == nullptr || *english == '\0') return english;
    const std::string_view all(english);
    const std::size_t hash = all.find("###");
    const std::string_view text = hash == std::string_view::npos ? all : all.substr(0, hash);
    if (text.empty()) return english;
    const std::string* t = lookup(lang, text);
    if (t == nullptr) {
        note_missing(text);
        return english;
    }
    if (hash == std::string_view::npos) return t->c_str();
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    auto& m = c.composed[static_cast<std::size_t>(lang)];
    auto it = m.find(english);
    if (it == m.end()) it = m.emplace(english, *t + std::string(all.substr(hash))).first;
    return it->second.c_str();
}

std::string tr(const std::string& english) {
    const Language lang = language();
    if (lang == Language::En || english.empty()) return english;
    const std::size_t hash = english.find("###");
    const std::string_view text = std::string_view(english).substr(0, hash);
    if (text.empty()) return english;
    const std::string* t = lookup(lang, text);
    if (t == nullptr) {
        note_missing(text);
        return english;
    }
    return hash == std::string::npos ? *t : *t + english.substr(hash);
}

std::string fill_placeholders(std::string_view pattern, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(pattern.size() + 16);
    auto arg = args.begin();
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}' && arg != args.end()) {
            out += *arg++;
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::string trf(const char* english, std::initializer_list<std::string_view> args) {
    return fill_placeholders(tr(english), args);
}

const std::map<std::string, std::string, std::less<>>& translations(Language language) {
    return tables().table[static_cast<std::size_t>(language)];
}

std::string translation_error() { return tables().error; }

void record_missing_translations(bool on) { g_record.store(on); }

std::vector<std::string> take_missing_translations() {
    Cache& c = cache();
    const std::lock_guard<std::mutex> lock(c.mutex);
    std::vector<std::string> out(c.missing.begin(), c.missing.end());
    c.missing.clear();
    return out;
}

}  // namespace yantra::app
