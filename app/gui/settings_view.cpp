// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings window (File > Settings..., Ctrl+,) with its tabs General,
// Survey and Plugins (the plugin manager, PLG-09), and the Reset all
// settings dialog (Help > Reset all settings, UI-16). The model is
// AppState::settings_form / apply_settings / plugin_manager /
// set_plugin_enabled / reset_form / confirm_reset_settings. Ids:
// docs/APP.md "About, settings, reset and plugin UI".
#include "gui.h"
#include "gui_widgets.h"

#include <imgui.h>

#include <cfloat>
#include <iterator>
#include <string>

#include "yantra/app/i18n.h"

namespace yantra::app {

using namespace gui_detail;

void Gui::draw_settings_general(AppSettings& f) {
    ImGui::SeparatorText(tr("Language"));
    int lang = 0;
    for (std::size_t i = 0; i < std::size(kLanguages); ++i)
        if (f.language == language_key(kLanguages[i])) lang = static_cast<int>(i);
    ImGui::SetNextItemWidth(px(160.0F));
    if (ImGui::BeginCombo(tr("Language###settings.language"), language_name(kLanguages[lang]))) {
        for (std::size_t i = 0; i < std::size(kLanguages); ++i) {
            const std::string item = std::string(language_name(kLanguages[i])) + "###settings.language." +
                                     language_key(kLanguages[i]);
            if (ImGui::Selectable(item.c_str(), static_cast<int>(i) == lang)) f.language = language_key(kLanguages[i]);
            text_tooltip(tr("Show Yantra in this language."));
        }
        ImGui::EndCombo();
    }
    text_tooltip(tr("The language of menus, windows, help texts and plugin windows (UI-12)."));
    help_marker(tr("English, Dutch, French or German. Names of cells, floors and projects stay as you typed them. "
                   "See manual 15.1."));

    ImGui::SeparatorText(tr("Appearance"));
    int theme = static_cast<int>(f.theme);
    ImGui::RadioButton(tr("Dark###settings.theme.dark"), &theme, static_cast<int>(Theme::Dark));
    text_tooltip(tr("Dark colours, for dark rooms (the default)."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Light###settings.theme.light"), &theme, static_cast<int>(Theme::Light));
    text_tooltip(tr("Light colours, for bright rooms and daylight."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("High contrast###settings.theme.high_contrast"), &theme, static_cast<int>(Theme::HighContrast));
    text_tooltip(tr("White on black with yellow highlights, readable in sunlight on bright sites."));
    f.theme = static_cast<Theme>(theme);
    help_marker(tr("The colour theme (UI-13); it changes when you press Apply. See manual 15.1."));
    ImGui::Checkbox(tr("Touch layout###settings.touch"), &f.touch);
    text_tooltip(tr("Large controls of at least 48 pixels with more space between them, for 7 to 12 inch tablets."));
    double percent = f.ui_scale * 100.0;
    ImGui::SetNextItemWidth(px(120.0F));
    if (ImGui::InputDouble(tr("UI scale (%)###settings.ui_scale"), &percent, 5.0, 25.0, "%.0f")) f.ui_scale = percent / 100.0;
    text_tooltip(tr("Makes text and controls larger or smaller (75 to 250 %), on top of the display's own scaling."));

    ImGui::SeparatorText(tr("Help"));
    ImGui::Checkbox(tr("Show tooltips###settings.tooltips"), &f.tooltips);
    text_tooltip(tr("Show the hover texts of the controls. The (?) markers and the reasons of greyed controls stay."));
    ImGui::BeginDisabled(!f.tooltips);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble(tr("Tooltip delay (s)###settings.tooltip_delay"), &f.tooltip_delay_s, 0.1, 0.5, "%.1f");
    text_tooltip(tr("How long the pointer rests on a control before its tooltip shows (0 to 5 s)."));
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr("Window system"));
    const char* systems[] = {"auto", "x11", "wayland"};
    int ws = 0;
    for (int i = 0; i < 3; ++i)
        if (f.window_system == systems[i]) ws = i;
    ImGui::SetNextItemWidth(combo_w({tr("Automatic"), "X11", "Wayland"}));
    if (ImGui::Combo(tr("Window system###settings.window_system"), &ws, (std::string(tr("Automatic")) + '\0' + "X11" + '\0' + "Wayland" + '\0').c_str()))
        f.window_system = systems[ws];
    text_tooltip(tr("Which window system Yantra uses on Linux (NFR-10). Applies at the next start."));
    help_marker(tr("Automatic picks Wayland when the desktop runs it, else X11. The command line option --window-system overrides it."));

    // NFR-06: the applied settings, the plugin and device settings and the
    // calibration table to one file and back.
    ImGui::SeparatorText(tr("Profiles"));
    if (ImGui::Button(tr("Save profile...###settings.profile.save"), ImVec2(0.0F, target_h())))
        state_.browse(BrowsePurpose::ProfileSave);
    text_tooltip(tr("Save the applied settings, the plugin and radio device settings and the calibration table to a file."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Load profile...###settings.profile.load"), ImVec2(0.0F, target_h())))
        state_.browse(BrowsePurpose::ProfileLoad);
    text_tooltip(tr("Use the settings of a profile file. The open project closes and the plugins load again."));
}

void Gui::draw_settings_survey(AppSettings& f) {
    ImGui::SeparatorText(tr("Coverage"));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble(tr("Coverage threshold (dBm)###settings.threshold"), &f.survey.threshold_dbm, 1.0, 5.0, "%.0f");
    text_tooltip(tr("The serving level that counts as covered in the statistics and the survey panel (UI-11), -150 to -20 dBm."));

    ImGui::SeparatorText(tr("Live graphs"));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble(tr("Graph span (min)###settings.graph_span"), &f.survey.graph_span_min, 1.0, 5.0, "%.0f");
    text_tooltip(tr("How many minutes the live level, C1 and quality graphs show (1 to 60). A replay shows the whole recording."));

    ImGui::SeparatorText(tr("Ring buffer"));
    ImGui::Checkbox(tr("Ring buffer on when a project opens###settings.ring"), &f.survey.ring);
    text_tooltip(tr("Keep the last minutes of measurements while no recording runs, to save them afterwards (REC-09)."));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble(tr("Ring buffer length (min)###settings.ring_minutes"), &f.survey.ring_minutes, 1.0, 5.0, "%.0f");
    text_tooltip(tr("How many minutes the ring buffer keeps (2 to 60). A running ring buffer keeps its length."));
}

void Gui::draw_settings_sound(AppSettings& f) {
    SoundSettings& s = f.sound;
    ImGui::Checkbox(tr("Sounds on###settings.sound"), &s.enabled);
    text_tooltip(tr("Play the sounds below during a live survey (UI-09, UI-10). The popups of level alerts show "
                    "also without sound."));
    help_marker(tr("Yantra plays tones: there is no speech. The level tone is higher for a stronger level "
                   "(300 Hz at -120 dBm to 1500 Hz at -40 dBm). See manual 5.5."));
    ImGui::BeginDisabled(!s.enabled);
    int volume = static_cast<int>(s.volume * 100.0 + 0.5);
    ImGui::SetNextItemWidth(px(200.0F));
    if (ImGui::SliderInt(tr("Volume (%)###settings.volume"), &volume, 0, 100)) s.volume = volume / 100.0;
    ActionView need_sound;
    need_sound.enabled = s.enabled;
    need_sound.disabled_reason = tr("Switch the sounds on first.");
    need_sound.tooltip = tr("The loudness of every sound (0 to 100 %).");
    item_tooltip(need_sound);

    ImGui::SeparatorText(tr("Events"));
    struct Row {
        const char* label;
        bool* on;
        const char* tip;
        AlertSound sound;
        const char* test;
    };
    const Row rows[] = {
        {tr("Level tone###settings.level_tone"), &s.level_tone,
         tr("The serving level as a tone at an interval: the higher the tone, the stronger the level."),
         AlertSound::LevelTone, "###settings.test.level"},
        {tr("Loss of sync###settings.sound_sync"), &s.sync_loss,
         tr("Falling tones when the radio loses the serving cell's sync, rising ones when it is back."),
         AlertSound::SyncLost, "###settings.test.sync_lost"},
        {tr("Cell reselection###settings.sound_reselection"), &s.reselection,
         tr("Two short tones when the serving cell changes."), AlertSound::Reselection, "###settings.test.reselection"},
        {tr("Low battery###settings.sound_battery"), &s.low_battery,
         tr("Two low tones when the battery is under 15 % and not charging, again every 5 minutes."),
         AlertSound::LowBattery, "###settings.test.low_battery"},
    };
    for (const Row& r : rows) {
        ImGui::Checkbox(r.label, r.on);
        need_sound.tooltip = r.tip;
        item_tooltip(need_sound);
        ImGui::SameLine(px(260.0F));
        ImGui::PushID(r.test);
        if (ImGui::Button((std::string(tr("Test")) + r.test).c_str())) (void)state_.show(state_.test_sound(r.sound));
        ImGui::PopID();
        need_sound.tooltip = tr("Play this sound once now.");
        item_tooltip(need_sound);
    }
    ImGui::BeginDisabled(!s.level_tone);
    ImGui::SetNextItemWidth(px(120.0F));
    ImGui::InputDouble(tr("Level tone every (s)###settings.level_interval"), &s.level_interval_s, 1.0, 10.0, "%.0f");
    ActionView interval;
    interval.enabled = s.enabled && s.level_tone;
    interval.tooltip = tr("Seconds between two level tones (2 to 120).");
    interval.disabled_reason = s.enabled ? tr("Switch the level tone on first.") : tr("Switch the sounds on first.");
    item_tooltip(interval);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr("Level alerts"));
    help_marker(tr("Up to 8 cells, each with a threshold: when a cell drops below it, or is no longer received after "
                   "it was above it, a popup and a sound tell you. Give the cell's name from the cell database, its "
                   "identity as the panels show it, or its carrier in MHz. See manual 5.5."));
    ImGui::Checkbox(tr("Popup on an alert###settings.alert_popup"), &s.alert_popup);
    text_tooltip(tr("Show a popup besides the sound when a level alert fires."));
    int remove = -1;
    for (std::size_t i = 0; i < s.alerts.size(); ++i) {
        LevelAlert& a = s.alerts[i];
        const std::string n = std::to_string(i);
        ImGui::SetNextItemWidth(px(240.0F));
        input_text((std::string(tr("Cell")) + "###settings.alert.cell." + n).c_str(), a.cell, 128);
        text_tooltip(tr("The cell's name, its identity text or its carrier in MHz (e.g. 390.0125)."));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(120.0F));
        ImGui::InputDouble((std::string(tr("dBm")) + "###settings.alert.threshold." + n).c_str(), &a.threshold_dbm, 1.0,
                           5.0, "%.0f");
        text_tooltip(tr("The alert fires when the cell's level drops below this (-150 to -20 dBm)."));
        ImGui::SameLine();
        if (ImGui::Button((std::string(tr("Remove")) + "###settings.alert.remove." + n).c_str())) remove = static_cast<int>(i);
        text_tooltip(tr("Remove this alert."));
    }
    if (remove >= 0) s.alerts.erase(s.alerts.begin() + remove);
    const bool full = s.alerts.size() >= kMaxLevelAlerts;
    ImGui::BeginDisabled(full);
    if (ImGui::Button(tr("Add alert###settings.alert.add"))) s.alerts.push_back(LevelAlert{"", f.survey.threshold_dbm});
    ImGui::EndDisabled();
    ActionView add;
    add.enabled = !full;
    add.tooltip = tr("Watch one more cell (at most 8).");
    add.disabled_reason = tr("There are 8 alerts already: remove one first.");
    item_tooltip(add);
}

void Gui::draw_plugin_manager() {
    const PluginManagerView v = state_.plugin_manager();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("Switch plugins on or off. A switched-off plugin stays installed and keeps its settings (PLG-09)."));
    ImGui::PopTextWrapPos();
    if (v.plugins.empty()) ImGui::TextDisabled(tr("No plugins are installed."));
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollY;
    if (!v.plugins.empty() &&
        ImGui::BeginTable("###plugins.table", 7, flags, ImVec2(0.0F, ImGui::GetFontSize() * 16.0F))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("On"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(tr("Plugin"));
        ImGui::TableSetupColumn(tr("Version"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(tr("API"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(tr("Author"));
        ImGui::TableSetupColumn(tr("Licence"));
        ImGui::TableSetupColumn(tr("Status"));
        headers_row();
        for (std::size_t i = 0; i < v.plugins.size(); ++i) {
            const PluginRowView& p = v.plugins[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool on = p.enabled;
            ImGui::BeginDisabled(!p.can_toggle);
            const std::string id = "###plugins.toggle." + std::to_string(i);
            if (ImGui::Checkbox(id.c_str(), &on)) (void)state_.show(state_.set_plugin_enabled(p.radio, p.id, on));
            ImGui::EndDisabled();
            ActionView a;
            a.tooltip = std::string(p.enabled ? tr("Switch ") : tr("Switch on ")) + p.name + (p.enabled ? " off." : ".");
            a.enabled = p.can_toggle;
            a.disabled_reason = p.toggle_reason;
            item_tooltip(a);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(p.name.c_str());
            text_tooltip((p.id + " (" + p.kind + ")" + (p.description.empty() ? "" : ": " + p.description)).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(p.version.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(p.api.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextWrapped("%s", p.author.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextWrapped("%s", p.licence.c_str());
            ImGui::TableSetColumnIndex(6);
            if (p.message.empty()) {
                ImGui::TextUnformatted(p.status.c_str());
            } else {
                ImGui::TextColored(kBannerColour, "%s", p.status.c_str());
                text_tooltip(p.message.c_str());
            }
        }
        ImGui::EndTable();
    }
    if (!v.note.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", v.note.c_str());
        ImGui::PopTextWrapPos();
    }
}

void Gui::draw_settings() {
    AppSettings& f = state_.settings_form();
    // Enter in a field only ends the edit; Enter elsewhere presses OK.
    const bool editing = ImGui::IsAnyItemActive();
    // A tab's content scrolls when it is higher than the app window leaves
    // room for, so Apply, OK and Cancel stay in view (UI-13 touch layout).
    auto tab = [&](const char* label, const char* tip, const auto& body) {
        if (!tab_item(label, tip)) return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float used = ImGui::GetCursorScreenPos().y - vp->WorkPos.y;
        const float footer = target_h() + ImGui::GetFrameHeightWithSpacing() * 3.0F;
        const float max_h = std::max(px(120.0F), vp->WorkSize.y * 0.94F - used - footer);
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0F, 0.0F), ImVec2(FLT_MAX, max_h));
        if (ImGui::BeginChild("###settings.body", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AutoResizeY)) {
            wrap_texts();
            body();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    };
    if (ImGui::BeginTabBar("###settings.tabs")) {
        tab(tr("General###settings.tab.general"), tr("Language, appearance, touch layout and tooltips."), [&] { draw_settings_general(f); });
        tab(tr("Survey###settings.tab.survey"), tr("Coverage threshold, graphs and the ring buffer."), [&] { draw_settings_survey(f); });
        tab(tr("Sound###settings.tab.sound"), tr("Audible events, the level tone and level alerts."), [&] { draw_settings_sound(f); });
        tab(tr("Plugins###settings.tab.plugins"), tr("Switch plugins on or off."), [&] { draw_plugin_manager(); });
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    const std::string why = check_settings(f);
    if (!why.empty()) ImGui::TextColored(kErrorColour, "%s", why.c_str());
    error_line(state_);
    const bool changed = !(f == state_.settings());
    ImGui::BeginDisabled(!why.empty() || !changed);
    if (ImGui::Button(tr("Apply###dlg.apply"), ImVec2(button_w(tr("Apply###dlg.apply"), 140.0F), target_h())))
        (void)state_.show(state_.apply_settings(f));
    ImGui::EndDisabled();
    disabled_tooltip(tr("Use these settings and keep the window open."), why.empty() && changed,
                     !why.empty() ? why : std::string(tr("Nothing changed yet.")));
    ImGui::SameLine();
    ImGui::BeginDisabled(!why.empty());
    if (dialog_button(tr("OK###dlg.ok"), tr("Use these settings and close [Enter]")) ||
        (why.empty() && !editing && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        if (state_.show(state_.apply_settings(f)).ok) state_.cancel_dialog();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without the changes not applied yet [Esc]")) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

void Gui::draw_reset_settings() {
    ResetForm& f = state_.reset_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("All settings go back to their defaults and the UI restarts as on the first start."));
    ImGui::PopTextWrapPos();
    ImGui::Checkbox(tr("Also reset the calibration tables###reset.calibration"), &f.also_calibration);
    text_tooltip(tr("The level calibration of the radios (RF-02). Without it, the calibration stays."));
    const ResetSummary s = state_.reset_summary();
    if (ImGui::BeginTable("###reset.lists", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(tr("Reset"));
        ImGui::TableSetupColumn(tr("Kept"));
        headers_row();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        for (const std::string& r : s.reset) {
            ImGui::Bullet();
            ImGui::TextUnformatted(r.c_str());  // wraps in its column (TST-52)
        }
        ImGui::TableSetColumnIndex(1);
        for (const std::string& k : s.kept) {
            ImGui::Bullet();
            ImGui::TextUnformatted(k.c_str());
        }
        ImGui::EndTable();
    }
    const std::filesystem::path root = state_.reset_backup_root();
    ImGui::PushTextWrapPos(0.0F);
    if (!root.empty())
        ImGui::TextDisabled(tr("A time-stamped backup of the current settings is made in %s first."), root.string().c_str());
    else
        ImGui::TextDisabled(tr("The settings are not saved in this run, so there is nothing to back up."));
    ImGui::PopTextWrapPos();
    error_line(state_);
    if (dialog_button(tr("Reset###dlg.ok"), tr("Back up and reset all settings now."))) (void)state_.show(state_.confirm_reset_settings());
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Keep the settings [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

}  // namespace yantra::app
