// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.9 without a GUI: UI languages (UI-12), the settings of the
// language, touch layout, scale, themes and sounds (UI-13, UI-09/10), the
// manual sections of the in-app help (DOC-07 e) and the audible alerts
// with the FakeAudio (UI-09, UI-10).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "yantra/app/app_state.h"
#include "yantra/app/audible_alerts.h"
#include "yantra/app/i18n.h"
#include "yantra/app/manual.h"
#include "yantra/app/settings.h"

using namespace yantra::app;
namespace fs = std::filesystem;

namespace {

// Restores English after a test that switches the language.
struct LanguageGuard {
    ~LanguageGuard() { set_language(Language::En); }
};

SurveyFrame frame_with(const std::string& name, double serving_dbm, const std::string& neighbour = "",
                       double neighbour_dbm = 0.0) {
    SurveyFrame f;
    f.valid = true;
    CellRow s;
    s.name = name;
    s.identity = "la=1;mcc=206;mnc=16";
    s.frequency_hz = 390012500;
    s.level = serving_dbm;
    f.serving = s;
    if (!neighbour.empty()) {
        CellRow n;
        n.name = neighbour;
        n.frequency_hz = 390100000;
        n.level = neighbour_dbm;
        f.neighbours.push_back(n);
    }
    return f;
}

SurveyEvent event(std::int64_t t, EventKind k) {
    SurveyEvent e;
    e.t_ns = t;
    e.kind = k;
    return e;
}

int zero_crossings(const yantra::recording::Audio& a) {
    int n = 0;
    for (std::size_t i = 1; i < a.samples.size(); ++i)
        if ((a.samples[i - 1] < 0) != (a.samples[i] < 0)) ++n;
    return n;
}

}  // namespace

TEST_CASE("UI-12 languages: keys, names and tr() in English") {
    LanguageGuard guard;
    CHECK(std::string(language_key(Language::Nl)) == "nl");
    CHECK(parse_language("de") == Language::De);
    CHECK_FALSE(parse_language("es").has_value());
    CHECK(std::string(language_name(Language::Fr)) == "Français");
    CHECK(translation_error().empty());
    set_language(Language::En);
    const char* t = "Save###file.save";
    CHECK(tr(t) == t);  // English: the text itself
    CHECK(trf("Floor {} of {}", {"2", "3"}) == "Floor 2 of 3");
    CHECK(fill_placeholders("{} and {}", {"a"}) == "a and {}");
    CHECK(std::string(tr_mark("x")) == "x");
}

TEST_CASE("UI-12 tr() keeps the ### id and notes missing texts") {
    LanguageGuard guard;
    set_language(Language::Nl);
    record_missing_translations(true);
    (void)take_missing_translations();
    const char* missing = "A text no table has###test.id";
    CHECK(std::string(tr(missing)) == missing);  // English stays
    CHECK(tr(std::string("Another text without a translation")) == "Another text without a translation");
    const auto m = take_missing_translations();
    REQUIRE(m.size() == 2);
    CHECK(m[0] == "A text no table has");
    record_missing_translations(false);
    // A text the Dutch table has: its "###id" part is kept as it is.
    const auto& nl = translations(Language::Nl);
    if (!nl.empty()) {
        const std::string en = nl.begin()->first;
        CHECK(std::string(tr((en + "###x.y").c_str())) == nl.begin()->second + "###x.y");
        CHECK(tr(en) == nl.begin()->second);
    }
    CHECK(translations(Language::En).empty());
}

TEST_CASE("UI-13, UI-12, UI-09/10 settings: round trip and checks") {
    AppSettings s;
    s.language = "fr";
    s.theme = Theme::HighContrast;
    s.touch = true;
    s.ui_scale = 1.5;
    s.sound.enabled = true;
    s.sound.volume = 0.3;
    s.sound.level_tone = true;
    s.sound.level_interval_s = 5.0;
    s.sound.reselection = false;
    s.sound.alerts = {{"Site A", -90.0}, {"390.0125", -100.0}};
    std::string error;
    const AppSettings back = settings_from_json(settings_to_json(s), error);
    CHECK(error.empty());
    CHECK(back == s);
    CHECK(check_settings(s).empty());
    CHECK(std::string(theme_key(Theme::HighContrast)) == "high_contrast");
    CHECK(parse_theme("high_contrast") == Theme::HighContrast);

    AppSettings bad = s;
    bad.language = "xx";
    CHECK_FALSE(check_settings(bad).empty());
    bad = s;
    bad.ui_scale = 3.0;
    CHECK_FALSE(check_settings(bad).empty());
    bad = s;
    bad.sound.level_interval_s = 1.0;
    CHECK_FALSE(check_settings(bad).empty());
    bad = s;
    bad.sound.alerts.assign(kMaxLevelAlerts + 1, LevelAlert{"x", -90.0});
    CHECK_FALSE(check_settings(bad).empty());
    bad = s;
    bad.sound.alerts[0].cell.clear();
    CHECK_FALSE(check_settings(bad).empty());
    // Values out of range in the file keep their defaults.
    const AppSettings odd = settings_from_json(
        R"({"language":"es","ui_scale":9,"sound":{"volume":4,"alerts":[{"cell":"","threshold_dbm":-90},{"cell":"B","threshold_dbm":-90}]}})",
        error);
    CHECK(odd.language == "en");
    CHECK(odd.ui_scale == 1.0);
    CHECK(odd.sound.volume == doctest::Approx(0.6));
    REQUIRE(odd.sound.alerts.size() == 1);
    CHECK(odd.sound.alerts[0].cell == "B");
}

TEST_CASE("DOC-07 e manual sections: every section has its heading in docs/manual") {
    const std::vector<fs::path> folders = manual_folders({fs::path(YANTRA_TEST_SOURCE_DATA_DIR)});
    for (const ManualSection& m : manual_sections()) {
        CAPTURE(m.number);
        std::string error;
        const std::string text = manual_section_text(m, folders, error);
        CHECK(error.empty());
        CHECK(text.find(std::string(m.number) + " " + m.title) != std::string::npos);
    }
    REQUIRE(find_manual_section("13.2") != nullptr);
    CHECK(find_manual_section("99.9") == nullptr);
    // A chapter's text holds its sections; a section stops at the next one.
    std::string error;
    const std::string chapter = manual_section_text(*find_manual_section("13"), folders, error);
    CHECK(chapter.find("## 13.2") != std::string::npos);
    const std::string section = manual_section_text(*find_manual_section("13.1"), folders, error);
    CHECK(section.find("## 13.2") == std::string::npos);
    // The screenshots are for the HTML/PDF manual, not the in-app help (SCR-07).
    const std::string survey = manual_section_text(*find_manual_section("5.1"), folders, error);
    CHECK(survey.find("Survey panels") != std::string::npos);
    CHECK(survey.find("](../img/") == std::string::npos);
    // No manual: the reason.
    CHECK(manual_section_text(*find_manual_section("1"), {fs::path("/nonexistent")}, error).empty());
    CHECK_FALSE(error.empty());
}

TEST_CASE("DOC-07 e AppState help: sections, contents and refusals") {
    AppPaths paths;
    paths.data_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR)};
    AppState st(AppSettings{}, paths);
    CHECK_FALSE(st.help_view().open);
    REQUIRE(st.trigger(Action::UserManual).ok);
    CHECK(st.help_view().open);
    CHECK(st.help_view().number.empty());
    CHECK(st.help_view().chapters.size() == 17);
    REQUIRE(st.open_help("8.2").ok);
    CHECK(st.help_view().title == "8.2 Verification");
    CHECK(st.help_view().text.find("## 8.2 Verification") == 0);
    CHECK_FALSE(st.open_help("42.1").ok);
    CHECK(st.help_view().number == "8.2");  // unchanged
    st.close_help();
    CHECK_FALSE(st.help_view().open);
}

TEST_CASE("DOC-03 Help > Quick starts: the quick starts in the help window") {
    // Every quick start file is there, whole, without its licence comment
    // and pictures; its title is the file's heading.
    const std::vector<fs::path> folders = quick_start_folders({fs::path(YANTRA_TEST_SOURCE_DATA_DIR)});
    REQUIRE(quick_starts().size() == 3);
    for (const QuickStart& q : quick_starts()) {
        CAPTURE(q.id);
        std::string error;
        const std::string text = quick_start_text(q, folders, error);
        CHECK(error.empty());
        CHECK(text.rfind("# Quick start: ", 0) == 0);
        CHECK(text.find("SPDX") == std::string::npos);
        CHECK(text.find("\n![") == std::string::npos);
        CHECK(text.find("1. ") != std::string::npos);  // the steps
    }
    CHECK(find_quick_start("4") == nullptr);
    std::string error;
    CHECK(quick_start_text(*find_quick_start("1"), {fs::path("/nonexistent")}, error).empty());
    CHECK(error.find("not installed") != std::string::npos);

    AppPaths paths;
    paths.data_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR)};
    AppState st(AppSettings{}, paths);
    REQUIRE(st.trigger(Action::QuickStarts).ok);
    CHECK(st.help_view().open);
    CHECK(st.help_view().quick_starts);
    CHECK(st.help_view().number.empty());
    CHECK(st.help_view().title == "Quick starts");
    REQUIRE(st.help_view().chapters.size() == 3);
    CHECK(st.help_view().chapters[0].second == "First indoor project");
    REQUIRE(st.open_quick_start("2").ok);
    CHECK(st.help_view().title == "Indoor walk test with acceptance report");
    CHECK(st.help_view().text.find("# Quick start: indoor walk test with acceptance report") == 0);
    CHECK(st.help_view().error.empty());
    CHECK_FALSE(st.open_quick_start("9").ok);
    CHECK(st.help_view().number == "2");  // unchanged
    // The manual again: no longer the quick starts.
    REQUIRE(st.open_help("").ok);
    CHECK_FALSE(st.help_view().quick_starts);
    st.close_help();
    CHECK_FALSE(st.help_view().open);
}

TEST_CASE("UI-09 tones: level pitch and clips") {
    CHECK(level_tone_hz(-120.0) == doctest::Approx(300.0));
    CHECK(level_tone_hz(-40.0) == doctest::Approx(1500.0));
    CHECK(level_tone_hz(-200.0) == doctest::Approx(300.0));
    CHECK(level_tone_hz(-80.0) == doctest::Approx(900.0));
    const auto low = alert_clip(AlertSound::LevelTone, -120.0, 1.0, 22050);
    const auto high = alert_clip(AlertSound::LevelTone, -40.0, 1.0, 22050);
    CHECK(low.sample_rate == 22050);
    CHECK(low.seconds() == doctest::Approx(0.18).epsilon(0.01));
    // 0.18 s at 300 Hz: ~108 crossings; at 1500 Hz ~540.
    CHECK(zero_crossings(low) == doctest::Approx(108).epsilon(0.05));
    CHECK(zero_crossings(high) == doctest::Approx(540).epsilon(0.05));
    // Volume 0: silence; every sound has a clip of its own.
    for (std::int16_t v : alert_clip(AlertSound::SyncLost, 0.0, 0.0).samples) CHECK(v == 0);
    CHECK(alert_clip(AlertSound::LevelAlert, 0.0, 0.5).seconds() > alert_clip(AlertSound::Reselection, 0.0, 0.5).seconds());
}

TEST_CASE("UI-09 audible events through the FakeAudio") {
    FakeAudio audio;
    AudibleAlerts a;
    SoundSettings s;
    s.enabled = true;
    a.set_settings(s);
    const SurveyFrame f = frame_with("Site A", -70.0);
    // The first step only takes the history's newest time: no sound for old events.
    a.update({event(100, EventKind::SyncLost)}, f, false, 0.0, audio);
    CHECK(audio.played() == 0);
    a.update({event(100, EventKind::SyncLost), event(200, EventKind::SyncLost)}, f, false, 0.1, audio);
    REQUIRE(audio.played() == 1);
    CHECK(a.played().back() == AlertSound::SyncLost);
    // A sound waits while another plays (a voice note is never cut).
    a.update({event(300, EventKind::ServingChange), event(300, EventKind::SyncBack)}, f, false, 0.2, audio);
    CHECK(audio.played() == 1);
    audio.finish_playback();
    a.update({}, f, false, 0.3, audio);
    audio.finish_playback();
    a.update({}, f, false, 0.4, audio);
    REQUIRE(audio.played() == 3);
    CHECK(a.played()[1] == AlertSound::Reselection);
    CHECK(a.played()[2] == AlertSound::SyncBack);
    audio.finish_playback();
    // Switched off per event.
    s.reselection = false;
    a.set_settings(s);
    a.update({event(400, EventKind::ServingChange)}, f, false, 0.5, audio);
    CHECK(audio.played() == 3);
    // Low battery: once, again after 5 minutes.
    a.update({}, f, true, 1.0, audio);
    CHECK(a.played().back() == AlertSound::LowBattery);
    audio.finish_playback();
    a.update({}, f, true, 100.0, audio);
    CHECK(audio.played() == 4);
    a.update({}, f, true, 301.5, audio);
    CHECK(audio.played() == 5);
    audio.finish_playback();
    // Sounds off: nothing at all.
    s.enabled = false;
    a.set_settings(s);
    a.update({event(500, EventKind::SyncLost)}, f, true, 1000.0, audio);
    CHECK(audio.played() == 5);
}

TEST_CASE("UI-09 level tone at the interval, pitch from the serving level") {
    FakeAudio audio;
    AudibleAlerts a;
    SoundSettings s;
    s.enabled = true;
    s.level_tone = true;
    s.level_interval_s = 10.0;
    a.set_settings(s);
    a.update({}, frame_with("Site A", -40.0), false, 0.0, audio);
    REQUIRE(audio.played() == 1);
    CHECK(zero_crossings(audio.last_played()) == doctest::Approx(540).epsilon(0.05));
    audio.finish_playback();
    a.update({}, frame_with("Site A", -120.0), false, 5.0, audio);
    CHECK(audio.played() == 1);
    a.update({}, frame_with("Site A", -120.0), false, 10.0, audio);
    REQUIRE(audio.played() == 2);
    CHECK(zero_crossings(audio.last_played()) == doctest::Approx(108).epsilon(0.05));
}

TEST_CASE("UI-10 level alerts: popup and sound once, again after 2 dB above") {
    FakeAudio audio;
    AudibleAlerts a;
    SoundSettings s;
    s.enabled = true;
    s.alerts = {{"Site B", -90.0}, {"390.0125", -60.0}};
    a.set_settings(s);
    // Not armed until seen 2 dB above the threshold.
    a.update({}, frame_with("Site A", -70.0, "Site B", -91.0), false, 0.0, audio);
    CHECK(a.popups().empty());
    a.update({}, frame_with("Site A", -70.0, "Site B", -89.0), false, 0.1, audio);
    CHECK(a.popups().empty());
    a.update({}, frame_with("Site A", -70.0, "Site B", -87.5), false, 0.2, audio);
    a.update({}, frame_with("Site A", -70.0, "Site B", -90.5), false, 0.3, audio);
    REQUIRE(a.popups().size() == 1);
    CHECK(a.popups()[0].cell == "Site B");
    CHECK(a.popups()[0].level_dbm == doctest::Approx(-90.5));
    CHECK(a.popups()[0].text.find("Site B dropped to -90.5 dBm") == 0);
    REQUIRE(audio.played() == 1);
    CHECK(a.played().back() == AlertSound::LevelAlert);
    audio.finish_playback();
    // Still low: no second alert; back 2 dB above and down again: one more.
    a.update({}, frame_with("Site A", -70.0, "Site B", -95.0), false, 0.4, audio);
    CHECK(a.popups().size() == 1);
    a.update({}, frame_with("Site A", -70.0, "Site B", -86.0), false, 0.5, audio);
    a.update({}, frame_with("Site A", -70.0), false, 0.6, audio);  // no longer received
    REQUIRE(a.popups().size() == 2);
    CHECK(std::isnan(a.popups()[1].level_dbm));
    CHECK(a.popups()[1].text.find("no longer received") != std::string::npos);
    // By carrier in MHz: the serving cell at 390.0125 MHz.
    a.update({}, frame_with("Site A", -55.0), false, 0.7, audio);
    a.update({}, frame_with("Site A", -61.0), false, 0.8, audio);
    REQUIRE(a.popups().size() == 3);
    CHECK(a.popups()[2].cell == "390.0125");
    a.dismiss_popups();
    CHECK(a.popups().empty());
    // Popups off: only the sound.
    s.alert_popup = false;
    a.set_settings(s);
    audio.finish_playback();
    const int before = audio.played();
    a.update({}, frame_with("Site A", -55.0), false, 0.9, audio);
    a.update({}, frame_with("Site A", -65.0), false, 1.0, audio);
    CHECK(a.popups().empty());
    CHECK(audio.played() == before + 1);
}

TEST_CASE("UI-09 test sound: needs an output, never cuts a playing clip") {
    AudibleAlerts a;
    SoundSettings s;
    s.enabled = true;
    a.set_settings(s);
    std::string error;
    auto none = make_no_audio("No sound device.");
    CHECK_FALSE(a.play_now(AlertSound::SyncLost, 0.0, *none, error));
    CHECK(error == "No sound device.");
    FakeAudio audio;
    CHECK(a.play_now(AlertSound::SyncLost, 0.0, audio, error));
    CHECK_FALSE(a.play_now(AlertSound::SyncBack, 0.0, audio, error));
    CHECK(audio.played() == 1);
}
