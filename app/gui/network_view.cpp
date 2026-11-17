// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Network menu's windows (step 9.8; docs/APP.md "Cell database,
// network checks and post-processing"): the cell database editor (DB-01,
// DB-02), the verification with auto-learn, difference log and neighbour
// cross-check (DB-03..05) and the reselection simulator (DB-08). Drawing
// only: the models are CellDbEditor and NetworkChecks (app/model).
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"

namespace yantra::app {

using namespace gui_detail;

namespace {

ImVec4 rgb_colour(std::uint32_t rgb) {
    return ImVec4(static_cast<float>((rgb >> 16) & 0xff) / 255.0F, static_cast<float>((rgb >> 8) & 0xff) / 255.0F,
                  static_cast<float>(rgb & 0xff) / 255.0F, 1.0F);
}

bool multiline_text(const char* label, std::string& s, float height, std::size_t capacity = 8192) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputTextMultiline(label, buf.data(), buf.size(), ImVec2(-1.0F, height));
    if (changed) s = std::string(buf.c_str());
    return changed;
}

}  // namespace

void Gui::draw_network_windows() {
    NetworkWindows& nw = state_.network_windows();
    if (nw.cell_db && state_.cell_db() != nullptr) {
        bool open = true;
        if (begin_tool_window("Cell database", "win.cell_db", open, 60.0F, 30.0F, 900.0F, 4000.0F)) {
            help_button("8.1");
            draw_cell_db(*state_.cell_db());
        }
        ImGui::End();
        if (!open) nw.cell_db = false;
    }
    if (nw.verification && state_.network_checks() != nullptr) {
        bool open = true;
        if (begin_tool_window("Verification", "win.verification", open, 90.0F, 80.0F, 820.0F, 700.0F)) {
            help_button("8.2");
            draw_verification(*state_.network_checks());
        }
        ImGui::End();
        if (!open) nw.verification = false;
    }
    if (nw.reselection && state_.network_checks() != nullptr) {
        bool open = true;
        if (begin_tool_window("Reselection simulator", "win.reselection", open, 120.0F, 110.0F, 640.0F, 560.0F)) {
            help_button("8.3");
            draw_reselection(*state_.network_checks());
        }
        ImGui::End();
        if (!open) nw.reselection = false;
    }
}

// -- cell database (DB-01, DB-02) -------------------------------------------------------

void Gui::draw_cell_db(CellDbEditor& ed) {
    error_line(state_);
    input_text(tr("Filter###cells.filter"), network_.filter, 128);
    text_tooltip(tr("Show the cells whose name, site, carrier, channel or identity holds this text."));
    const std::vector<std::size_t> shown = ed.filtered(network_.filter);
    const std::vector<CellRowView>& rows = ed.rows();
    ImGui::SameLine();
    ImGui::TextDisabled(tr("%zu of %zu cells"), shown.size(), rows.size());
    const float list_h = std::max(140.0F, ImGui::GetContentRegionAvail().y * 0.38F);
    if (ImGui::BeginTable("###cells.table", 7,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0F, list_h))) {
        // Columns as wide as their texts; the list scrolls sideways when the
        // window is narrower (TST-52: a value is never cut in its column).
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char* h : {tr("Cell"), tr("Technology"), tr("Site / sector"), tr("Carrier"), tr("Channel"), tr("Identity"), tr("Neighbours")})
            ImGui::TableSetupColumn(h);
        headers_row();
        for (std::size_t i : shown) {
            const CellRowView& r = rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = r.name + "###cells.row." + r.id;
            if (ImGui::Selectable(label.c_str(), ed.selected() == r.id, ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0.0F, row_h())))
                (void)state_.show(ed.select(r.id));
            text_tooltip((tr("Id ") + r.id + ", " + r.origin).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.technology.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s%s%s", r.site.c_str(), r.sector.empty() ? "" : " / ", r.sector.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.carrier.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.channel.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.identity.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%zu", r.neighbours);
        }
        ImGui::EndTable();
    }
    if (rows.empty())
        ImGui::TextWrapped(tr("No cells yet: add one, import a CSV file, or learn them from the air (Network > "
                           "Verification)."));

    // New entry, one button per technology.
    for (const std::string& t : ed.technologies()) {
        const std::string b = tr("New ") + t + " cell###cells.new." + t;
        if (ImGui::Button(b.c_str())) (void)state_.show(ed.new_entry(t));
        text_tooltip(tr("Start a new cell of this technology; its key and verified fields are listed empty."));
        ImGui::SameLine();
    }
    if (!ed.selected().empty()) {
        if (ImGui::Button(tr("Remove###cells.remove"))) (void)state_.show(ed.remove(ed.selected()));
        text_tooltip(tr("Remove the selected cell."));
    }
    ImGui::NewLine();

    if (section_header(tr("Entry###cells.entry"), tr("The selected entry, or a new one: its fields."), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!ed.editing()) {
            ImGui::TextDisabled(tr("Select a cell in the list, or start a new one."));
        } else {
            CellForm& f = ed.form();
            ImGui::Text("%s %s (%s)", f.is_new ? tr("New cell") : tr("Cell"), f.id.c_str(), f.technology.c_str());
            // The buttons first: the field list of a technology can be long.
            ImGui::SameLine();
            if (ImGui::Button(tr("Apply###cells.apply"))) (void)state_.show(ed.apply());
            text_tooltip(tr("Store the entry in the project."));
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel###cells.cancel"))) ed.cancel();
            text_tooltip(tr("Forget the changes in the form."));
            ImGui::PushItemWidth(em(260.0F));
            if (f.is_new) {
                input_text(tr("Id###cells.form.id"), f.id, 64);
                text_tooltip(tr("Leave empty to number it (cell-<n>)."));
            }
            input_text(tr("Name###cells.form.label"), f.label, 128);
            input_text(tr("Site###cells.form.site"), f.site_name, 128);
            input_text(tr("Sector###cells.form.sector"), f.sector, 32);
            input_text(tr("Azimuth (degrees)###cells.form.azimuth"), f.azimuth, 32);
            input_text(tr("Colour (#rrggbb)###cells.form.colour"), f.colour, 16);
            for (CellFieldForm& ff : f.fields) {
                std::string label = (ff.label.empty() ? ff.name : ff.label);
                if (!ff.unit.empty()) label += " (" + ff.unit + ")";
                if (ff.key) label += " [key]";
                label += "###cells.field." + ff.name;
                input_text(label.c_str(), ff.value, 64);
                text_tooltip((ff.name + (ff.help.empty() ? "" : ": " + ff.help) +
                              ". Decimal, 0x.. hexadecimal, a number or a text.")
                                 .c_str());
            }
            input_text("###cells.form.new_field", f.new_field, 64);
            ImGui::SameLine();
            if (ImGui::Button(tr("Add field###cells.add_field"))) (void)state_.show(ed.add_field());
            text_tooltip(tr("Add a field the plugin's cell schema has, or one of your own."));
            ImGui::PopItemWidth();
            ImGui::TextUnformatted(tr("Neighbours (one per line, e.g. la=200/frequency_hz=390200000)"));
            multiline_text("###cells.form.neighbours", f.neighbours, 90.0F);
            ImGui::TextUnformatted(tr("Notes"));
            multiline_text("###cells.form.notes", f.notes, 50.0F);
        }
    }

    if (section_header(tr("CSV import and export###cells.csv"), tr("Read or write the cell database as a CSV file."))) {
        CsvForm& c = ed.csv();
        if (network_.csv_file.empty()) network_.csv_file = ed.default_csv_file().string();
        ImGui::PushItemWidth(-160.0F);
        input_text("File###cells.csv.file", network_.csv_file, 1024);
        ImGui::PopItemWidth();
        c.file = network_.csv_file;
        ImGui::Checkbox(tr("Location area in hexadecimal###cells.csv.hex"), &c.hex_la);
        text_tooltip(tr("Write la as 0x.. (reading takes both)."));
        ImGui::SameLine();
        ImGui::Checkbox(tr("Semicolons###cells.csv.semicolon"), &c.semicolon);
        text_tooltip(tr("Separate the columns with ';' (spreadsheets with a decimal comma); import finds out itself."));
        ImGui::SameLine();
        ImGui::Checkbox(tr("Import replaces###cells.csv.replace"), &c.replace);
        text_tooltip(tr("Replace the whole database instead of adding to it (rows with the same identity update)."));
        if (ImGui::Button(tr("Import###cells.import"))) (void)state_.show(ed.import_csv());
        ImGui::SameLine();
        if (ImGui::Button(tr("Export###cells.export"))) (void)state_.show(ed.export_csv());
        for (const std::string& p : ed.problems()) ImGui::TextColored(kErrorColour, "%s", p.c_str());
    }

    if (section_header(tr("Pairing received cells###cells.match"), tr("Tie cells the radio receives to entries of the database."))) {
        ImGui::TextWrapped(tr("Received cells are paired with entries by these fields (e.g. by location area, by "
                           "carrier or by both). Empty: the technology's key fields."));
        for (const std::string& t : ed.technologies()) {
            auto it = network_.match_text.find(t);
            if (it == network_.match_text.end()) it = network_.match_text.emplace(t, ed.match_fields_text(t)).first;
            ImGui::PushItemWidth(em(300.0F));
            input_text((t + "###cells.match." + t).c_str(), it->second, 256);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button((tr("Use###cells.match_apply.") + t).c_str())) {
                (void)state_.show(ed.set_match_fields(t, it->second));
                it->second = ed.match_fields_text(t);
            }
        }
    }
}

// -- verification, auto-learn, cross-check (DB-03..05) ---------------------------------

void Gui::draw_verification(NetworkChecks& nc) {
    error_line(state_);
    ImGui::Checkbox(tr("Show matches###verify.matches"), &network_.show_matches);
    text_tooltip(tr("List the fields and neighbours that match too, not only the differences."));
    const VerificationView v = nc.verification(network_.show_matches);
    ImGui::Text(tr("%zu cells received: %zu paired, %zu not in the database, %zu ambiguous; %zu differences; %zu "
                "entries not seen"),
                v.received, v.paired, v.not_in_database, v.ambiguous, v.differences, v.not_seen);
    if (!v.note.empty()) ImGui::TextWrapped("%s", v.note.c_str());
    ImGui::TextColored(rgb_colour(kMatchRgb), "match");
    ImGui::SameLine();
    ImGui::TextColored(rgb_colour(kDiffersRgb), "differs");
    ImGui::SameLine();
    ImGui::TextColored(rgb_colour(kNewRgb), "new");
    ImGui::SameLine();
    ImGui::TextColored(rgb_colour(kMissingRgb), tr("missing / not seen"));
    if (ImGui::BeginTable("###verify.table", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0F, std::max(em(140.0F), ImGui::GetContentRegionAvail().y * 0.45F)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char* h : {tr("Cell"), tr("Item"), tr("State"), tr("Database"), tr("Received")}) ImGui::TableSetupColumn(h);
        headers_row();
        for (const VerificationRow& r : v.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.cell.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.item.c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(rgb_colour(r.rgb), "%s", r.state.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.database.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.received.c_str());
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr("Learn###verify.learn"))) (void)state_.show(nc.learn());
    text_tooltip(tr("Add the received cells that are not in the database and fill fields entries lack (stored values "
                 "are never overwritten)."));
    if (network_.log_file.empty()) network_.log_file = nc.default_log_file().string();
    {
        // The field takes the room the label, the switch and the button leave.
        const ImGuiStyle& st = ImGui::GetStyle();
        const float rest = ImGui::CalcTextSize(tr("Log file###verify.log_file"), nullptr, true).x + st.ItemInnerSpacing.x +
                           switch_w(tr("Not seen###verify.not_seen")) + label_w(tr("Write log###verify.log")) +
                           2.0F * st.ItemSpacing.x;
        ImGui::PushItemWidth(std::max(em(120.0F), ImGui::GetContentRegionAvail().x - rest));
    }
    input_text(tr("Log file###verify.log_file"), network_.log_file, 1024);
    text_tooltip(tr("The file Write log writes the verification to."));
    ImGui::PopItemWidth();
    same_line_if(switch_w(tr("Not seen###verify.not_seen")));
    ImGui::Checkbox(tr("Not seen###verify.not_seen"), &network_.log_not_seen);
    text_tooltip(tr("Also log the entries no received cell paired with."));
    same_line_if(label_w(tr("Write log###verify.log")));
    if (ImGui::Button(tr("Write log###verify.log"))) (void)state_.show(nc.write_log(network_.log_file, network_.log_not_seen));
    text_tooltip(tr("Add the differences to the CSV log; a difference already in it is not written again."));

    if (section_header(tr("Neighbour cross-check###verify.cross"), tr("Compare the neighbours each cell broadcasts with the cells received."), ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<CrossCheckRow> cc = nc.cross_check();
        if (cc.empty()) ImGui::TextDisabled(tr("No findings: the neighbour lists agree."));
        for (const CrossCheckRow& r : cc) ImGui::TextColored(rgb_colour(r.rgb), "%s: %s", r.kind.c_str(), r.text.c_str());
    }
}

// -- reselection simulator (DB-08) ------------------------------------------------------

void Gui::draw_reselection(NetworkChecks& nc) {
    error_line(state_);
    ReselectionForm& f = nc.reselection_form();
    const auto choices = nc.serving_choices();
    if (choices.empty()) {
        ImGui::TextWrapped(tr("No cell of a technology with a reselection rule: add cells (Network > Cell database) of a "
                           "technology plugin that has one."));
        return;
    }
    ImGui::TextUnformatted(tr("Serving cell"));
    bool first = true;
    for (const auto& [id, name] : choices) {
        const std::string label = name + "###resel.serving." + id;
        if (!first) same_line_if(switch_w(label.c_str()));
        first = false;
        if (ImGui::RadioButton(label.c_str(), f.serving == id)) (void)state_.show(nc.choose_serving(id));
        text_tooltip(tr("Simulate with this cell as the serving cell."));
    }
    ImGui::PushItemWidth(em(140.0F));
    ImGui::InputDouble(tr("MS power (dBm)###resel.ms_power"), &f.ms_power_dbm, 1.0, 5.0, "%.1f");
    ImGui::PopItemWidth();
    text_tooltip(tr("The radio's maximum transmit power, used by the C1 of the rule."));
    same_line_if(label_w(tr("Received levels###resel.received")));
    if (ImGui::Button(tr("Received levels###resel.received"))) (void)state_.show(nc.use_received_levels());
    text_tooltip(tr("Take the levels of the cells received (radio or replay) as they are now."));
    const ReselectionView v = nc.reselection();
    if (ImGui::BeginTable("###resel.table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                                   ImGuiTableFlags_SizingStretchProp)) {
        for (const char* h : {tr("Cell"), tr("Level (dBm)"), tr("C1 (dB)"), ""}) ImGui::TableSetupColumn(h);
        headers_row();
        for (const ReselectionRow& r : v.rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", r.name.c_str(), r.serving ? tr(" (serving)") : "");
            ImGui::TableNextColumn();
            double level = f.levels.count(r.id) ? f.levels[r.id] : r.level_dbm;
            ImGui::PushItemWidth(-1.0F);
            if (ImGui::InputDouble(("###resel.level." + r.id).c_str(), &level, 1.0, 5.0, "%.1f")) f.levels[r.id] = level;
            ImGui::PopItemWidth();
            ImGui::TableNextColumn();
            if (r.c1) ImGui::Text("%.1f", *r.c1);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (r.chosen) ImGui::TextColored(rgb_colour(kMatchRgb), "reselect");
        }
        ImGui::EndTable();
    }
    if (!v.ok) {
        if (!v.error.empty()) ImGui::TextColored(kErrorColour, "%s", v.error.c_str());
        return;
    }
    ImGui::Text(tr("State: %s"), v.state_label.empty() ? v.state.c_str() : v.state_label.c_str());
    if (!v.reason.empty()) ImGui::TextWrapped("%s", v.reason.c_str());
}

}  // namespace yantra::app
