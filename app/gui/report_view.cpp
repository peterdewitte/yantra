// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Export, Report, acceptance profile and sharing dialogs (step 10.4;
// docs/APP.md "Export, report and sharing"; EXP-01, EXP-03..08).
// Drawing only: the model is ReportExport and AppState (app/model).
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "gui.h"
#include "gui_widgets.h"
#include "yantra/app/i18n.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

const char* column_label(exports::Column c) {
    switch (c) {
    case exports::Column::Time: return tr("Time");
    case exports::Column::Floor: return tr("Floor");
    case exports::Column::Position: return tr("Position (m)");
    case exports::Column::PlanPosition: return tr("Position (plan units)");
    case exports::Column::Cell: return tr("Serving cell");
    case exports::Column::Channel: return tr("Channel");
    case exports::Column::Frequency: return tr("Frequency");
    case exports::Column::Level: return tr("Level");
    case exports::Column::C1: return tr("C1");
    case exports::Column::Quality: return tr("Quality");
    case exports::Column::ServingChange: return tr("Serving cell change");
    case exports::Column::Neighbours: return tr("Neighbours");
    case exports::Column::Marker: return tr("Marker");
    case exports::Column::AntennaCorrection: return tr("Antenna correction");
    case exports::Column::Label: return tr("Label");
    case exports::Column::Kind: return tr("Kind of point");
    case exports::Column::Session: return tr("Session");
    }
    return "";
}

/// "File: <path>" with room for the `button` after it on the line: a long
/// path is shortened with "..." (the whole path on hover, TST-52).
void path_line(const char* label, const std::filesystem::path& p, const char* button) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const std::string s = p.empty() ? std::string(tr("(none)")) : p.string();
    const float room = ImGui::GetContentRegionAvail().x - label_w(button) - ImGui::GetStyle().ItemSpacing.x;
    text_ellipsis(s.c_str(), std::max(room, em(40.0F)), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    text_tooltip(s.c_str());
}

void ok_cancel(AppState& state, const char* ok, const char* ok_tip, Result (AppState::*confirm)()) {
    ImGui::Spacing();
    if (dialog_button(ok, ok_tip, 180.0F) || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        (void)(state.*confirm)();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without writing anything [Esc]")) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state.cancel_dialog();
    error_line(state);
}

void share_choice(AppState& state, bool& share) {
    ImGui::Checkbox(tr("Send the files when written###out.share"), &share);
    text_tooltip(tr("EXP-07: copy the files to the shared folder, upload them or e-mail them with the sharing "
                    "settings. Nothing is sent unless you tick this."));
    ImGui::SameLine();
    if (small_button(tr("Sharing settings...###out.sharing"))) (void)state.open_sharing();
    text_tooltip(tr("Where the files are sent: a shared folder, an upload address or your own mail server."));
}

void job_line(const ReportExport& out) {
    const OutputJobView j = out.job();
    if (!j.running) return;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s", j.what.c_str());
    ImGui::ProgressBar(j.progress < 0 ? 0.0F : static_cast<float>(j.progress), ImVec2(-1.0F, 0.0F), buf);
}

}  // namespace

void Gui::draw_export_dialog() {
    ReportExport* out = state_.output();
    if (out == nullptr) {
        state_.cancel_dialog();
        return;
    }
    ExportForm& f = out->export_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("The points of the value shown in the project window, as a table, a CAD drawing or a "
                              "picture; or every recording of a folder at once."));
    ImGui::PopTextWrapPos();
    const ExportForm::Kind before = f.kind;
    int kind = static_cast<int>(f.kind);
    ImGui::RadioButton(tr("CSV table###ex.csv"), &kind, 0);
    text_tooltip(tr("EXP-01: one row per point with the columns you choose, for a spreadsheet."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("DXF drawing###ex.dxf"), &kind, 1);
    text_tooltip(tr("EXP-03: the floor's walls, antennas and coloured points on layers, in metres, for CAD."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Floor picture###ex.image"), &kind, 2);
    text_tooltip(tr("EXP-08: the floor plan with the coloured points and the legend as PNG or JPG."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Batch###ex.batch"), &kind, 3);
    text_tooltip(tr("EXP-04: every recording in a folder tree, merged into one file or one file each."));
    f.kind = static_cast<ExportForm::Kind>(kind);
    if (f.kind != before && f.kind != ExportForm::Kind::Batch) {
        // The file follows the kind's extension.
        std::filesystem::path p = f.file.empty() ? out->suggested_export_file(f.kind) : f.file;
        p.replace_extension(out->suggested_export_file(f.kind).extension());
        f.file = p;
    }

    if (f.kind != ExportForm::Kind::Batch) {
        ImGui::Checkbox(tr("All floors###ex.all_floors"), &f.all_floors);
        text_tooltip(f.kind == ExportForm::Kind::Csv
                         ? tr("On: the points of every floor in one file. Off: the floor shown in the window.")
                         : tr("On: one file per floor (the floor's id is added to the name). Off: the floor shown."));
    }
    if (f.kind == ExportForm::Kind::Csv || f.kind == ExportForm::Kind::Batch) {
        ImGui::SeparatorText(tr("Columns"));
        const auto all = exports::all_columns();
        f.columns.resize(all.size(), false);
        // Three columns where the longest label fits, else fewer (UI-13).
        float widest = 0.0F;
        for (exports::Column c : all) widest = std::max(widest, ImGui::CalcTextSize(column_label(c)).x);
        widest += ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x * 2.0F;
        const int cols = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / widest), 1, 3);
        if (ImGui::BeginTable("##ex.columns", cols)) {
            for (std::size_t i = 0; i < all.size(); ++i) {
                ImGui::TableNextColumn();
                bool on = f.columns[i];
                if (ImGui::Checkbox((std::string(column_label(all[i])) + "###ex.col." + exports::column_id(all[i])).c_str(),
                                    &on))
                    f.columns[i] = on;
                text_tooltip(tr("Write this column group into the CSV file, in this order."));
            }
            ImGui::EndTable();
        }
        const char* seps[] = {tr("Comma ,"), tr("Semicolon ;"), tr("Tab"), tr("Bar |")};
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
        ImGui::Combo(tr("Separator###ex.separator"), &f.separator, seps, 4);
        text_tooltip(tr("A semicolon suits spreadsheets that use a decimal comma."));
        ImGui::SameLine();
        ImGui::Checkbox(tr("Local time###ex.local_time"), &f.local_time);
        text_tooltip(tr("Off: the time in UTC (ISO 8601). On: the computer's local time with its offset."));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
        ImGui::InputInt(tr("Neighbours###ex.neighbours"), &f.neighbours);
        f.neighbours = std::clamp(f.neighbours, 0, 32);
        text_tooltip(tr("How many neighbour cells get their columns (0 to 32), strongest first."));
    }
    if (f.kind == ExportForm::Kind::Image) {
        ImGui::Checkbox(tr("Coverage map under the points###ex.map"), &f.image_map);
        text_tooltip(tr("The newest stored interpolation of the value shown, in the same colours."));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
        ImGui::InputInt(tr("Size (pixels)###ex.px"), &f.image_px, 100, 400);
        f.image_px = std::clamp(f.image_px, 100, 8000);
        text_tooltip(tr("The longer side of the plan in the picture, 100 to 8000 pixels."));
    }
    if (f.kind == ExportForm::Kind::Batch) {
        ImGui::SeparatorText(tr("Batch export"));
        std::string folder = f.batch_folder.string();
        if (input_text(tr("Folder of recordings###ex.batch_folder"), folder)) f.batch_folder = folder;
        text_tooltip(tr("Every recording in this folder and its subfolders is exported."));
        ImGui::Checkbox(tr("One file per recording###ex.batch_separate"), &f.batch_separate);
        text_tooltip(tr("Off: all recordings in one file. On: one file per recording in the output folder."));
        std::vector<std::string> formats = {tr("CSV (Yantra)")};
        for (const std::string& s : out->batch_formats()) formats.push_back(s);
        std::vector<const char*> items;
        for (const std::string& s : formats) items.push_back(s.c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0F);
        ImGui::Combo(tr("Format###ex.batch_format"), &f.batch_format, items.data(), static_cast<int>(items.size()));
        text_tooltip(tr("CSV, or a format of an exporter plugin (PLG-02)."));
    }
    ImGui::SeparatorText(tr("File"));
    path_line(f.kind == ExportForm::Kind::Batch ? (f.batch_separate ? tr("Output folder:") : tr("Output file:")) : tr("File:"),
              f.kind == ExportForm::Kind::Batch ? f.batch_output : f.file, tr("Browse...###ex.browse"));
    ImGui::SameLine();
    if (small_button(tr("Browse...###ex.browse"))) state_.browse(BrowsePurpose::ExportFile);
    text_tooltip(tr("Choose where the export is written."));
    share_choice(state_, f.share);
    job_line(*out);
    ok_cancel(state_, tr("Export###dlg.ok"), tr("Write the export [Enter]"), &AppState::confirm_export);
}

void Gui::draw_report_dialog() {
    ReportExport* out = state_.output();
    if (out == nullptr) {
        state_.cancel_dialog();
        return;
    }
    ReportForm& f = out->report_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("The coverage and acceptance report of the project: per floor the plan with every "
                              "measured point, the coverage map, the statistics and the acceptance profiles."));
    ImGui::PopTextWrapPos();
    input_text(tr("Title###rp.title"), f.title);
    text_tooltip(tr("Empty: \"Indoor coverage report\"."));
    input_text(tr("Surveyor###rp.surveyor"), f.surveyor);
    text_tooltip(tr("Who measured, as printed on the report. Typed for this report only, never stored."));
    float thr = static_cast<float>(f.threshold);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
    if (ImGui::InputFloat(tr("Threshold (dBm)###rp.threshold"), &thr, 1.0F, 5.0F, "%.1f")) f.threshold = thr;
    text_tooltip(tr("The coverage figures count the points at or above this level."));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
    input_text(tr("More thresholds###rp.more"), f.more_thresholds, 128);
    text_tooltip(tr("Further levels for the coverage table, separated by commas, e.g. -85, -105."));
    float bin = static_cast<float>(f.histogram_bin);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
    if (ImGui::InputFloat(tr("Histogram bin (dB)###rp.bin"), &bin, 0.5F, 1.0F, "%.1f")) f.histogram_bin = bin;
    text_tooltip(tr("The width of a bar of the level histogram, 0.5 to 20 dB."));
    ImGui::SameLine();
    ImGui::Checkbox(tr("Table of every point###rp.points"), &f.point_table);
    text_tooltip(tr("Adds the table of all points of each floor (long for big floors)."));

    ImGui::SeparatorText(tr("Floors"));
    const auto floors = out->floor_names();
    f.floors.resize(floors.size(), true);
    for (std::size_t i = 0; i < floors.size(); ++i) {
        bool on = f.floors[i];
        if (ImGui::Checkbox((floors[i].second + "###rp.floor." + floors[i].first).c_str(), &on)) f.floors[i] = on;
        text_tooltip(tr("Give this floor its section in the report."));
    }
    ImGui::SeparatorText(tr("Acceptance profiles"));
    ImGui::Checkbox(tr("Choose the profiles###rp.choose"), &f.choose_profiles);
    text_tooltip(tr("Off: every profile stored in the project (or the default one). On: the ticked ones."));
    ImGui::SameLine();
    if (small_button(tr("Edit profiles...###rp.edit_profiles"))) (void)state_.open_profiles();
    text_tooltip(tr("Add, change or remove the acceptance profiles of the project (EXP-06)."));
    const auto names = out->profile_names();
    f.profiles.resize(names.size(), true);
    ImGui::BeginDisabled(!f.choose_profiles);
    for (std::size_t i = 0; i < names.size(); ++i) {
        bool on = f.profiles[i];
        if (ImGui::Checkbox((names[i] + "###rp.profile." + std::to_string(i)).c_str(), &on)) f.profiles[i] = on;
        disabled_tooltip(tr("Evaluate this acceptance profile in the report."), f.choose_profiles,
                         tr("Tick \"Choose the profiles\" first; otherwise every stored profile is used."));
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText(tr("File"));
    ImGui::Checkbox(tr("PDF###rp.pdf"), &f.pdf);
    text_tooltip(tr("The report as a PDF file, for printing and sending."));
    ImGui::SameLine();
    ImGui::Checkbox(tr("HTML###rp.html"), &f.html);
    text_tooltip(tr("The HTML file is the same report as one page with its pictures inside."));
    path_line(tr("File:"), f.file, tr("Browse...###rp.browse"));
    ImGui::SameLine();
    if (small_button(tr("Browse...###rp.browse"))) state_.browse(BrowsePurpose::ReportFile);
    text_tooltip(tr("Choose the report's file; the HTML file gets the same name with .html."));
    share_choice(state_, f.share);
    job_line(*out);
    ok_cancel(state_, tr("Make report###dlg.ok"), tr("Write the report in the background [Enter]"),
              &AppState::confirm_report);
}

void Gui::draw_profiles_dialog() {
    ReportExport* out = state_.output();
    if (out == nullptr) {
        state_.cancel_dialog();
        return;
    }
    ProfileForm& f = out->profile_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("An acceptance profile passes when enough of the floor reaches each level: of the "
                              "points, of the tested area (squares) or of the interpolated floor area."));
    ImGui::PopTextWrapPos();
    if (f.profiles.empty()) ImGui::TextDisabled(tr("No profile stored: reports use the default profile."));
    for (std::size_t i = 0; i < f.profiles.size(); ++i) {
        const bool bad = i < f.problems.size() && !f.problems[i].empty();
        const std::string label = (f.profiles[i].name.empty() ? std::string(tr("(no name)")) : f.profiles[i].name) +
                                  (bad ? " (!)" : "") + "###ap.item." + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), f.selected == static_cast<int>(i), 0, ImVec2(0.0F, target_h())))
            f.selected = static_cast<int>(i);
        text_tooltip(bad ? tr("This profile has a problem: select it to see it.") : tr("Select the profile to change it."));
    }
    if (small_button(tr("Add###ap.add"))) out->add_profile();
    text_tooltip(tr("A new profile like the default one."));
    ImGui::SameLine();
    ImGui::BeginDisabled(f.selected < 0);
    if (small_button(tr("Remove###ap.remove"))) (void)out->remove_profile(f.selected);
    disabled_tooltip(tr("Remove the selected profile from the list."), f.selected >= 0, tr("Select a profile first."));
    ImGui::EndDisabled();
    if (f.selected >= 0 && static_cast<std::size_t>(f.selected) < f.profiles.size()) {
        survey::AcceptanceProfile& p = f.profiles[static_cast<std::size_t>(f.selected)];
        ImGui::SeparatorText(tr("Profile"));
        bool changed = input_text(tr("Name###ap.name"), p.name, 128);
        text_tooltip(tr("The profile's name in the report; each name once."));
        int basis = static_cast<int>(p.basis);
        const char* bases[] = {tr("Points"), tr("Tested area"), tr("Floor area")};
        ImGui::SetNextItemWidth(combo_w({bases[0], bases[1], bases[2]}));
        if (ImGui::Combo(tr("Basis###ap.basis"), &basis, bases, 3)) {
            p.basis = static_cast<survey::AcceptanceProfile::Basis>(basis);
            changed = true;
        }
        text_tooltip(tr("Points: the share of the measured points. Tested area: of the squares walked. Floor area: of "
                        "the interpolated map."));
        if (p.basis == survey::AcceptanceProfile::Basis::TestedArea) {
            float sq = static_cast<float>(p.square_m);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
            if (ImGui::InputFloat(tr("Square (m)###ap.square"), &sq, 0.5F, 1.0F, "%.1f")) {
                p.square_m = sq;
                changed = true;
            }
            text_tooltip(tr("The side of a tested square in metres."));
            float pc = static_cast<float>(p.square_percentile);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
            if (ImGui::InputFloat(tr("Percentile of a square###ap.percentile"), &pc, 5.0F, 10.0F, "%.0f")) {
                p.square_percentile = pc;
                changed = true;
            }
            text_tooltip(tr("The value of a square: this percentile of its points (50 = the median)."));
        }
        if (p.basis == survey::AcceptanceProfile::Basis::FloorArea) {
            float c = static_cast<float>(p.min_confidence);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
            if (ImGui::InputFloat(tr("Least confidence###ap.confidence"), &c, 0.05F, 0.1F, "%.2f")) {
                p.min_confidence = c;
                changed = true;
            }
            text_tooltip(tr("Only map cells at least this confident (0 to 1) count."));
        }
        ImGui::TextUnformatted(tr("Criteria: at least the share of the level"));
        for (std::size_t k = 0; k < p.criteria.size(); ++k) {
            const std::string n = std::to_string(k);
            float t = static_cast<float>(p.criteria[k].threshold);
            float m = static_cast<float>(p.criteria[k].min_percent);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
            if (ImGui::InputFloat((std::string(tr("dBm")) + "###ap.thr." + n).c_str(), &t, 1.0F, 5.0F, "%.1f")) {
                p.criteria[k].threshold = t;
                changed = true;
            }
            text_tooltip(tr("The level the criterion counts at or above."));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0F);
            if (ImGui::InputFloat((std::string(tr("%")) + "###ap.pct." + n).c_str(), &m, 1.0F, 5.0F, "%.1f")) {
                p.criteria[k].min_percent = m;
                changed = true;
            }
            text_tooltip(tr("The least share in percent that must reach the level."));
            ImGui::SameLine();
            const bool remove = small_button((tr("Remove###ap.crit_remove.") + n).c_str());
            text_tooltip(tr("Remove this criterion."));
            if (remove) {
                p.criteria.erase(p.criteria.begin() + static_cast<std::ptrdiff_t>(k));
                changed = true;
                break;
            }
        }
        const bool add = small_button(tr("Add criterion###ap.crit_add"));
        text_tooltip(tr("Another level with its least share."));
        if (add) {
            p.criteria.push_back(survey::AcceptanceCriterion{-95.0, 95.0});
            changed = true;
        }
        if (changed) (void)out->check_profiles();
        if (static_cast<std::size_t>(f.selected) < f.problems.size())
            for (const std::string& why : f.problems[static_cast<std::size_t>(f.selected)])
                ImGui::TextColored(kErrorColour, "%s", why.c_str());
    }
    ok_cancel(state_, tr("Save###dlg.ok"), tr("Store the profiles in the project [Enter]"), &AppState::confirm_profiles);
}

void Gui::draw_sharing_dialog() {
    SharingSettings& s = state_.sharing_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("Where exports and reports are sent when you ask for it (EXP-07). Only the files, the "
                              "addresses and the login you type here are sent."));
    ImGui::PopTextWrapPos();
    int m = static_cast<int>(s.method);
    ImGui::RadioButton(tr("Off###sh.off"), &m, 0);
    text_tooltip(tr("Nothing is sent."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Shared folder###sh.folder"), &m, 1);
    text_tooltip(tr("Copy the files into a shared folder."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("Upload (HTTP)###sh.http"), &m, 2);
    text_tooltip(tr("Upload each file to a web address."));
    ImGui::SameLine();
    ImGui::RadioButton(tr("E-mail (SMTP)###sh.smtp"), &m, 3);
    text_tooltip(tr("Send one e-mail with the files through your own mail server."));
    s.method = static_cast<SharingSettings::Method>(m);
    switch (s.method) {
    case SharingSettings::Method::Off: break;
    case SharingSettings::Method::Folder: {
        std::string folder = s.folder.string();
        if (input_text(tr("Folder###sh.folder_path"), folder)) s.folder = folder;
        text_tooltip(tr("A shared network folder mounted on this computer; the files are copied into it."));
        break;
    }
    case SharingSettings::Method::Http:
        input_text(tr("Upload address###sh.url"), s.url);
        text_tooltip(tr("Each file is sent with an HTTP PUT to this address followed by the file name (a WebDAV "
                        "folder or your own upload service)."));
        break;
    case SharingSettings::Method::Smtp:
        input_text(tr("Mail server###sh.server"), s.smtp_server);
        text_tooltip(tr("Your own mail server, e.g. smtp://mail.example.org:587 (encrypted when offered) or "
                        "smtps://mail.example.org."));
        input_text(tr("From###sh.from"), s.mail_from);
        text_tooltip(tr("The sender's e-mail address."));
        input_text(tr("To###sh.to"), s.mail_to);
        text_tooltip(tr("One or more addresses, separated by commas."));
        break;
    }
    if (s.method == SharingSettings::Method::Http || s.method == SharingSettings::Method::Smtp) {
        input_text(tr("Login###sh.user"), s.user, 128);
        text_tooltip(tr("Empty: no login."));
        std::string& pw = state_.sharing_password();
        std::string buf = pw;
        buf.resize(std::max<std::size_t>(128, pw.size() + 1), '\0');
        if (ImGui::InputText(tr("Password###sh.password"), buf.data(), buf.size(), ImGuiInputTextFlags_Password))
            pw = std::string(buf.c_str());
        text_tooltip(tr("Kept until Yantra closes, never stored."));
    }
    if (s.method != SharingSettings::Method::Off) {
        ImGui::Checkbox(tr("Send the floor's CSV when a walk test ends###sh.walk_end"), &s.on_walk_end);
        text_tooltip(tr("When a walk test stops, its floor's points are exported as CSV and sent."));
    }
    ok_cancel(state_, tr("OK###dlg.ok"), tr("Store the sharing settings [Enter]"), &AppState::confirm_sharing);
}

}  // namespace yantra::app
