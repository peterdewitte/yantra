// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI languages, see i18n.h. The language files are embedded by cmake
// (app/CMakeLists.txt, i18n_tables.cpp.in) as pieces of raw text.
#include "yantra/app/i18n.h"

#include <algorithm>
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
    std::array<Table, 5> table;  // En (empty), Nl, Fr, De, Pseudo (empty)
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
    std::array<std::unordered_map<std::string, std::string>, 5> composed;
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
    case Language::Pseudo: return "pseudo";
    case Language::En: break;
    }
    return "en";
}

const char* language_name(Language language) {
    switch (language) {
    case Language::Nl: return "Nederlands";
    case Language::Fr: return "Français";
    case Language::De: return "Deutsch";
    case Language::Pseudo: return "Pseudo (+40 %)";
    case Language::En: break;
    }
    return "English";
}

std::optional<Language> parse_language(std::string_view key) {
    for (Language l : kLanguages)
        if (key == language_key(l)) return l;
    if (key == language_key(kPseudoLanguage)) return kPseudoLanguage;
    return std::nullopt;
}

namespace {

// A letter's accented form (Latin-1, so the UI font has it); nullptr: kept.
const char* accented(char c) {
    switch (c) {
    case 'a': return "\u00e1";
    case 'e': return "\u00e9";
    case 'i': return "\u00ed";
    case 'o': return "\u00f6";
    case 'u': return "\u00fc";
    case 'c': return "\u00e7";
    case 'n': return "\u00f1";
    case 'y': return "\u00fd";
    case 'A': return "\u00c1";
    case 'E': return "\u00c9";
    case 'I': return "\u00cd";
    case 'O': return "\u00d6";
    case 'U': return "\u00dc";
    case 'C': return "\u00c7";
    case 'N': return "\u00d1";
    default: return nullptr;
    }
}

std::string pseudo_line(std::string_view line) {
    if (line.empty()) return {};
    std::string body;
    std::size_t chars = 0;  // code points shown (a placeholder counts as one)
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '{' && i + 1 < line.size() && line[i + 1] == '}') {
            body += "{}";
            ++i;
            ++chars;
        } else if (c == '%' && i + 1 < line.size()) {
            // A printf conversion: flags, width, precision, length, type.
            std::size_t j = i + 1;
            while (j < line.size() && std::string_view("-+ #0123456789.*hlLzjt").find(line[j]) != std::string_view::npos) ++j;
            if (j < line.size()) ++j;
            body.append(line.substr(i, j - i));
            i = j - 1;
            ++chars;
        } else if (const char* a = accented(c)) {
            body += a;
            ++chars;
        } else {
            body += c;
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++chars;
        }
    }
    const std::size_t extra = std::max<std::size_t>(2, (chars * 4 + 9) / 10);
    // The padding is a word of its own (a long word is a translation's
    // matter, not the padding's), unless it is a single character.
    if (extra <= 3) return "[" + body + std::string(extra - 2, '~') + "]";
    return "[" + body + " " + std::string(extra - 3, '~') + "]";
}

}  // namespace

std::string pseudo_text(std::string_view english) {
    const std::size_t ids = english.find("##");
    const std::string_view text = english.substr(0, ids);
    std::string out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        out += pseudo_line(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        if (nl == std::string_view::npos) break;
        out += '\n';
        start = nl + 1;
    }
    if (ids != std::string_view::npos) out.append(english.substr(ids));
    return out;
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
    if (lang == Language::Pseudo) {
        Cache& c = cache();
        const std::lock_guard<std::mutex> lock(c.mutex);
        auto& m = c.composed[static_cast<std::size_t>(lang)];
        auto it = m.find(english);
        if (it == m.end()) it = m.emplace(english, pseudo_text(all)).first;
        return it->second.c_str();
    }
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
    if (lang == Language::Pseudo) return pseudo_text(english);
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
