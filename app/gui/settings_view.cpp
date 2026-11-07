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

#include <string>

namespace yantra::app {

using namespace gui_detail;

void Gui::draw_settings_general(AppSettings& f) {
    ImGui::SeparatorText("Appearance");
    int theme = f.theme == Theme::Light ? 1 : 0;
    ImGui::RadioButton("Dark###settings.theme.dark", &theme, 0);
    text_tooltip("Dark colours (the default).");
    ImGui::SameLine();
    ImGui::RadioButton("Light###settings.theme.light", &theme, 1);
    text_tooltip("Light colours, for bright rooms and daylight.");
    f.theme = theme == 1 ? Theme::Light : Theme::Dark;
    help_marker("The colour theme; it changes when you press Apply.");

    ImGui::SeparatorText("Help");
    ImGui::Checkbox("Show tooltips###settings.tooltips", &f.tooltips);
    text_tooltip("Show the hover texts of the controls. The (?) markers and the reasons of greyed controls stay.");
    ImGui::BeginDisabled(!f.tooltips);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble("Tooltip delay (s)###settings.tooltip_delay", &f.tooltip_delay_s, 0.1, 0.5, "%.1f");
    text_tooltip("How long the pointer rests on a control before its tooltip shows (0 to 5 s).");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Window system");
    const char* systems[] = {"auto", "x11", "wayland"};
    int ws = 0;
    for (int i = 0; i < 3; ++i)
        if (f.window_system == systems[i]) ws = i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    if (ImGui::Combo("Window system###settings.window_system", &ws, "Automatic\0X11\0Wayland\0")) f.window_system = systems[ws];
    text_tooltip("Which window system Yantra uses on Linux (NFR-10). Applies at the next start.");
    help_marker("Automatic picks Wayland when the desktop runs it, else X11. The command line option --window-system overrides it.");
}

void Gui::draw_settings_survey(AppSettings& f) {
    ImGui::SeparatorText("Coverage");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble("Coverage threshold (dBm)###settings.threshold", &f.survey.threshold_dbm, 1.0, 5.0, "%.0f");
    text_tooltip("The serving level that counts as covered in the statistics and the survey panel (UI-11), -150 to -20 dBm.");

    ImGui::SeparatorText("Live graphs");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble("Graph span (min)###settings.graph_span", &f.survey.graph_span_min, 1.0, 5.0, "%.0f");
    text_tooltip("How many minutes the live level, C1 and quality graphs show (1 to 60). A replay shows the whole recording.");

    ImGui::SeparatorText("Ring buffer");
    ImGui::Checkbox("Ring buffer on when a project opens###settings.ring", &f.survey.ring);
    text_tooltip("Keep the last minutes of measurements while no recording runs, to save them afterwards (REC-09).");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
    ImGui::InputDouble("Ring buffer length (min)###settings.ring_minutes", &f.survey.ring_minutes, 1.0, 5.0, "%.0f");
    text_tooltip("How many minutes the ring buffer keeps (2 to 60). A running ring buffer keeps its length.");
}

void Gui::draw_plugin_manager() {
    const PluginManagerView v = state_.plugin_manager();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted("Switch plugins on or off. A switched-off plugin stays installed and keeps its settings (PLG-09).");
    ImGui::PopTextWrapPos();
    if (v.plugins.empty()) ImGui::TextDisabled("No plugins are installed.");
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_ScrollY;
    if (!v.plugins.empty() &&
        ImGui::BeginTable("###plugins.table", 7, flags, ImVec2(0.0F, ImGui::GetFontSize() * 16.0F))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Plugin");
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("API", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Author");
        ImGui::TableSetupColumn("Licence");
        ImGui::TableSetupColumn("Status");
        ImGui::TableHeadersRow();
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
            a.tooltip = std::string(p.enabled ? "Switch " : "Switch on ") + p.name + (p.enabled ? " off." : ".");
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
    if (ImGui::BeginTabBar("###settings.tabs")) {
        if (ImGui::BeginTabItem("General###settings.tab.general")) {
            draw_settings_general(f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Survey###settings.tab.survey")) {
            draw_settings_survey(f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Plugins###settings.tab.plugins")) {
            draw_plugin_manager();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    const std::string why = check_settings(f);
    if (!why.empty()) ImGui::TextColored(kErrorColour, "%s", why.c_str());
    error_line(state_);
    const bool changed = !(f == state_.settings());
    ImGui::BeginDisabled(!why.empty() || !changed);
    if (dialog_button("Apply###dlg.apply", "Use these settings and keep the window open.")) (void)state_.show(state_.apply_settings(f));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!why.empty());
    if (dialog_button("OK###dlg.ok", "Use these settings and close [Enter]") ||
        (why.empty() && !editing && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        if (state_.show(state_.apply_settings(f)).ok) state_.cancel_dialog();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Close without the changes not applied yet [Esc]") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

void Gui::draw_reset_settings() {
    ResetForm& f = state_.reset_form();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
    ImGui::TextUnformatted("All settings go back to their defaults and the UI restarts as on the first start.");
    ImGui::PopTextWrapPos();
    ImGui::Checkbox("Also reset the calibration tables###reset.calibration", &f.also_calibration);
    text_tooltip("The level calibration of the radios (RF-02). Without it, the calibration stays.");
    const ResetSummary s = state_.reset_summary();
    if (ImGui::BeginTable("###reset.lists", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Reset");
        ImGui::TableSetupColumn("Kept");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        for (const std::string& r : s.reset) ImGui::BulletText("%s", r.c_str());
        ImGui::TableSetColumnIndex(1);
        for (const std::string& k : s.kept) ImGui::BulletText("%s", k.c_str());
        ImGui::EndTable();
    }
    const std::filesystem::path root = state_.reset_backup_root();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
    if (!root.empty())
        ImGui::TextDisabled("A time-stamped backup of the current settings is made in %s first.", root.string().c_str());
    else
        ImGui::TextDisabled("The settings are not saved in this run, so there is nothing to back up.");
    ImGui::PopTextWrapPos();
    error_line(state_);
    if (dialog_button("Reset###dlg.ok", "Back up and reset all settings now.")) (void)state_.show(state_.confirm_reset_settings());
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Keep the settings [Esc]") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

}  // namespace yantra::app
