// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The plugins' own UI (PLG-06): their menu entries and windows, and
// Yantra's drawing table (yantra_ui_api, include/yantra/plugin.h) on Dear
// ImGui. The plugin describes a window's content each frame through the
// table; Yantra draws every control itself, with the app's theme, sizes
// and help rules (DOC-07: tooltips, "(?)" markers, disabled reasons) and
// the plugin's translations (UI-12). The model is PluginUi
// (app/model/plugin_ui.h). Ids: windows "plg.<plugin id>.<window id>",
// controls "plg.<control id>" inside their window, menu entries
// "plg.<plugin id>.<item id>", menus "menu.view" / "menu.tools".
#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "yantra/app/plugin_ui.h"

// The host's side of yantra_ui (opaque to plugins): one per window and
// draw call.
struct yantra_ui {
    yantra::app::PluginUi* model = nullptr;
    const yantra::app::PluginWindowView* window = nullptr;
    yantra::app::Gui::PluginWindowState* state = nullptr;
    std::deque<std::string> strings;  // tr() results, valid during the draw call
    std::vector<bool> sections;       // open state of the nested sections
    bool close = false;
    int controls = 0;                 // controls drawn (tests)
};

namespace yantra::app {

using namespace gui_detail;

namespace {

const char* text_or(const char* s, const char* fallback = "") { return s != nullptr ? s : fallback; }

std::string tr(yantra_ui* ui, const char* english, bool verbatim = false) {
    if (english == nullptr) return {};
    if (verbatim) return english;
    return ui->model->translate(ui->window->plugin_id, english);
}

/// "label###plg.<id>" of a control.
std::string control_label(const yantra_ui_control* c, const std::string& shown) {
    return shown + "###plg." + text_or(c->id, "control");
}

bool valid(const yantra_ui_control* c) { return c != nullptr && c->struct_size >= sizeof(yantra_ui_control); }
bool disabled(const yantra_ui_control* c) { return (c->flags & YANTRA_UI_DISABLED) != 0; }
bool verbatim(const yantra_ui_control* c) { return (c->flags & YANTRA_UI_VERBATIM) != 0; }

/// The hover text of a control: its tooltip, the shortcut in brackets
/// and, when disabled, why (DOC-07 b, c).
void control_help(yantra_ui* ui, const yantra_ui_control* c) {
    ActionView a;
    a.tooltip = tr(ui, c->tooltip);
    if (c->shortcut != nullptr && *c->shortcut != '\0') a.tooltip += std::string(" [") + c->shortcut + "]";
    a.enabled = !disabled(c);
    if (!a.enabled) a.disabled_reason = c->disabled_reason != nullptr ? tr(ui, c->disabled_reason) : ::yantra::app::tr("Not available now.");
    item_tooltip(a);
    if (c->help != nullptr && *c->help != '\0') help_marker(tr(ui, c->help, verbatim(c)).c_str());
}

const char* api_tr(yantra_ui* ui, const char* english) {
    if (english == nullptr) return "";
    ui->strings.push_back(tr(ui, english));
    return ui->strings.back().c_str();
}

void api_heading(yantra_ui* ui, const char* text, const char* help) {
    ImGui::SeparatorText(tr(ui, text).c_str());
    if (help != nullptr && *help != '\0') help_marker(tr(ui, help).c_str());
}

void api_text(yantra_ui* ui, const char* text) {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr(ui, text).c_str());
    ImGui::PopTextWrapPos();
}

void api_value(yantra_ui* ui, const yantra_ui_control* c, const char* value, const char* unit) {
    if (!valid(c)) return;
    ++ui->controls;
    const std::string label = tr(ui, c->label, verbatim(c));
    std::string v = text_or(value, "-");
    if (unit != nullptr && *unit != '\0') v += std::string(" ") + unit;
    // A selectable carries the id (tests, screenshots) and the tooltip.
    ImGui::Selectable(control_label(c, label + ": " + v).c_str(), false, ImGuiSelectableFlags_None,
                      ImVec2(ImGui::CalcTextSize((label + ": " + v).c_str()).x, 0.0F));
    control_help(ui, c);
    ui->state->values[text_or(c->id)] = v;
}

void api_status(yantra_ui* ui, uint32_t tone, const char* text, const char* tooltip) {
    ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (tone == YANTRA_UI_TONE_PASS) colour = ImVec4(0.30F, 0.75F, 0.35F, 1.0F);
    if (tone == YANTRA_UI_TONE_WARN) colour = kBannerColour;
    if (tone == YANTRA_UI_TONE_FAIL) colour = kErrorColour;
    ImGui::TextColored(colour, "%s", tr(ui, text).c_str());
    if (tooltip != nullptr) text_tooltip(tr(ui, tooltip).c_str());
}

void api_separator(yantra_ui*) { ImGui::Separator(); }
void api_same_line(yantra_ui*) { ImGui::SameLine(); }

int api_button(yantra_ui* ui, const yantra_ui_control* c) {
    if (!valid(c)) return 0;
    ++ui->controls;
    const std::string id = text_or(c->id);
    // YANTRA_UI_CONFIRM: a confirmed click is reported in the next frame.
    if (ui->state->confirmed == id) {
        ui->state->confirmed.clear();
        ImGui::BeginDisabled(disabled(c));
        ImGui::Button(control_label(c, tr(ui, c->label, verbatim(c))).c_str());
        ImGui::EndDisabled();
        control_help(ui, c);
        return 1;
    }
    ImGui::BeginDisabled(disabled(c));
    const bool pressed = ImGui::Button(control_label(c, tr(ui, c->label, verbatim(c))).c_str());
    ImGui::EndDisabled();
    control_help(ui, c);
    if (!pressed) return 0;
    if ((c->flags & YANTRA_UI_CONFIRM) != 0) {
        ui->state->confirm = id;
        ui->state->confirm_text = c->help != nullptr ? tr(ui, c->help) : tr(ui, c->tooltip);
        return 0;
    }
    return 1;
}

int api_checkbox(yantra_ui* ui, const yantra_ui_control* c, int32_t* value) {
    if (!valid(c) || value == nullptr) return 0;
    ++ui->controls;
    bool on = *value != 0;
    ImGui::BeginDisabled(disabled(c));
    const bool changed = ImGui::Checkbox(control_label(c, tr(ui, c->label, verbatim(c))).c_str(), &on);
    ImGui::EndDisabled();
    control_help(ui, c);
    if (changed) *value = on ? 1 : 0;
    return changed ? 1 : 0;
}

int api_number(yantra_ui* ui, const yantra_ui_control* c, double* value, double minimum, double maximum, double step,
               const char* unit) {
    if (!valid(c) || value == nullptr) return 0;
    ++ui->controls;
    std::string label = tr(ui, c->label, verbatim(c));
    if (unit != nullptr && *unit != '\0') label += std::string(" (") + unit + ")";
    const std::string id = control_label(c, label);
    ImGui::BeginDisabled(disabled(c));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
    double v = *value;
    bool changed = false;
    const bool closed = std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
    const bool integer = step > 0.0 && std::floor(step) == step;
    if (closed) {
        // A slider (touch) and a field for a typed value, as the radio windows.
        changed = ImGui::SliderScalar(("###plg." + std::string(text_or(c->id)) + ".slider").c_str(), ImGuiDataType_Double, &v,
                                      &minimum, &maximum, integer ? "%.0f" : "%.3g");
        if (c->tooltip != nullptr) text_tooltip(tr(ui, c->tooltip).c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0F);
        double typed = v;
        if (ImGui::InputDouble((id + ".input").c_str(), &typed, 0.0, 0.0, integer ? "%.0f" : "%.6g")) {
            v = typed;
            changed = true;
        }
    } else {
        changed = ImGui::InputDouble(id.c_str(), &v, step > 0.0 ? step : 0.0, step > 0.0 ? step * 10.0 : 0.0,
                                     integer ? "%.0f" : "%.6g");
    }
    ImGui::EndDisabled();
    control_help(ui, c);
    if (!changed || !std::isfinite(v)) return 0;
    if (std::isfinite(minimum)) v = std::max(v, minimum);
    if (std::isfinite(maximum)) v = std::min(v, maximum);
    if (step > 0.0) {
        const double base = std::isfinite(minimum) ? minimum : 0.0;
        v = base + std::round((v - base) / step) * step;
    }
    if (v == *value) return 0;
    *value = v;
    return 1;
}

int api_choice(yantra_ui* ui, const yantra_ui_control* c, const yantra_enum_item* items, uint32_t count, int64_t* value) {
    if (!valid(c) || value == nullptr || (items == nullptr && count > 0)) return 0;
    ++ui->controls;
    const std::string base = text_or(c->id);
    std::string shown = "-";
    for (uint32_t i = 0; i < count; ++i)
        if (items[i].value == *value) shown = tr(ui, items[i].label, verbatim(c));
    ImGui::BeginDisabled(disabled(c));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
    int changed = 0;
    if (ImGui::BeginCombo(control_label(c, tr(ui, c->label, verbatim(c))).c_str(), shown.c_str())) {
        for (uint32_t i = 0; i < count; ++i) {
            const std::string item = tr(ui, items[i].label, verbatim(c)) + "###plg." + base + ".item." + text_or(items[i].name);
            if (ImGui::Selectable(item.c_str(), items[i].value == *value) && items[i].value != *value) {
                *value = items[i].value;
                changed = 1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    control_help(ui, c);
    return changed;
}

int api_text_input(yantra_ui* ui, const yantra_ui_control* c, char* buffer, uint32_t size) {
    if (!valid(c) || buffer == nullptr || size == 0) return 0;
    ++ui->controls;
    ImGui::BeginDisabled(disabled(c));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
    const bool changed = ImGui::InputText(control_label(c, tr(ui, c->label, verbatim(c))).c_str(), buffer, size);
    ImGui::EndDisabled();
    control_help(ui, c);
    return changed ? 1 : 0;
}

int api_section_begin(yantra_ui* ui, const yantra_ui_control* c) {
    if (!valid(c)) {
        ui->sections.push_back(false);
        return 0;
    }
    ++ui->controls;
    const bool open = ImGui::TreeNodeEx(control_label(c, tr(ui, c->label, verbatim(c))).c_str(),
                                        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);
    control_help(ui, c);
    ui->sections.push_back(open);
    return open ? 1 : 0;
}

void api_section_end(yantra_ui* ui) {
    if (ui->sections.empty()) return;
    if (ui->sections.back()) ImGui::TreePop();
    ui->sections.pop_back();
}

void api_progress(yantra_ui* ui, const yantra_ui_control* c, double fraction, const char* text) {
    if (!valid(c)) return;
    ++ui->controls;
    const float f = std::isfinite(fraction) ? static_cast<float>(std::clamp(fraction, 0.0, 1.0)) : 0.0F;
    const std::string overlay = text != nullptr ? tr(ui, text) : std::string();
    ImGui::PushID(text_or(c->id));
    ImGui::ProgressBar(f, ImVec2(-1.0F, 0.0F), overlay.empty() ? nullptr : overlay.c_str());
    ImGui::PopID();
    control_help(ui, c);
}

void api_plot(yantra_ui* ui, const yantra_ui_control* c, const yantra_ui_plot* p) {
    if (!valid(c) || p == nullptr || p->struct_size < sizeof(yantra_ui_plot) || p->values == nullptr || p->width == 0)
        return;
    ++ui->controls;
    const std::string label = control_label(c, tr(ui, c->label, verbatim(c)));
    const float lo = static_cast<float>(p->y_min), hi = static_cast<float>(p->y_max);
    const ImVec2 size(-1.0F, ImGui::GetFontSize() * 7.0F);
    const int n = static_cast<int>(std::min<uint32_t>(p->width, 100000));
    if (p->kind == YANTRA_PLOT_HISTOGRAM) {
        ImGui::PlotHistogram(label.c_str(), p->values, n, 0, nullptr, lo, hi, size);
    } else if (p->kind == YANTRA_PLOT_HEATMAP && p->height > 0) {
        ImGui::InvisibleButton(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, size.y));
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const uint32_t rows = std::min<uint32_t>(p->height, 2000);
        const float cw = (b.x - a.x) / static_cast<float>(n), rh = (b.y - a.y) / static_cast<float>(rows);
        for (uint32_t r = 0; r < rows; ++r)
            for (int x = 0; x < n; ++x) {
                const float v = p->values[static_cast<std::size_t>(r) * p->width + static_cast<std::size_t>(x)];
                const float t = hi > lo && std::isfinite(v) ? std::clamp((v - lo) / (hi - lo), 0.0F, 1.0F) : 0.0F;
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(t, 0.3F, 1.0F - t, 1.0F));
                dl->AddRectFilled(ImVec2(a.x + cw * static_cast<float>(x), a.y + rh * static_cast<float>(r)),
                                  ImVec2(a.x + cw * static_cast<float>(x + 1), a.y + rh * static_cast<float>(r + 1)), col);
            }
    } else {
        ImGui::PlotLines(label.c_str(), p->values, n, 0, nullptr, lo, hi, size);
    }
    control_help(ui, c);
    char axis[160];
    std::snprintf(axis, sizeof axis, "%g .. %g %s", p->x_min, p->x_max, text_or(p->x_unit));
    ImGui::TextDisabled("%s", axis);
}

void api_table(yantra_ui* ui, const yantra_ui_control* c, const yantra_ui_table* t) {
    if (!valid(c) || t == nullptr || t->struct_size < sizeof(yantra_ui_table) || t->column_count == 0 ||
        t->column_count > 64 || t->headers == nullptr || (t->row_count > 0 && t->cells == nullptr))
        return;
    ++ui->controls;
    const std::string id = std::string("plg.") + text_or(c->id);
    const int cols = static_cast<int>(t->column_count);
    ImGui::TextUnformatted(tr(ui, c->label, verbatim(c)).c_str());
    control_help(ui, c);
    if (!ImGui::BeginTable(id.c_str(), cols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(0.0F, ImGui::GetFontSize() * 12.0F)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    for (int i = 0; i < cols; ++i) ImGui::TableSetupColumn(tr(ui, t->headers[i]).c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int i = 0; i < cols; ++i) {
        ImGui::TableSetColumnIndex(i);
        const std::string tip = t->header_tooltips != nullptr && t->header_tooltips[i] != nullptr ? tr(ui, t->header_tooltips[i]) : "";
        table_header(tr(ui, t->headers[i]).c_str(), tip.empty() ? nullptr : tip.c_str());
    }
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(std::min<uint64_t>(t->row_count, 1000000)));
    while (clip.Step())
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            ImGui::TableNextRow();
            for (int i = 0; i < cols; ++i) {
                ImGui::TableSetColumnIndex(i);
                ImGui::TextUnformatted(text_or(t->cells[static_cast<std::size_t>(r) * t->column_count + static_cast<std::size_t>(i)]));
            }
        }
    ImGui::EndTable();
    ui->state->table_rows[text_or(c->id)] = t->row_count;
}

void api_close(yantra_ui* ui) { ui->close = true; }

yantra_ui_api make_api(yantra_ui* ui, const std::string& language) {
    yantra_ui_api a{};
    a.struct_size = sizeof a;
    a.ui = ui;
    a.language = language == "nl" ? "nl" : language == "fr" ? "fr" : language == "de" ? "de" : "en";
    a.tr = api_tr;
    a.heading = api_heading;
    a.text = api_text;
    a.value = api_value;
    a.status = api_status;
    a.separator = api_separator;
    a.same_line = api_same_line;
    a.button = api_button;
    a.checkbox = api_checkbox;
    a.number = api_number;
    a.choice = api_choice;
    a.text_input = api_text_input;
    a.section_begin = api_section_begin;
    a.section_end = api_section_end;
    a.progress = api_progress;
    a.plot = api_plot;
    a.table = api_table;
    a.close = api_close;
    return a;
}

}  // namespace

void Gui::draw_plugin_entries(std::uint32_t menu) {
    PluginUi& ui = state_.plugin_ui();
    const std::vector<PluginMenuEntry> entries = ui.menu(menu);
    if (entries.empty()) return;
    if (menu != YANTRA_MENU_VIEW && menu != YANTRA_MENU_TOOLS) ImGui::Separator();
    std::string open_sub;  // the submenu being drawn ("" none)
    bool sub_shown = false;
    for (const PluginMenuEntry& e : entries) {
        if (e.submenu != open_sub) {
            if (!open_sub.empty() && sub_shown) ImGui::EndMenu();
            open_sub = e.submenu;
            sub_shown = !open_sub.empty() && ImGui::BeginMenu((open_sub + "###" + e.id + ".sub").c_str());
        }
        if (!open_sub.empty() && !sub_shown) continue;
        if (ImGui::MenuItem((e.label + "###" + e.id).c_str(), e.shortcut.empty() ? nullptr : e.shortcut.c_str(),
                            e.checked, e.enabled))
            (void)state_.show(ui.select(e.plugin_id, e.item_id));
        ActionView a;
        a.tooltip = e.tooltip;
        if (!e.shortcut.empty()) a.tooltip += " [" + e.shortcut + "]";
        a.enabled = e.enabled;
        a.disabled_reason = e.disabled_reason;
        item_tooltip(a);
    }
    if (!open_sub.empty() && sub_shown) ImGui::EndMenu();
}

void Gui::draw_plugin_menus() {
    PluginUi& ui = state_.plugin_ui();
    const struct {
        std::uint32_t menu;
        const char* label;
    } menus[] = {{YANTRA_MENU_VIEW, tr("View###menu.view")}, {YANTRA_MENU_TOOLS, tr("Tools###menu.tools")}};
    for (const auto& m : menus) {
        // The menus appear when an enabled plugin adds entries (UI-14).
        if (!ui.has_menu(m.menu)) continue;
        if (!ImGui::BeginMenu(m.label)) continue;
        draw_plugin_entries(m.menu);
        ImGui::EndMenu();
    }
}

void Gui::draw_plugin_windows() {
    PluginUi& model = state_.plugin_ui();
    plugin_frame_.clear();
    // Copies: a plugin may open or close windows while it draws.
    const std::vector<PluginWindowView> windows = model.open_windows();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    int n = 0;
    for (const PluginWindowView& w : windows) {
        PluginWindowState& st = plugin_windows_[w.id];
        yantra_ui ui;
        ui.model = &model;
        ui.window = &w;
        ui.state = &st;
        const yantra_ui_api api = make_api(&ui, model.language());
        const std::string title = w.title + "###" + w.id;
        const float width = std::min(em(w.width > 0.0 ? static_cast<float>(w.width) : 420.0F), vp->WorkSize.x * 0.95F);
        const float height = w.height > 0.0 ? std::min(em(static_cast<float>(w.height)), vp->WorkSize.y * 0.9F) : 0.0F;
        bool open = true;
        bool drawn = false;
        if (w.kind == YANTRA_UI_DIALOG) {
            // A modal dialog; it closes with yantra_ui_api::close (or Esc).
            if (!ImGui::IsPopupOpen(title.c_str())) ImGui::OpenPopup(title.c_str());
            ImGui::SetNextWindowSize(ImVec2(std::min(width, vp->WorkSize.x * 0.95F), height), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                wrap_texts();
                help_marker(w.help.c_str());
                model.draw(w, api);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ui.close = true;
                if (ui.close) ImGui::CloseCurrentPopup();
                drawn = true;
                ImGui::EndPopup();
            } else {
                ui.close = true;
            }
        } else {
            // A panel sits at the right edge of the main window; a window floats.
            if (w.kind == YANTRA_UI_PANEL)
                ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - width - 8.0F,
                                               vp->WorkPos.y + 40.0F + 30.0F * static_cast<float>(n)),
                                        ImGuiCond_Appearing);
            else
                ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 80.0F + 30.0F * static_cast<float>(n),
                                               vp->WorkPos.y + 80.0F + 30.0F * static_cast<float>(n)),
                                        ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
            if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
                wrap_texts();
                help_button("14.2");
                ImGui::TextDisabled("(?)");
                text_tooltip(w.help.c_str());
                model.draw(w, api);
                drawn = true;
            }
            ImGui::End();
        }
        // Sections the plugin left open are closed for it.
        while (!ui.sections.empty()) {
            if (ui.sections.back()) ImGui::TreePop();
            ui.sections.pop_back();
        }
        ++n;
        if (!st.confirm.empty()) {
            // YANTRA_UI_CONFIRM: ask; "Yes" reports the click next frame.
            const std::string q = tr("Confirm###") + w.id + ".confirm";
            if (!ImGui::IsPopupOpen(q.c_str())) ImGui::OpenPopup(q.c_str());
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
            if (ImGui::BeginPopupModal(q.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
                ImGui::TextUnformatted(st.confirm_text.c_str());
                ImGui::PopTextWrapPos();
                if (dialog_button(tr("Yes###plg.confirm.ok"), tr("Go ahead"))) {
                    st.confirmed = st.confirm;
                    st.confirm.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (dialog_button(tr("No###plg.confirm.cancel"), tr("Do not do it [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    st.confirm.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        PluginFrame f;
        f.id = w.id;
        f.drawn = drawn;
        f.controls = ui.controls;
        plugin_frame_.push_back(f);
        if (!open || ui.close) {
            model.close(w.plugin_id, w.window_id);
            plugin_windows_.erase(w.id);
        }
    }
}

}  // namespace yantra::app
