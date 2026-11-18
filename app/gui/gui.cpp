// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ImGui drawing of the app shell, see gui.h.
// Copied from yantra-v2 68ea20a: app/src/gui.cpp, adapted.
#include "gui.h"
#include "yantra/app/i18n.h"
#include "gui_widgets.h"
#include "yantra/app/app_icon.h"

#include <imgui.h>
#include <imgui_internal.h>  // ImGuiTabBar (the floor tabs' order, IND-04)

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace yantra::app {

using namespace gui_detail;

const char* dialog_id(Dialog d, const AppState& state) {
    switch (d) {
    case Dialog::NewProject: return tr("New indoor project###dlg.new_project");
    case Dialog::Browse:
        switch (state.browse_purpose()) {
        case BrowsePurpose::OpenProject: return tr("Open project###dlg.browse");
        case BrowsePurpose::SaveAs: return tr("Save project as###dlg.browse");
        case BrowsePurpose::NewProjectFile: return tr("New project file###dlg.browse");
        case BrowsePurpose::PlanFile: return tr("Plan file###dlg.browse");
        case BrowsePurpose::PhotoFile: return tr("Photo###dlg.browse");
        case BrowsePurpose::ExportFile: return tr("Export to###dlg.browse");
        case BrowsePurpose::ReportFile: return tr("Report file###dlg.browse");
        case BrowsePurpose::ProfileSave: return tr("Save profile###dlg.browse");
        case BrowsePurpose::ProfileLoad: return tr("Load profile###dlg.browse");
        }
        break;
    case Dialog::AddFloor: return tr("Add floor###dlg.add_floor");
    case Dialog::PlanImport: return tr("Plan import###dlg.plan_import");
    case Dialog::About: return tr("About Yantra###dlg.about");
    case Dialog::FloorProperties: return tr("Floor properties###dlg.floor_properties");
    case Dialog::RemoveFloor: return tr("Remove floor?###dlg.remove_floor");
    case Dialog::InterpolationSettings: return tr("Interpolation settings###dlg.interpolation");
    case Dialog::Settings: return tr("Settings###dlg.settings");
    case Dialog::ResetSettings: return tr("Reset all settings?###dlg.reset_settings");
    case Dialog::TourConsent: return tr("Download a MAK plan sheet?###dlg.tour_consent");
    case Dialog::Export: return tr("Export###dlg.export");
    case Dialog::Report: return tr("Report###dlg.report");
    case Dialog::AcceptanceProfiles: return tr("Acceptance profiles###dlg.profiles");
    case Dialog::Sharing: return tr("Sharing###dlg.sharing");
    case Dialog::ReportReady: return tr("Report ready###dlg.report_ready");
    case Dialog::Camera: return tr("Take a photo###dlg.camera");
    case Dialog::Note: return tr("Note###dlg.note");
    case Dialog::None: break;
    }
    return "###dlg.none";
}

Gui::Gui(AppState& state)
    : state_(state), canvas_(std::make_unique<PlanCanvas>()), style_(style_of(state.settings())),
      ui_generation_(state.ui_generation()) {}

Gui::~Gui() {
    canvas_.reset();
    import_view_.close();
    yantra_logo_.reset();
    curtat_logo_.reset();
    UserTexture::release_all();
}

void Gui::set_wait_for_tiles(bool on) {
    canvas_->set_wait_for_tiles(on);
    import_view_.set_wait_for_tiles(on);
}

Gui::Style Gui::style_of(const AppSettings& s) {
    return Style{s.theme, s.touch, static_cast<float>(s.ui_scale)};
}

namespace {

// UI-13 high contrast for bright sites: white text on black, white frame
// borders, yellow for what is active or hovered.
void style_colors_high_contrast(ImGuiStyle& st) {
    ImGui::StyleColorsDark(&st);
    ImVec4* c = st.Colors;
    const ImVec4 black(0.0F, 0.0F, 0.0F, 1.0F), white(1.0F, 1.0F, 1.0F, 1.0F), yellow(1.0F, 0.86F, 0.0F, 1.0F);
    const ImVec4 grey(0.16F, 0.16F, 0.16F, 1.0F), dark_yellow(0.55F, 0.45F, 0.0F, 1.0F);
    c[ImGuiCol_Text] = white;
    c[ImGuiCol_TextDisabled] = ImVec4(0.75F, 0.75F, 0.75F, 1.0F);
    c[ImGuiCol_WindowBg] = c[ImGuiCol_ChildBg] = c[ImGuiCol_PopupBg] = black;
    c[ImGuiCol_MenuBarBg] = black;
    c[ImGuiCol_Border] = white;
    c[ImGuiCol_FrameBg] = grey;
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_FrameBgActive] = dark_yellow;
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgCollapsed] = black;
    c[ImGuiCol_TitleBgActive] = dark_yellow;
    c[ImGuiCol_Button] = grey;
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_ButtonActive] = dark_yellow;
    c[ImGuiCol_Header] = grey;
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_HeaderActive] = dark_yellow;
    c[ImGuiCol_Tab] = grey;
    c[ImGuiCol_TabHovered] = dark_yellow;
    c[ImGuiCol_TabSelected] = dark_yellow;
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = yellow;
    c[ImGuiCol_Separator] = white;
    c[ImGuiCol_NavCursor] = yellow;
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_TableBorderLight] = white;
    c[ImGuiCol_TableRowBg] = black;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.10F, 0.10F, 0.10F, 1.0F);
    c[ImGuiCol_TextSelectedBg] = dark_yellow;
}

}  // namespace

void Gui::apply_style(const Style& style) {
    ImGuiStyle st;
    switch (style.theme) {
    case Theme::Light: ImGui::StyleColorsLight(&st); break;
    case Theme::HighContrast: style_colors_high_contrast(st); break;
    case Theme::Dark: ImGui::StyleColorsDark(&st); break;
    }
    st.WindowRounding = 4.0F;
    st.FrameRounding = 4.0F;
    st.FramePadding = ImVec2(10.0F, 7.0F);
    st.ItemSpacing = ImVec2(10.0F, 8.0F);
    if (style.theme == Theme::HighContrast) st.FrameBorderSize = 1.0F;
    float font_scale = 1.0F;
    if (style.touch) {
        // UI-13 touch layout: a larger font and every control at least
        // kTouchTargetPx high, with room between them for fingers.
        font_scale = 1.5F;
        const float font_px = 13.0F * font_scale;
        st.FramePadding = ImVec2(14.0F, std::max(7.0F, (static_cast<float>(kTouchTargetPx) - font_px) * 0.5F));
        st.ItemSpacing = ImVec2(14.0F, 12.0F);
        st.ItemInnerSpacing = ImVec2(10.0F, 8.0F);
        st.ScrollbarSize = 30.0F;
        st.GrabMinSize = 30.0F;
        st.TouchExtraPadding = ImVec2(4.0F, 4.0F);
        st.WindowPadding = ImVec2(14.0F, 14.0F);
    }
    st.ScaleAllSizes(style.scale);
    st.FontSizeBase = ImGui::GetStyle().FontSizeBase;
    st.FontScaleMain = font_scale * style.scale;
    st.FontScaleDpi = ImGui::GetStyle().FontScaleDpi;
    st.HoverDelayNormal = ImGui::GetStyle().HoverDelayNormal;  // DOC-07 h: Gui::draw follows the setting
    ImGui::GetStyle() = st;
    gui_detail::set_layout(style.touch, style.scale, style.theme);
}

void Gui::restart_ui() {
    // UI-16: after Reset all settings the UI starts as on first start.
    canvas_->close();
    import_view_.close();
    shown_ = Dialog::None;
    shown_floor_.clear();
    tab_order_.clear();
    marker_label_.clear();
    ring_name_.clear();
    cut_name_.clear();
    replay_pick_ = -1;
    replay_speed_ = 1.0;
    graph_level_ = graph_c1_ = graph_quality_ = true;
    threshold_on_.clear();
    setting_text_.clear();
    setting_input_error_.clear();
    setting_editing_.clear();
    calibration_file_.clear();
    plugin_windows_.clear();
}

void Gui::run(Action a) {
    const Result r = state_.trigger(a);
    (void)r;  // the state shows the result in the status line
}

void Gui::handle_shortcuts() {
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    struct Key {
        ImGuiKeyChord chord;
        Action action;
    };
    const Key keys[] = {
        {ImGuiMod_Ctrl | ImGuiKey_N, Action::NewProject},  {ImGuiMod_Ctrl | ImGuiKey_O, Action::OpenProject},
        {ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, Action::SaveAs}, {ImGuiMod_Ctrl | ImGuiKey_S, Action::Save},
        {ImGuiMod_Ctrl | ImGuiKey_W, Action::CloseProject}, {ImGuiMod_Ctrl | ImGuiKey_Q, Action::Exit},
        {ImGuiMod_Ctrl | ImGuiKey_Comma, Action::Settings},
    };
    for (const Key& k : keys) {
        if (ImGui::IsKeyChordPressed(k.chord)) {
            if (state_.action(k.action).enabled) run(k.action);
            return;
        }
    }
}

void Gui::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;
    // Touch layout: menu entries of a touch target's height (UI-13).
    const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(spacing.x, layout().touch ? std::max(spacing.y, px(static_cast<float>(kTouchTargetPx)) -
                                                                                   ImGui::GetFontSize() + 0.5F)
                                                         : spacing.y));
    for (const Menu& m : state_.menus()) {
        if (!ImGui::BeginMenu((m.label + "###menu." + m.id).c_str())) {
            if (m.id == "file") {
                draw_radio_menu();  // PLG-16: between File and Help
                draw_plugin_menus();  // PLG-06: View and Tools when plugins add entries
            }
            continue;
        }
        for (const MenuEntry& e : m.entries) {
            switch (e.kind) {
            case MenuEntry::Kind::Separator: ImGui::Separator(); break;
            case MenuEntry::Kind::Recent: {
                const auto recent = state_.recent_projects();
                const bool open = ImGui::BeginMenu(tr("Recent projects###file.recent"), !recent.empty());
                if (!recent.empty()) text_tooltip(tr("The projects you opened last."));
                if (open) {
                    for (const RecentView& r : recent) {
                        if (ImGui::MenuItem(r.label.c_str(), nullptr, false, r.exists))
                            (void)state_.open_project(r.path);
                        text_tooltip(r.exists ? r.path.string().c_str() : tr("This file no longer exists."));
                    }
                    ImGui::EndMenu();
                }
                if (recent.empty()) text_tooltip(tr("Projects you open or create are listed here."));
                break;
            }
            case MenuEntry::Kind::Action:
                if (ImGui::MenuItem(item_label(e.item).c_str(), e.item.shortcut.empty() ? nullptr : e.item.shortcut.c_str(),
                                    e.item.checked, e.item.enabled))
                    run(e.item.action);
                item_tooltip(e.item);
                break;
            }
        }
        // PLG-06: the entries enabled plugins add to this menu.
        if (m.id == "file") draw_plugin_entries(YANTRA_MENU_FILE);
        if (m.id == "help") draw_plugin_entries(YANTRA_MENU_HELP);
        ImGui::EndMenu();
        if (m.id == "file") {
            draw_radio_menu();  // PLG-16: between File and Help
            draw_plugin_menus();  // PLG-06: View and Tools when plugins add entries
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndMainMenuBar();
}

void Gui::draw_start_screen() {
    const StartScreenView v = state_.start_screen();
    const float column = std::min(ImGui::GetContentRegionAvail().x, em(760.0F));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - column) * 0.5F));
    ImGui::BeginChild("###start_column", ImVec2(column, 0.0F));
    wrap_texts();
    help_button("1.1");
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8F);
    ImGui::TextUnformatted(v.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(v.intro.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    draw_tour_offer();

    const float button_w = (column - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
    int n = 0;
    for (const ActionView& a : v.actions) {
        if (n % 2 == 1) ImGui::SameLine();
        ImGui::BeginDisabled(!a.enabled);
        if (ImGui::Button((a.label + "###start." + a.id).c_str(), ImVec2(button_w, target_h(1.25F)))) run(a.action);
        ImGui::EndDisabled();
        item_tooltip(a);
        ++n;
    }

    ImGui::Spacing();
    ImGui::SeparatorText(tr("Recent projects"));
    help_marker(tr("The projects you opened or created last, newest first. A greyed-out entry no longer exists where it "
                "was; the list is kept in your settings."));
    if (v.recent.empty()) {
        ImGui::TextDisabled(tr("None yet: projects you create or open are listed here."));
    } else {
        int i = 0;
        for (const RecentView& r : v.recent) {
            ImGui::BeginDisabled(!r.exists);
            if (ImGui::Selectable((r.label + "###start.recent." + std::to_string(i++)).c_str(), false, 0,
                                  ImVec2(column, std::max(ImGui::GetTextLineHeight() + 6.0F, target_h(0.75F)))))
                (void)state_.open_project(r.path);
            ImGui::EndDisabled();
            text_tooltip(r.exists ? r.path.string().c_str() : tr("This file no longer exists."));
        }
    }
    ImGui::EndChild();
}

void Gui::draw_project_screen() {
    const auto p = state_.project_view();
    ProjectWindow* w = state_.project_window();
    if (!p || w == nullptr) return;
    // DEMO-07: a permanent banner on a demo project, above everything.
    if (!p->demo_label.empty()) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float bh = ImGui::GetTextLineHeight() + 10.0F;
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + bh),
                                                  ImGui::GetColorU32(kBannerColour), 3.0F);
        ImGui::SetCursorScreenPos(ImVec2(a.x + 10.0F, a.y + 5.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
        ImGui::TextUnformatted(p->demo_label.c_str());
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("###demo_banner", ImVec2(std::max(1.0F, ImGui::GetContentRegionAvail().x), bh));
        text_tooltip(tr("This is a demo project: its measurements are simulated, not real. Its exports and reports carry "
                     "the same label."));
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3F);
    ImGui::TextUnformatted(p->name.c_str());
    ImGui::PopFont();
    // The path, shortened with "..." when the line is too short for it (its
    // whole text on hover), then the note.
    {
        const std::string path = p->path.string();
        const std::string note = "- " + p->file_note;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float marker = ImGui::CalcTextSize("(?)").x + gap;
        const float note_w = ImGui::CalcTextSize(note.c_str()).x + gap;
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool one_line = avail - note_w - marker >= avail * 0.3F;
        text_ellipsis(path.c_str(), one_line ? avail - note_w - marker : avail - marker, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        text_tooltip(path.c_str());
        if (one_line) ImGui::SameLine();
        ImGui::TextDisabled("%s", note.c_str());
    }
    help_marker(tr("A project file is live: every change is written to it at once, so nothing is lost when Yantra "
                "stops, and there is nothing to discard. File > Save makes the file complete on its own (ready to "
                "copy); Save as continues with a copy. While it is open here, no other Yantra can open it."));

    draw_floor_tabs(*w);
    // IND-47: the About building tab's page instead of a floor's.
    if (about_shown_) {
        draw_about_page();
        return;
    }
    draw_plan_credit(w->selected());
    const FloorContent* c = w->content(state_.settings().layers.bin_size_m);
    if (c == nullptr) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", state_.no_floor_hint().c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    // Floor commands (IND-04).
    const std::string id = c->floor_id;
    // UI-23: one button for the floor's plan and its properties.
    if (ImGui::Button(tr("Plan and properties...###floor.plan"))) (void)state_.edit_floor_plan(id);
    text_tooltip(state_.edit_plan_reason(id).empty()
                     ? tr("Open the floor in the plan import window: its name, level and height, its trim, rotation, "
                          "scale and reference point, and more floors from the same sheet.")
                     : tr("This floor has no plan: change its name, level and height. Add its plan with File > Add "
                          "floor."));
    // IND-04, UI-20: no Move or Remove buttons here: a floor tab is dragged
    // to its place, and its right-click menu moves or removes it
    // (draw_floor_tabs).
    // The view's buttons stay together (the bar, Fit, + and -).
    same_line_if(ImGui::CalcTextSize("|").x + ImGui::GetStyle().ItemSpacing.x * 4.0F + label_w(tr("Fit###plan.fit")) +
                 label_w("+") + label_w("-"));
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button(tr("Fit###plan.fit"))) canvas_->fit();
    text_tooltip(tr("Show the whole floor [double click on the plan]"));
    ImGui::SameLine();
    if (ImGui::Button("+###plan.zoom_in")) canvas_->zoom(1.25);
    text_tooltip(tr("Zoom in [mouse wheel]"));
    ImGui::SameLine();
    if (ImGui::Button("-###plan.zoom_out")) canvas_->zoom(0.8);
    text_tooltip(tr("Zoom out [mouse wheel]"));
    {
        // The hint after the buttons when it fits there, else on a line of its own.
        const char* hint = tr("Drag to pan, wheel to zoom");
        same_line_if(ImGui::CalcTextSize(hint).x);
        ImGui::TextDisabled("%s", hint);
    }

    // The tabs on the left, the plan on the right (narrower on small
    // windows). The plan and the side panel keep a usable height; when the
    // window is too low for them (touch layout, large UI scale, long
    // translations) the main window scrolls (TST-52).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y = std::max(avail.y, em(240.0F));
    const float panel = std::min(std::clamp(avail.x * 0.28F, em(220.0F), em(340.0F)), avail.x * 0.5F);
    ImGui::BeginChild("###side", ImVec2(panel, avail.y), ImGuiChildFlags_Borders);
    wrap_texts();
    overlay_.progress = false;
    // UI-18: two tabs, Walk and Map, as a row of buttons that wraps on a
    // narrow panel so each name stays readable (UI-13). A tab asked for by a
    // command (the tour's Show me) is shown.
    struct Page {
        const char* key;
        const char* label;
        const char* tip;
    };
    const Page pages[] = {
        {"walk", tr("Walk###panel.walk"),
         tr("Measure the floor: the walk test (or the mouse survey with the simulator), the live values and the "
            "recording.")},
        {"map", tr("Map###panel.map"),
         tr("The measured points and the interpolated coverage map of the floor, with the post-processing settings.")},
    };
    if (const std::string want = state_.take_panel_request(); want == "walk" || want == "map") page_ = want;
    if (page_ != "walk" && page_ != "map") page_ = "walk";
    {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float width = ImGui::GetContentRegionAvail().x;
        float x = 0.0F;
        for (const Page& pg : pages) {
            const std::string_view label(pg.label);
            const std::string text(label.substr(0, label.find("##")));
            const float bw = ImGui::CalcTextSize(text.c_str()).x + 2.0F * st.FramePadding.x;
            if (x > 0.0F && x + st.ItemSpacing.x + bw <= width) {
                ImGui::SameLine();
                x += st.ItemSpacing.x;
            } else {
                x = 0.0F;
            }
            x += bw;
            const bool current = page_ == pg.key;
            if (current) ImGui::PushStyleColor(ImGuiCol_Button, st.Colors[ImGuiCol_TabSelected]);
            if (ImGui::Button(pg.label)) page_ = pg.key;
            if (current) ImGui::PopStyleColor();
            text_tooltip(pg.tip);
        }
        ImGui::Separator();
    }
    if (page_ == "walk") {
        help_button("7.1");
        draw_walk_page(c);
    } else {
        help_button("13.5");
        draw_map_page(*w, c);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("###plan", ImVec2(0.0F, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // The Map tab may have changed the content (another map chosen).
    c = w->content(state_.settings().layers.bin_size_m);
    if (c != nullptr) {
        const bool walking = state_.live() != nullptr && state_.live()->running();
        canvas_->set_left_pans(!walking);
        canvas_->set_overlay([this](ImDrawList* dl) {
            draw_note_pins(dl);  // IND-48
            draw_plan_overlay(dl);
        });
        canvas_->set_grab_test([this](Point2 at, double tolerance) { return grab_note(at, tolerance); });
        canvas_->draw(*w, *c, state_.settings().layers, ImGui::GetContentRegionAvail());
        handle_canvas_input();
    }
    ImGui::EndChild();
}

namespace {

// IND-04: the floor whose tab the user dragged and by how many places
// (+ up, - down), comparing the tab bar's order with the floors' order.
std::optional<std::pair<std::string, int>> dragged_floor(const ImGuiTabBar& bar, const std::vector<FloorTab>& tabs,
                                                         const std::vector<ImGuiID>& ids) {
    std::vector<std::size_t> shown;  // the floors' indices in the bar's order
    for (const ImGuiTabItem& item : bar.Tabs) {
        const auto it = std::find(ids.begin(), ids.end(), item.ID);
        if (it != ids.end()) shown.push_back(static_cast<std::size_t>(it - ids.begin()));
    }
    if (shown.size() != tabs.size()) return std::nullopt;  // a tab is new or gone
    std::size_t a = 0;
    while (a < shown.size() && shown[a] == a) ++a;
    if (a == shown.size()) return std::nullopt;
    std::size_t b = shown.size() - 1;
    while (shown[b] == b) --b;
    // One tab moved from b to a (left, down) or from a to b (right, up).
    const int places = static_cast<int>(b - a);
    if (shown[a] == b) return std::make_pair(tabs[b].id, -places);
    return std::make_pair(tabs[a].id, places);
}

}  // namespace

void Gui::draw_floor_tabs(ProjectWindow& w) {
    const std::string model = w.selected();
    const bool force = model != shown_floor_;
    // A floor chosen by a command (added, imported, removed) is shown
    // instead of the About building tab.
    if (force && !model.empty() && state_.about_tab()) state_.show_about_tab(false);
    std::string visible;
    // IND-04: the tabs are reorderable: a floor is moved by dragging its tab.
    // A frame without the flag after the floors' order changed: Dear ImGui
    // then sorts the tabs by their order of the last frame (BeginTabBarEx).
    const ImGuiTabBarFlags bar_flags = ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                       (tab_resort_ ? 0 : ImGuiTabBarFlags_Reorderable);
    tab_resort_ = false;
    if (ImGui::BeginTabBar("###floors", bar_flags)) {
        // IND-47: the About building tab, always the first one; it cannot be
        // moved (leading, no reorder) or removed (no close button, no menu).
        const bool about_model = state_.about_tab();
        const bool about_force = about_model != about_shown_;
        const bool about_open =
            ImGui::BeginTabItem(tr("About building###tab.about"), nullptr,
                                ImGuiTabItemFlags_Leading | ImGuiTabItemFlags_NoReorder |
                                    (about_force && about_model ? ImGuiTabItemFlags_SetSelected : 0));
        text_tooltip(tr("The building: address and map, photo, surveyor, survey date and notes. The report starts with it."));
        building_.tab = true;
        building_.tab_first = true;  // drawn before every floor tab
        if (about_open) ImGui::EndTabItem();
        const std::vector<FloorTab>& tabs = w.tabs();
        std::vector<ImGuiID> tab_ids;
        tab_ids.reserve(tabs.size());
        std::optional<std::pair<std::string, int>> move;  // from the tab's menu
        for (std::size_t i = 0; i < tabs.size(); ++i) {
            const FloorTab& t = tabs[i];
            const ImGuiTabItemFlags f =
                ((force || (about_force && !about_model)) && t.id == model && !about_model) ? ImGuiTabItemFlags_SetSelected : 0;
            const bool open = ImGui::BeginTabItem((t.label + "###tab." + t.id).c_str(), nullptr, f);
            tab_ids.push_back(ImGui::GetItemID());
            text_tooltip(t.tooltip.c_str());
            // The tab's menu (right click): Move down, Move up, Remove....
            if (ImGui::BeginPopupContextItem(("###floor_menu." + t.id).c_str())) {
                // Touch layout: entries of a touch target's height, as in the menus (UI-13).
                const ImVec2 sp = ImGui::GetStyle().ItemSpacing;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                    ImVec2(sp.x, layout().touch ? std::max(sp.y, px(static_cast<float>(kTouchTargetPx)) -
                                                                               ImGui::GetFontSize() + 0.5F)
                                                                : sp.y));
                const bool lowest = i == 0;
                const bool highest = i + 1 == tabs.size();
                if (ImGui::MenuItem(tr("Move down###floor.down"), nullptr, false, !lowest)) move.emplace(t.id, -1);
                text_tooltip(tr("Move the floor one place down in the level order (the floors swap levels)."));
                if (ImGui::MenuItem(tr("Move up###floor.up"), nullptr, false, !highest)) move.emplace(t.id, +1);
                text_tooltip(tr("Move the floor one place up in the level order (the floors swap levels)."));
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Remove...###floor.remove"))) (void)state_.request_remove_floor(t.id);
                text_tooltip(tr("Remove the floor with its walk tests (you are asked first)."));
                ImGui::PopStyleVar();
                ImGui::EndPopup();
            }
            if (open) {
                visible = t.id;
                ImGui::EndTabItem();
            }
        }
        // A tab dragged to another place (applied when the mouse is up):
        // the same model call as the menu, once per drag.
        ImGuiTabBar* bar = ImGui::GetCurrentTabBar();
        if (bar != nullptr && bar->ReorderRequestTabId != 0) tab_dragged_ = true;
        if (tab_dragged_ && bar != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && bar->ReorderRequestTabId == 0) {
            tab_dragged_ = false;
            if (!move) move = dragged_floor(*bar, tabs, tab_ids);
        }
        if (move && !state_.move_floor(move->first, move->second).ok)
            tab_resort_ = true;  // refused: back to the floors' order
        // A floor moved: Dear ImGui keeps a tab bar's own order, so have it
        // sort the tabs by this frame's order on the next frame.
        std::string order;
        for (const FloorTab& t : w.tabs()) order += t.id + '\n';
        if (order != tab_order_) {
            tab_resort_ = true;
            tab_order_ = order;
        }
        // IND-10: "+" next to the floor tabs.
        const ActionView add = state_.action(Action::AddFloor);
        if (ImGui::TabItemButton("+###floors.add", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            run(Action::AddFloor);
        item_tooltip(add);
        ImGui::EndTabBar();
        // A click on the About tab or on a floor's tab (not a command's choice).
        if (!about_force && about_open != about_model) state_.show_about_tab(about_open);
        about_shown_ = about_force ? about_model : about_open;
    }
    // A tab the user clicked selects its floor; a floor selected by a
    // command (added, removed) is shown on the next frame.
    if (!force && !visible.empty() && visible != model) (void)w.select(visible);
    shown_floor_ = force || visible.empty() ? model : visible;
}

namespace {

// The width of a status item with its separator ("|") before it.
float separated_width(const std::string& text) {
    const ImGuiStyle& st = ImGui::GetStyle();
    return ImGui::CalcTextSize("|").x + 2.0F * st.ItemSpacing.x + ImGui::CalcTextSize(text.c_str()).x;
}

}  // namespace

namespace {

// The status items (radio, recording, ...) on lines of `avail` width: the
// number of lines they need when they follow `first` pixels on the first.
int item_lines(const std::vector<StatusPiece>& pieces, float first, float avail) {
    int lines = 1;
    float x = first;
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        const float w = separated_width(pieces[i].text);
        if (x + w > avail && x > 0.0F) {
            ++lines;
            x = ImGui::CalcTextSize(pieces[i].text.c_str()).x;
        } else {
            x += w;
        }
    }
    return lines;
}

// The width the status items take on one line.
float items_width(const std::vector<StatusPiece>& pieces) {
    float w = 0.0F;
    for (std::size_t i = 1; i < pieces.size(); ++i) w += separated_width(pieces[i].text);
    return w;
}

}  // namespace

// The message on the left, shortened with "..." (its whole text on hover)
// when the items need the room; the items on the right of it, or on lines
// of their own when even they do not fit one line (TST-52).
float Gui::status_height(const std::vector<StatusPiece>& pieces, float width) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float avail = width - 2.0F * st.WindowPadding.x;
    const float line = ImGui::GetTextLineHeight();
    const float items = items_width(pieces);
    // The message keeps at least a quarter of the line next to the items.
    const int lines = items <= avail * 0.75F ? 1 : 1 + item_lines(pieces, 0.0F, avail);
    return static_cast<float>(lines - 1) * (line + st.ItemSpacing.y) + ImGui::GetFrameHeight() + 6.0F;
}

void Gui::draw_status_bar(const std::vector<StatusPiece>& pieces, float h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("###status_bar", nullptr, flags)) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float right = ImGui::GetCursorScreenPos().x + avail;
        const float items = items_width(pieces);
        const bool one_line = items <= avail * 0.75F;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const StatusPiece& p = pieces[i];
            if (p.colour != 0U) ImGui::PushStyleColor(ImGuiCol_Text, p.colour);
            if (i == 0) {
                text_ellipsis(p.text.c_str(), one_line ? avail - items : avail);
                text_tooltip(p.text.c_str());
            } else if (ImGui::GetItemRectMax().x + separated_width(p.text) <= right + 0.5F) {
                ImGui::SameLine();
                ImGui::TextDisabled("|");
                ImGui::SameLine();
                ImGui::TextUnformatted(p.text.c_str());
            } else {
                ImGui::TextUnformatted(p.text.c_str());
            }
            if (p.colour != 0U) ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

void Gui::draw_new_project() {
    NewProjectForm& f = state_.new_project_form();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextDisabled(tr("One project file holds one building: its floors, plans and walk tests."));
    ImGui::PopTextWrapPos();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    input_text(tr("Project name###name"), f.name);
    text_tooltip(tr("The building or project name, shown in the window title and in reports (required)."));
    input_text(tr("Site###site"), f.site);
    text_tooltip(tr("Where the building is: address or site name, for the report."));
    input_text(tr("Operator###operator"), f.operator_name);
    text_tooltip(tr("The network operator the survey is made for."));
    std::string notes = f.notes;
    std::string buf = notes;
    buf.resize(std::max<std::size_t>(4096, notes.size() + 1), '\0');
    if (ImGui::InputTextMultiline(tr("Notes###notes"), buf.data(), buf.size(),
                                  ImVec2(0.0F, ImGui::GetTextLineHeight() * 4.0F)))
        f.notes = std::string(buf.c_str());
    text_tooltip(tr("Free text for the report, e.g. the purpose of the survey."));
    std::string file = f.file.string();
    if (input_text(tr("File###file"), file)) f.file = file;
    text_tooltip(tr("Where the project file is created (.yantra is added when missing)."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Browse...###browse"))) state_.browse(BrowsePurpose::NewProjectFile);
    text_tooltip(tr("Choose the folder and file name in the file browser."));
    ImGui::Spacing();
    if (dialog_button(tr("Create###dlg.ok"), tr("Create the project file, then add the first floor [Enter]")) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter))
        (void)state_.confirm_new_project();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without creating a project [Esc]")) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_browser() {
    FileBrowser* b = state_.browser();
    if (b == nullptr) return;
    // Navigation row: parent, home, the folder (typed path fallback).
    ImGui::BeginDisabled(!b->can_go_up());
    if (ImGui::Button(tr("Up###fb.up"))) b->go_up();
    ImGui::EndDisabled();
    text_tooltip(tr("Open the parent folder [Backspace]"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Home###fb.home"))) b->go_home();
    text_tooltip(tr("Open your home folder."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh###fb.refresh"))) b->refresh();
    text_tooltip(tr("List the folder again."));
    ImGui::SameLine();
    std::string folder = b->folder().string();
    ImGui::SetNextItemWidth(-1.0F);
    std::string fbuf = folder;
    fbuf.resize(std::max<std::size_t>(1024, folder.size() + 1), '\0');
    if (ImGui::InputText("###fb.folder", fbuf.data(), fbuf.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        (void)b->navigate(std::string(fbuf.c_str()));
    text_tooltip(tr("The current folder. Type a folder and press Enter to open it."));

    const float list_h = ImGui::GetTextLineHeightWithSpacing() * 14.0F;
    ImGui::BeginChild("###fb.places", ImVec2(em(180.0F), list_h), ImGuiChildFlags_Borders);
    ImGui::TextDisabled(tr("Places"));
    // Ids by position (a path's separators would make it a window path);
    // a touch target's height in the touch layout (UI-13).
    const std::vector<BrowserPlace> places = b->places();
    for (std::size_t i = 0; i < places.size(); ++i) {
        const BrowserPlace& p = places[i];
        if (ImGui::Selectable((p.label + "###fb.place." + std::to_string(i)).c_str(), p.path == b->folder(), 0,
                              ImVec2(0.0F, row_h())))
            (void)b->navigate(p.path);
        text_tooltip(p.path.string().c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("###fb.list", ImVec2(0.0F, list_h), ImGuiChildFlags_Borders);
    if (!b->folder_error().empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kErrorColour, "%s", b->folder_error().c_str());
        ImGui::PopTextWrapPos();
    }
    if (b->entries().empty() && b->folder_error().empty())
        ImGui::TextDisabled(tr("No folders or matching files here."));
    if (ImGui::BeginTable("###fb.table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Name"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Size"), ImGuiTableColumnFlags_WidthFixed, em(90.0F));
        std::optional<std::size_t> activate;
        for (std::size_t i = 0; i < b->entries().size(); ++i) {
            const BrowserEntry& e = b->entries()[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = (e.directory ? "[folder]  " : "") + e.name + "###fb.e." + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), b->selected() == i,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0F, row_h()))) {
                b->select(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || e.directory) activate = i;
            }
            text_tooltip(e.directory ? tr("Open this folder.") : tr("Choose this file; a double click opens it."));
            ImGui::TableNextColumn();
            if (!e.directory) ImGui::TextDisabled("%s", format_size(e.size).c_str());
        }
        ImGui::EndTable();
        if (activate) (void)state_.browser_activate(*activate);
    }
    ImGui::EndChild();
    b = state_.browser();
    if (b == nullptr) return;  // a file was chosen

    // File name (typed path fallback), filter, buttons.
    std::string name = b->file_name();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
    if (input_text(tr("File name###fb.name"), name)) b->set_file_name(name);
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    text_tooltip(tr("A file name in the current folder, or a full path (\"~/\" for your home folder). A folder opens "
                 "it."));
    std::vector<const char*> filters;
    for (const FileFilter& f : b->filters()) filters.push_back(f.label.c_str());
    int active = static_cast<int>(b->active_filter());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
    if (ImGui::Combo(tr("Show###fb.filter"), &active, filters.data(), static_cast<int>(filters.size())))
        b->set_filter(static_cast<std::size_t>(active));
    text_tooltip(tr("Which files are listed (folders are always listed)."));
    ImGui::SameLine();
    bool hidden = b->show_hidden();
    if (ImGui::Checkbox(tr("Hidden###fb.hidden"), &hidden)) b->set_show_hidden(hidden);
    text_tooltip(tr("Also list hidden files and folders (names starting with a dot)."));
    ImGui::Spacing();
    const bool save = b->mode() == BrowseMode::Save;
    bool ok = dialog_button(save ? tr("Save###dlg.ok") : tr("Open###dlg.ok"),
                            save ? tr("Use this file name [Enter]") : tr("Open the selected file [Enter]"));
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close the file browser [Esc]")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Escape) && !b->pending_overwrite())) {
        state_.browser_cancel();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !ImGui::GetIO().WantTextInput) b->go_up();
    if (enter || (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput)) ok = true;
    if (ok && !b->pending_overwrite()) (void)state_.browser_confirm();
    if (state_.dialog() == Dialog::Browse && state_.browser() != nullptr) error_line(state_);

    // Overwrite confirmation (Save).
    b = state_.browser();
    if (b != nullptr && b->pending_overwrite()) {
        if (!ImGui::IsPopupOpen(tr("Replace file?###dlg.overwrite"))) ImGui::OpenPopup(tr("Replace file?###dlg.overwrite"));
        if (ImGui::BeginPopupModal(tr("Replace file?###dlg.overwrite"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text(tr("%s already exists."), b->pending_overwrite()->filename().string().c_str());
            ImGui::TextDisabled("%s", b->pending_overwrite()->parent_path().string().c_str());
            ImGui::TextUnformatted(tr("Replace it? The old file is lost."));
            if (dialog_button(tr("Replace###ow.yes"), tr("Replace the existing file"))) {
                ImGui::CloseCurrentPopup();
                (void)state_.browser_overwrite(true);
            }
            ImGui::SameLine();
            if (dialog_button(tr("Keep it###ow.no"), tr("Do not replace; choose another name [Esc]")) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                (void)state_.browser_overwrite(false);
            }
            ImGui::EndPopup();
        }
    }
}

void Gui::draw_add_floor() {
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(tr("Choose the plan of the new floor: a PDF or image file, or a sheet the project already "
                           "has (several floors can be trimmed from one sheet). You then trim the floor in the plan "
                           "import window."));
    ImGui::PopTextWrapPos();
    ImGui::SeparatorText(tr("New plan file"));
    if (dialog_button(tr("Choose a plan file...###af.plan_file"),
                      tr("Pick a PDF or image file (PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR). It is copied into the "
                      "project; the file itself is never changed."),
                      220.0F))
        state_.browse(BrowsePurpose::PlanFile);
    ImGui::SeparatorText(tr("Plan already in the project"));
    const std::vector<SourceChoice> sources = state_.plan_sources();
    if (sources.empty()) ImGui::TextDisabled(tr("None yet."));
    for (const SourceChoice& s : sources) {
        ImGui::BeginDisabled(!s.available);
        if (ImGui::Button((tr("Use###af.src.") + s.id).c_str())) (void)state_.import_plan_source(s.id);
        ImGui::EndDisabled();
        text_tooltip(s.available ? tr("Trim another floor from this plan.") : s.reason.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(s.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", s.detail.c_str());
    }
    ImGui::SeparatorText(tr("Without a plan"));
    help_marker(tr("A blank floor: its plan can be added later. Useful for a level that only needs walk-test points."));
    AddFloorForm& f = state_.add_floor_form();
    input_text(tr("Name###name"), f.name);
    text_tooltip(tr("For example \"Ground floor\" or \"Basement\"."));
    ImGui::InputInt(tr("Level##floor###level"), &f.level);
    text_tooltip(tr("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level."));
    float height = static_cast<float>(f.height_m);
    if (ImGui::InputFloat(tr("Height (m)###height"), &height, 0.1F, 0.5F, "%.2f")) f.height_m = static_cast<double>(height);
    text_tooltip(tr("Floor-to-floor height in metres (more than 0, at most 50)."));
    ImGui::Spacing();
    if (dialog_button(tr("Add blank floor###dlg.ok"), tr("Add the floor without a plan [Enter]"), 180.0F) ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        (void)state_.confirm_add_floor();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without adding a floor [Esc]")) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_plan_import() {
    PlanImportWindow* w = state_.plan_import();
    if (w == nullptr) {
        state_.close_plan_import();
        return;
    }
    if (!import_view_.draw(state_, *w)) state_.close_plan_import();
}

void Gui::draw_logo(UserTexture& tex, std::string& loaded, const std::filesystem::path& file, float size) {
    if (file.empty()) return;
    if (loaded != file.string()) {
        loaded = file.string();
        tex.reset();
        std::string error;
        if (const auto r = load_data_png({file.parent_path()}, file.filename(), error)) tex.set(r->width, r->height, r->rgba.data());
    }
    if (!tex.valid()) return;
    ImGui::Image(tex.ref(), ImVec2(size, size * static_cast<float>(tex.height()) / static_cast<float>(std::max(1, tex.width()))));
}

void Gui::draw_about() {
    const AboutView a = state_.about();
    about_.shown = true;
    // BRD-06: the Yantra logo next to the name.
    draw_logo(yantra_logo_, yantra_logo_file_, a.yantra_logo, 64.0F);
    about_.yantra_logo = yantra_logo_.valid();
    if (yantra_logo_.valid()) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    ImGui::Text("%s %s", a.name.c_str(), a.version.c_str());
    ImGui::PopFont();
    ImGui::TextUnformatted(tr("Indoor coverage survey"));
    if (!a.build_date.empty()) ImGui::Text(tr("Built %s"), a.build_date.c_str());
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Text(tr("Licence: %s"), a.licence.c_str());
    if (!a.source_url.empty()) {
        ImGui::Text(tr("Source code: %s"), a.source_url.c_str());
        help_marker(a.source_note.c_str());
    }
    ImGui::Spacing();
    // BRD-01, BRD-03: the curtat.be logo (light or dark PNG by theme), the link and the statement.
    draw_logo(curtat_logo_, curtat_logo_file_, a.curtat_logo, 48.0F);
    about_.curtat_logo = curtat_logo_.valid();
    about_.curtat_file = a.curtat_logo.filename().string();
    if (curtat_logo_.valid()) ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(a.statement.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Text(tr("Homepage: %s"), a.homepage.c_str());
    ImGui::EndGroup();
    ImGui::Spacing();
    // Credits and plugins (UI-15, NFR-01, PLG-09), scrolled.
    if (ImGui::BeginChild("###about.credits", ImVec2(0.0F, ImGui::GetFontSize() * 14.0F), ImGuiChildFlags_Borders)) {
        wrap_texts();
        auto table = [](const char* id, const std::vector<CreditRow>& rows, const char* first) {
            if (rows.empty()) return;
            if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
                return;
            ImGui::TableSetupColumn(first);
            ImGui::TableSetupColumn(tr("Licence"));
            ImGui::TableSetupColumn(tr("Source"));
            headers_row();
            for (const CreditRow& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", r.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", r.licence.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", r.source.c_str());
            }
            ImGui::EndTable();
        };
        ImGui::SeparatorText(tr("Third-party components"));
        if (!a.credits.error.empty()) ImGui::TextDisabled("%s", a.credits.error.c_str());
        table("###about.libraries", a.credits.libraries, tr("Component"));
        ImGui::SeparatorText(tr("Data"));
        table("###about.data", a.credits.data, tr("Item"));
        ImGui::SeparatorText(tr("Loaded plugins"));
        if (a.plugins.empty()) ImGui::TextDisabled(tr("No plugins are loaded."));
        if (!a.plugins.empty() &&
            ImGui::BeginTable("###about.plugins", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("Plugin"));
            ImGui::TableSetupColumn(tr("Version"));
            ImGui::TableSetupColumn(tr("Licence"));
            ImGui::TableSetupColumn(tr("Status"));
            headers_row();
            for (const PluginRowView& p : a.plugins) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s (%s)", p.name.c_str(), p.kind.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(p.version.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", p.licence.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(p.status.c_str());
            }
            ImGui::EndTable();
        }
        if (!a.plugins_note.empty()) ImGui::TextDisabled("%s", a.plugins_note.c_str());
    }
    ImGui::EndChild();
    about_.libraries = a.credits.libraries.size();
    about_.data = a.credits.data.size();
    about_.plugins = a.plugins.size();
    ImGui::Spacing();
    if (dialog_button(tr("Close###dlg.cancel"), tr("Close [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter))
        state_.cancel_dialog();
}

void Gui::draw_floor_properties() {
    FloorForm& f = state_.floor_properties_form();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    input_text(tr("Name###name"), f.name);
    text_tooltip(tr("For example \"Ground floor\" or \"Basement\"."));
    ImGui::InputInt(tr("Level##floor###level"), &f.level);
    text_tooltip(tr("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level; the tabs are "
                 "ordered by level."));
    float height = static_cast<float>(f.height_m);
    if (ImGui::InputFloat(tr("Height (m)###height"), &height, 0.1F, 0.5F, "%.2f")) f.height_m = static_cast<double>(height);
    text_tooltip(tr("Floor-to-floor height in metres (more than 0, at most 50)."));
    ImGui::Spacing();
    if (dialog_button(tr("Apply###dlg.ok"), tr("Store the changes [Enter]")) ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        (void)state_.confirm_floor_properties();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###dlg.cancel"), tr("Close without changes [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_remove_floor() {
    ProjectWindow* w = state_.project_window();
    if (w == nullptr || !w->pending_remove()) {
        state_.cancel_dialog();
        return;
    }
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(w->pending_remove()->question.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (dialog_button(tr("Remove###dlg.ok"), tr("Remove the floor and everything on it")))
        (void)state_.confirm_remove_floor();
    ImGui::SameLine();
    if (dialog_button(tr("Keep it###dlg.cancel"), tr("Do not remove the floor [Esc]")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

namespace {

// DOC-07 e: the manual section of each dialog (manual.h).
const char* dialog_section(Dialog d) {
    switch (d) {
    case Dialog::NewProject:
    case Dialog::Browse:
    case Dialog::AddFloor:
    case Dialog::FloorProperties:
    case Dialog::RemoveFloor: return "13.1";
    case Dialog::PlanImport: return "13.2";
    case Dialog::About: return "1.2";
    case Dialog::Camera: return "13.6";
    case Dialog::Note: return "13.7";
    case Dialog::InterpolationSettings: return "13.5";
    case Dialog::Settings: return "15";
    case Dialog::ResetSettings: return "15.4";
    case Dialog::TourConsent: return "1.3";
    case Dialog::Export: return "10.1";
    case Dialog::Report:
    case Dialog::ReportReady: return "10.2";
    case Dialog::AcceptanceProfiles: return "10.3";
    case Dialog::Sharing: return "10.4";
    case Dialog::None: break;
    }
    return "1";
}

}  // namespace

void Gui::draw_dialog_body(Dialog d) {
    if (d != Dialog::PlanImport) help_button(dialog_section(d));
    // DEMO-11: the tour goes on inside a dialog (the dialog blocks the
    // tour window behind it).
    if (d != Dialog::TourConsent && state_.tour().active()) {
        // DEMO-15: the tour's strip in a green box.
        const gui_detail::DemoColours dc = gui_detail::demo_colours(gui_detail::layout().theme);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, dc.bg);
        gui_detail::push_demo_text(dc);
        ImGui::BeginChild("###tour_strip", ImVec2(0.0F, 0.0F),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        gui_detail::wrap_texts();
        draw_tour_body(state_.tour().view(), true);
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
    }
    switch (d) {
    case Dialog::NewProject: draw_new_project(); break;
    case Dialog::Browse: draw_browser(); break;
    case Dialog::AddFloor: draw_add_floor(); break;
    case Dialog::PlanImport: draw_plan_import(); break;
    case Dialog::About: draw_about(); break;
    case Dialog::Camera: draw_camera_dialog(); break;
    case Dialog::Note: draw_note_dialog(); break;
    case Dialog::FloorProperties: draw_floor_properties(); break;
    case Dialog::RemoveFloor: draw_remove_floor(); break;
    case Dialog::InterpolationSettings: draw_interpolation_dialog(); break;
    case Dialog::Settings: draw_settings(); break;
    case Dialog::ResetSettings: draw_reset_settings(); break;
    case Dialog::TourConsent: draw_tour_consent(); break;
    case Dialog::Export: draw_export_dialog(); break;
    case Dialog::Report: draw_report_dialog(); break;
    case Dialog::AcceptanceProfiles: draw_profiles_dialog(); break;
    case Dialog::Sharing: draw_sharing_dialog(); break;
    case Dialog::ReportReady: draw_report_ready(); break;
    case Dialog::None: break;
    }
}

void Gui::draw_dialogs() {
    const Dialog d = state_.dialog();
    if (shown_ == Dialog::PlanImport && d != Dialog::PlanImport) {
        // The plan import window closed: drop its textures; the project
        // window reopens the plan tiles (a trim may have changed).
        import_view_.close();
        canvas_->close();
    }
    if (d == Dialog::None) {
        shown_ = Dialog::None;
        return;
    }
    const char* id = dialog_id(d, state_);
    if (shown_ != d) {
        ImGui::OpenPopup(id);
        shown_ = d;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (d == Dialog::PlanImport) {
        // The plan import window takes nearly the whole app window.
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.96F, vp->WorkSize.y * 0.94F), ImGuiCond_Appearing);
    } else {
        // Dialogs fit small windows too (UI-13).
        const bool wide = d == Dialog::Browse || d == Dialog::Settings || d == Dialog::About ||
                          d == Dialog::Export || d == Dialog::Report;
        const float width = std::min(em(wide ? 820.0F : 600.0F), vp->WorkSize.x * 0.95F);
        // Dialogs whose content grows (the settings tabs) fit their height
        // every frame.
        const bool grows = d == Dialog::Settings ||
                           d == Dialog::Export || d == Dialog::Report || d == Dialog::AcceptanceProfiles ||
                           d == Dialog::Sharing || d == Dialog::Camera || d == Dialog::Note;
        ImGui::SetNextWindowSize(ImVec2(width, 0.0F), grows ? ImGuiCond_Always : ImGuiCond_Appearing);
    }
    // The camera dialog grows when the preview starts: kept centred.
    if (const NoteForm* nf = state_.notes() != nullptr ? state_.notes()->form() : nullptr; d == Dialog::Note && nf != nullptr) {
        // IND-48: anchored at the note's spot on the plan: beside it, on
        // the side with more room.
        // The window stays inside the app window (its size of the last
        // frame; the first frame: its width and a guess).
        const ImVec2 a = canvas_ ? canvas_->screen_of(nf->plan) : vp->GetCenter();
        const float gap = em(18.0F);
        const float width = std::min(em(600.0F), vp->WorkSize.x * 0.95F);
        const ImVec2 size(note_window_size_.x > 0.0F ? note_window_size_.x : width,
                          note_window_size_.y > 0.0F ? note_window_size_.y : std::min(em(420.0F), vp->WorkSize.y * 0.9F));
        const bool right = a.x < vp->WorkPos.x + vp->WorkSize.x * 0.5F;
        const bool below = a.y < vp->WorkPos.y + vp->WorkSize.y * 0.5F;
        ImVec2 at(right ? a.x + gap : a.x - gap - size.x, below ? a.y + gap : a.y - gap - size.y);
        at.x = std::clamp(at.x, vp->WorkPos.x, std::max(vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x - size.x));
        at.y = std::clamp(at.y, vp->WorkPos.y, std::max(vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y - size.y));
        ImGui::SetNextWindowPos(at, ImGuiCond_Always);
        notes_.anchor = a;
    } else {
        ImGui::SetNextWindowPos(vp->GetCenter(), d == Dialog::Camera ? ImGuiCond_Always : ImGuiCond_Appearing,
                                ImVec2(0.5F, 0.5F));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0F, 0.0F), ImVec2(vp->WorkSize.x, vp->WorkSize.y * 0.98F));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        // Closed by ImGui (it should not happen for modals without a close
        // button): give up the dialog.
        state_.cancel_dialog();
        shown_ = Dialog::None;
        return;
    }
    wrap_texts();
    draw_dialog_body(d);
    draw_help_popup();  // DOC-07 e: over the dialog
    // A command moved to another dialog (or closed it): close this popup;
    // the next frame opens the next one.
    if (state_.dialog() != d) {
        ImGui::CloseCurrentPopup();
        shown_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void Gui::draw() {
    survey_ = SurveyOverlay{};
    tour_ = TourOverlay{};
    about_ = AboutOverlay{};
    building_ = BuildingOverlay{};
    camera_ = CameraOverlay{};
    notes_ = NotesOverlay{};
    UserTexture::collect();
    state_.tick();
    if (state_.ui_generation() != ui_generation_) {
        ui_generation_ = state_.ui_generation();
        restart_ui();
    }
    // The settings: theme, touch layout and scale (UI-13), tooltips (DOC-07 h).
    if (const Style st = style_of(state_.settings()); !(st == style_)) {
        style_ = st;
        apply_style(style_);
    }
    ImGui::GetStyle().HoverDelayNormal = static_cast<float>(state_.settings().tooltip_delay_s);
    set_tooltips_shown(state_.settings().tooltips);
    // DOC-07 e: F1 opens the help of the window in front (last frame's).
    const std::string f1 = help_section_;
    help_section_.clear();
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) (void)state_.open_help(f1);
    handle_shortcuts();
    draw_menu_bar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = vp->WorkPos.y;
    const std::vector<StatusPiece> status = status_pieces();
    const float status_h = status_height(status, vp->WorkSize.x);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y - status_h - top));
    // No title, frame or resizing; a vertical scroll bar only when the
    // project screen is taller than the window.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0F, 20.0F));
    if (ImGui::Begin(kMainWindow, nullptr, flags)) {
        wrap_texts();
        if (state_.screen() == Screen::Start) {
            canvas_->close();
            shown_floor_.clear();
            draw_start_screen();
        } else {
            draw_project_screen();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    draw_status_bar(status, status_h);
    draw_radio_windows();
    draw_network_windows();
    draw_survey_windows();
    draw_plugin_windows();
    draw_tour();
    draw_dialogs();
    draw_help_window();
    draw_alerts_window();
    if (state_.tour().active()) draw_tour_highlight(state_.tour().view());
}

}  // namespace yantra::app
