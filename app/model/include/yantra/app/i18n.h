// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI languages (UI-12): English (the source texts in the code) and
// Dutch, French and German from Yantra's own string tables (spec 16.2),
// the language files data/i18n/<lang>.json. They are built into the app
// (cmake embeds them in yantra_app_model), so a translation needs no data
// folder at run time and every test sees the same tables.
//
// A UI text is written in English next to its control (DOC-07 f) and
// passed through tr(): tr("Save###file.save") gives "Opslaan###file.save"
// in Dutch (the "###id" part, the stable id of gui.h, is never
// translated). A text with values uses "{}" placeholders and trf():
// trf("Floor {} of {}", {a, b}). A text that is translated later (stored
// in a table, translated where it is drawn) is marked with tr_mark(), so
// the UI lint (tools/lint_ui) finds it. Texts without a translation stay
// English; the lint fails the build on a text missing in a language.
//
// Thread-safe: texts may be translated on any thread (events of the
// measurement thread); the language is switched on the UI thread.
#pragma once

#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::app {

enum class Language { En, Nl, Fr, De, Pseudo };
/// The UI languages a user chooses from (Settings, --language).
inline constexpr Language kLanguages[] = {Language::En, Language::Nl, Language::Fr, Language::De};
/// TST-52: a pseudo-language for the GUI text-overflow test ("pseudo" in
/// the settings and on the command line, not offered in Settings): every
/// English text 40 % longer, in brackets, with accented letters
/// (pseudo_text()), so a layout that has no room for a longer translation
/// fails before that translation exists. Plugin texts follow it too.
inline constexpr Language kPseudoLanguage = Language::Pseudo;

/// "en", "nl", "fr", "de" (settings, plugin translation tables); "pseudo".
const char* language_key(Language language);
/// The language's own name ("English", "Nederlands", "Français",
/// "Deutsch"): shown untranslated, so everyone finds their own.
const char* language_name(Language language);
std::optional<Language> parse_language(std::string_view key);
/// The pseudo-language's form of an English text (TST-52): each line
/// "[" + the text with accented letters + " ~~~" padding + "]", at least
/// 40 % (and two characters) longer; "{}" placeholders, printf
/// conversions, a "##id" suffix and line breaks are kept as they are.
std::string pseudo_text(std::string_view english);

/// The language of tr() (default English).
void set_language(Language language);
Language language();

/// `english` in the current language; the English text when the table
/// has none. A "###id" suffix is kept as it is. The pointer stays valid
/// for the whole run.
#if defined(__GNUC__)
// As gettext: printf-style uses check the English format's arguments.
__attribute__((format_arg(1)))
#endif
const char* tr(const char* english);
std::string tr(const std::string& english);
/// tr() of `english`, then every "{}" replaced by the next argument.
std::string trf(const char* english, std::initializer_list<std::string_view> args);
/// Replaces every "{}" of `pattern` by the next argument (no translation).
std::string fill_placeholders(std::string_view pattern, std::initializer_list<std::string_view> args);
/// Marks an English text for the string tables without translating it
/// (it is translated where it is shown).
constexpr const char* tr_mark(const char* english) { return english; }

/// The table of a language (English: empty): English text -> translation.
const std::map<std::string, std::string, std::less<>>& translations(Language language);
/// Why a built-in language file could not be read ("" when all were).
std::string translation_error();

/// UI lint: from now on tr() notes every text it had no translation for
/// in a language other than English (off by default).
void record_missing_translations(bool on);
/// The texts noted since the last call (sorted, each once), and clears them.
std::vector<std::string> take_missing_translations();

}  // namespace yantra::app
