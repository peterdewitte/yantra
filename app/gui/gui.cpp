// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ImGui drawing of the app shell, see gui.h.
// Copied from yantra-v2 68ea20a: app/src/gui.cpp, adapted.
#include "gui.h"

#include <imgui.h>
#include <imgui_internal.h>  // ImGuiTabBar::TabsAddedNew (tab order)

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

namespace yantra::app {

namespace {

constexpr float kTouchTarget = 48.0F;  // UI-13: >= 48 px targets
const ImVec4 kBannerColour{0.95F, 0.62F, 0.07F, 1.0F};
const ImVec4 kErrorColour{1.0F, 0.45F, 0.40F, 1.0F};

/// The hover text of the last item, also when it is disabled (DOC-07 b, c).
void item_tooltip(const ActionView& a) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(a.tooltip.c_str());
    if (!a.enabled) {
        ImGui::Spacing();
        ImGui::TextColored(kBannerColour, "%s", a.disabled_reason.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void text_tooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayNormal)) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// A dimmed "(?)" marker with an explanation on hover (DOC-07 a).
void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0F);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

std::string item_label(const ActionView& a) { return a.label + "###" + a.id; }

/// ImGui::InputText on a std::string (no imgui_stdlib needed).
bool input_text(const char* label, std::string& s, std::size_t capacity = 1024) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputText(label, buf.data(), buf.size());
    if (changed) s = std::string(buf.c_str());
    return changed;
}

bool dialog_button(const char* label, const char* tooltip, float width = 140.0F) {
    const bool pressed = ImGui::Button(label, ImVec2(width, kTouchTarget * 0.8F));
    text_tooltip(tooltip);
    return pressed;
}

void error_line(const AppState& state) {
    if (!state.status_is_error()) return;
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextColored(kErrorColour, "%s", state.status().c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace

const char* dialog_id(Dialog d, const AppState& state) {
    switch (d) {
    case Dialog::NewProject: return "New indoor project###dlg.new_project";
    case Dialog::Browse:
        switch (state.browse_purpose()) {
        case BrowsePurpose::OpenProject: return "Open project###dlg.browse";
        case BrowsePurpose::SaveAs: return "Save project as###dlg.browse";
        case BrowsePurpose::NewProjectFile: return "New project file###dlg.browse";
        }
        break;
    case Dialog::AddFloor: return "Add floor###dlg.add_floor";
    case Dialog::About: return "About Yantra###dlg.about";
    case Dialog::FloorProperties: return "Floor properties###dlg.floor_properties";
    case Dialog::RemoveFloor: return "Remove floor?###dlg.remove_floor";
    case Dialog::None: break;
    }
    return "###dlg.none";
}

Gui::Gui(AppState& state) : state_(state), canvas_(std::make_unique<PlanCanvas>()) {}

Gui::~Gui() {
    canvas_.reset();
    UserTexture::release_all();
}

void Gui::set_wait_for_tiles(bool on) { canvas_->set_wait_for_tiles(on); }

void Gui::apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0F;
    s.FrameRounding = 4.0F;
    s.FramePadding = ImVec2(10.0F, 7.0F);
    s.ItemSpacing = ImVec2(10.0F, 8.0F);
    s.HoverDelayNormal = 0.5F;  // DOC-07 h default; a setting in step 9.7
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
    for (const Menu& m : state_.menus()) {
        if (!ImGui::BeginMenu((m.label + "###menu." + m.id).c_str())) continue;
        for (const MenuEntry& e : m.entries) {
            switch (e.kind) {
            case MenuEntry::Kind::Separator: ImGui::Separator(); break;
            case MenuEntry::Kind::Recent: {
                const auto recent = state_.recent_projects();
                if (ImGui::BeginMenu("Recent projects###file.recent", !recent.empty())) {
                    for (const RecentView& r : recent) {
                        if (ImGui::MenuItem(r.label.c_str(), nullptr, false, r.exists))
                            (void)state_.open_project(r.path);
                        text_tooltip(r.exists ? r.path.string().c_str() : "This file no longer exists.");
                    }
                    ImGui::EndMenu();
                }
                if (recent.empty()) text_tooltip("Projects you open or create are listed here.");
                break;
            }
            case MenuEntry::Kind::Action:
                if (ImGui::MenuItem(item_label(e.item).c_str(), e.item.shortcut.empty() ? nullptr : e.item.shortcut.c_str(),
                                    false, e.item.enabled))
                    run(e.item.action);
                item_tooltip(e.item);
                break;
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Gui::draw_start_screen() {
    const StartScreenView v = state_.start_screen();
    const float column = std::min(ImGui::GetContentRegionAvail().x, 760.0F);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (ImGui::GetContentRegionAvail().x - column) * 0.5F));
    ImGui::BeginChild("###start_column", ImVec2(column, 0.0F));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.8F);
    ImGui::TextUnformatted(v.title.c_str());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(v.intro.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float button_w = (column - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
    int n = 0;
    for (const ActionView& a : v.actions) {
        if (n % 2 == 1) ImGui::SameLine();
        ImGui::BeginDisabled(!a.enabled);
        if (ImGui::Button((a.label + "###start." + a.id).c_str(), ImVec2(button_w, kTouchTarget * 1.25F))) run(a.action);
        ImGui::EndDisabled();
        item_tooltip(a);
        ++n;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Recent projects");
    help_marker("The projects you opened or created last, newest first. A greyed-out entry no longer exists where it "
                "was; the list is kept in your settings.");
    if (v.recent.empty()) {
        ImGui::TextDisabled("None yet: projects you create or open are listed here.");
    } else {
        int i = 0;
        for (const RecentView& r : v.recent) {
            ImGui::BeginDisabled(!r.exists);
            if (ImGui::Selectable((r.label + "###start.recent." + std::to_string(i++)).c_str(), false, 0,
                                  ImVec2(column, std::max(ImGui::GetTextLineHeight() + 6.0F, kTouchTarget * 0.75F))))
                (void)state_.open_project(r.path);
            ImGui::EndDisabled();
            text_tooltip(r.exists ? r.path.string().c_str() : "This file no longer exists.");
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
        text_tooltip("This is a demo project: its measurements are simulated, not real. Its exports and reports carry "
                     "the same label.");
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3F);
    ImGui::TextUnformatted(p->name.c_str());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", p->path.string().c_str());
    text_tooltip(p->path.string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("- %s", p->file_note.c_str());
    help_marker("A project file is live: every change is written to it at once, so nothing is lost when Yantra "
                "stops, and there is nothing to discard. File > Save makes the file complete on its own (ready to "
                "copy); Save as continues with a copy. While it is open here, no other Yantra can open it.");

    draw_floor_tabs(*w);
    const FloorContent* c = w->content(state_.settings().layers.bin_size_m);
    if (c == nullptr) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("%s", state_.no_floor_hint().c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    // Floor commands (IND-04).
    const std::string id = c->floor_id;
    const auto& tabs = w->tabs();
    const auto it = std::find_if(tabs.begin(), tabs.end(), [&](const FloorTab& t) { return t.id == id; });
    const bool lowest = it == tabs.begin();
    const bool highest = it != tabs.end() && std::next(it) == tabs.end();
    if (ImGui::Button("Properties...###floor.properties")) (void)state_.edit_floor(id);
    text_tooltip("Rename the floor, or change its level number and floor-to-floor height.");
    ImGui::SameLine();
    ImGui::BeginDisabled(lowest);
    if (ImGui::Button("Move down###floor.down")) (void)state_.move_floor(id, -1);
    ImGui::EndDisabled();
    text_tooltip(lowest ? "Move the floor one place down in the level order. It is already the lowest."
                        : "Move the floor one place down in the level order (the floors swap levels).");
    ImGui::SameLine();
    ImGui::BeginDisabled(highest);
    if (ImGui::Button("Move up###floor.up")) (void)state_.move_floor(id, +1);
    ImGui::EndDisabled();
    text_tooltip(highest ? "Move the floor one place up in the level order. It is already the highest."
                         : "Move the floor one place up in the level order (the floors swap levels).");
    ImGui::SameLine();
    if (ImGui::Button("Remove...###floor.remove")) (void)state_.request_remove_floor(id);
    text_tooltip("Remove the floor with its walls, antennas and walk tests (you are asked first).");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (ImGui::Button("Fit###plan.fit")) canvas_->fit();
    text_tooltip("Show the whole floor [double click on the plan]");
    ImGui::SameLine();
    if (ImGui::Button("+###plan.zoom_in")) canvas_->zoom(1.25);
    text_tooltip("Zoom in [mouse wheel]");
    ImGui::SameLine();
    if (ImGui::Button("-###plan.zoom_out")) canvas_->zoom(0.8);
    text_tooltip("Zoom out [mouse wheel]");
    ImGui::SameLine();
    ImGui::TextDisabled("Drag to pan, wheel to zoom");

    // Layers on the left, the plan on the right (narrower on small windows).
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float panel = std::clamp(avail.x * 0.28F, 200.0F, 320.0F);
    ImGui::BeginChild("###layers", ImVec2(panel, avail.y), ImGuiChildFlags_Borders);
    draw_layer_panel(*w, c);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("###plan", ImVec2(0.0F, avail.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // The layer panel may have changed the content (another map chosen).
    c = w->content(state_.settings().layers.bin_size_m);
    if (c != nullptr) canvas_->draw(*w, *c, state_.settings().layers, ImGui::GetContentRegionAvail());
    ImGui::EndChild();
}

void Gui::draw_floor_tabs(ProjectWindow& w) {
    const std::string model = w.selected();
    const bool force = model != shown_floor_;
    std::string visible;
    if (ImGui::BeginTabBar("###floors", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) {
        for (const FloorTab& t : w.tabs()) {
            const ImGuiTabItemFlags f = (force && t.id == model) ? ImGuiTabItemFlags_SetSelected : 0;
            const bool open = ImGui::BeginTabItem((t.label + "###tab." + t.id).c_str(), nullptr, f);
            text_tooltip(t.tooltip.c_str());
            if (open) {
                visible = t.id;
                ImGui::EndTabItem();
            }
        }
        // A floor moved: Dear ImGui keeps a tab bar's first order unless a
        // tab is new, so have it sort the tabs by this frame's order.
        std::string order;
        for (const FloorTab& t : w.tabs()) order += t.id + '\n';
        if (order != tab_order_) {
            if (ImGuiTabBar* bar = ImGui::GetCurrentTabBar()) bar->TabsAddedNew = true;
            tab_order_ = order;
        }
        // IND-10: "+" next to the floor tabs.
        const ActionView add = state_.action(Action::AddFloor);
        if (ImGui::TabItemButton("+###floors.add", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            run(Action::AddFloor);
        item_tooltip(add);
        ImGui::EndTabBar();
    }
    // A tab the user clicked selects its floor; a floor selected by a
    // command (added, removed) is shown on the next frame.
    if (!force && !visible.empty() && visible != model) (void)w.select(visible);
    shown_floor_ = force || visible.empty() ? model : visible;
}

void Gui::draw_layer_panel(ProjectWindow& w, const FloorContent* c) {
    ImGui::SeparatorText("Layers");
    help_marker("Show or hide each layer; the arrows change the drawing order (the top of the list is drawn on "
                "top). These are your viewing settings: they do not change the project or its report.");
    for (const LayerRow& r : w.layer_rows(state_.settings().layers)) {
        ImGui::PushID(r.id.c_str());
        bool on = r.visible;
        if (ImGui::Checkbox((r.label + "###" + r.id).c_str(), &on)) state_.set_layer_visible(r.layer, on);
        text_tooltip(r.tooltip.c_str());
        const float arrows = ImGui::GetFrameHeight() * 2.0F + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - arrows));
        ImGui::BeginDisabled(!r.can_move_up);
        if (ImGui::ArrowButton(("###" + r.id + ".up").c_str(), ImGuiDir_Up)) (void)state_.move_layer(r.layer, +1);
        ImGui::EndDisabled();
        text_tooltip(r.can_move_up ? "Draw this layer above the next one." : "This layer is drawn on top.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!r.can_move_down);
        if (ImGui::ArrowButton(("###" + r.id + ".down").c_str(), ImGuiDir_Down)) (void)state_.move_layer(r.layer, -1);
        ImGui::EndDisabled();
        text_tooltip(r.can_move_down ? "Draw this layer below the previous one." : "This layer is drawn at the bottom.");
        if (!r.note.empty()) {
            ImGui::Indent();
            ImGui::PushTextWrapPos(0.0F);
            if (r.layer == Layer::Interpolation && c && c->interpolation && c->interpolation->stale)
                ImGui::TextColored(kBannerColour, "%s", r.note.c_str());
            else
                ImGui::TextDisabled("%s", r.note.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent();
        }
        if (r.layer == Layer::Binned && c != nullptr && c->metres_per_unit) {
            ImGui::Indent();
            float size = static_cast<float>(state_.settings().layers.bin_size_m);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
            if (ImGui::InputFloat("Square (m)###layer.bin_size", &size, 0.5F, 1.0F, "%.1f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                (void)state_.set_bin_size(static_cast<double>(size));
            text_tooltip("The side of the squares in metres (0.5 to 50). Each square shows the median of its points.");
            ImGui::Unindent();
        }
        if (r.layer == Layer::Interpolation && c != nullptr && c->interpolation) {
            ImGui::Indent();
            ImGui::TextDisabled("Fit: RMS %.1f dB over %zu points", c->interpolation->rms_db, c->interpolation->fit_points);
            if (c->interpolations.size() > 1) {
                ImGui::SetNextItemWidth(-1.0F);
                if (ImGui::BeginCombo("###layer.interpolation.choose", c->interpolation->key.c_str())) {
                    for (const auto& [iid, key] : c->interpolations)
                        if (ImGui::Selectable((key + "###layer.map." + std::to_string(iid)).c_str(),
                                              iid == c->interpolation->id))
                            (void)w.show_interpolation(iid);
                    ImGui::EndCombo();
                }
                text_tooltip("The floor has several stored maps (one per metric and cell): choose the one shown.");
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (c != nullptr) {
        ImGui::SeparatorText("Floor");
        ImGui::TextDisabled("%zu measured points", c->points.count());
        if (c->metres_per_unit)
            ImGui::TextDisabled("Scale: %.4g m per plan unit", *c->metres_per_unit);
        else
            ImGui::TextColored(kBannerColour, "Not to scale");
        if (!canvas_->tile_error().empty() && c->has_plan) {
            ImGui::PushTextWrapPos(0.0F);
            ImGui::TextColored(kErrorColour, "%s", canvas_->tile_error().c_str());
            ImGui::PopTextWrapPos();
        }
    }
}

void Gui::draw_status_bar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + 6.0F;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("###status_bar", nullptr, flags)) {
        const std::string& s = state_.status();
        if (state_.status_is_error())
            ImGui::TextColored(kErrorColour, "%s", s.c_str());
        else
            ImGui::TextUnformatted(s.empty() ? "Ready" : s.c_str());
    }
    ImGui::End();
}

void Gui::draw_new_project() {
    NewProjectForm& f = state_.new_project_form();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0F);
    ImGui::TextDisabled("One project file holds one building: its floors, plans, walls, antennas and walk tests.");
    ImGui::PopTextWrapPos();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    input_text("Project name###name", f.name);
    text_tooltip("The building or project name, shown in the window title and in reports (required).");
    input_text("Site###site", f.site);
    text_tooltip("Where the building is: address or site name, for the report.");
    input_text("Operator###operator", f.operator_name);
    text_tooltip("The network operator the survey is made for.");
    std::string notes = f.notes;
    std::string buf = notes;
    buf.resize(std::max<std::size_t>(4096, notes.size() + 1), '\0');
    if (ImGui::InputTextMultiline("Notes###notes", buf.data(), buf.size(),
                                  ImVec2(0.0F, ImGui::GetTextLineHeight() * 4.0F)))
        f.notes = std::string(buf.c_str());
    text_tooltip("Free text for the report, e.g. the purpose of the survey.");
    std::string file = f.file.string();
    if (input_text("File###file", file)) f.file = file;
    text_tooltip("Where the project file is created (.yantra is added when missing).");
    ImGui::SameLine();
    if (ImGui::Button("Browse...###browse")) state_.browse(BrowsePurpose::NewProjectFile);
    text_tooltip("Choose the folder and file name in the file browser.");
    ImGui::Spacing();
    if (dialog_button("Create###dlg.ok", "Create the project file, then add the first floor [Enter]") ||
        ImGui::IsKeyPressed(ImGuiKey_Enter))
        (void)state_.confirm_new_project();
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Close without creating a project [Esc]") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_browser() {
    FileBrowser* b = state_.browser();
    if (b == nullptr) return;
    // Navigation row: parent, home, the folder (typed path fallback).
    ImGui::BeginDisabled(!b->can_go_up());
    if (ImGui::Button("Up###fb.up")) b->go_up();
    ImGui::EndDisabled();
    text_tooltip("Open the parent folder [Backspace]");
    ImGui::SameLine();
    if (ImGui::Button("Home###fb.home")) b->go_home();
    text_tooltip("Open your home folder.");
    ImGui::SameLine();
    if (ImGui::Button("Refresh###fb.refresh")) b->refresh();
    text_tooltip("List the folder again.");
    ImGui::SameLine();
    std::string folder = b->folder().string();
    ImGui::SetNextItemWidth(-1.0F);
    std::string fbuf = folder;
    fbuf.resize(std::max<std::size_t>(1024, folder.size() + 1), '\0');
    if (ImGui::InputText("###fb.folder", fbuf.data(), fbuf.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        (void)b->navigate(std::string(fbuf.c_str()));
    text_tooltip("The current folder. Type a folder and press Enter to open it.");

    const float list_h = ImGui::GetTextLineHeightWithSpacing() * 14.0F;
    ImGui::BeginChild("###fb.places", ImVec2(180.0F, list_h), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Places");
    for (const BrowserPlace& p : b->places()) {
        if (ImGui::Selectable((p.label + "###fb.place." + p.path.string()).c_str(), p.path == b->folder()))
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
        ImGui::TextDisabled("No folders or matching files here.");
    if (ImGui::BeginTable("###fb.table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0F);
        std::optional<std::size_t> activate;
        for (std::size_t i = 0; i < b->entries().size(); ++i) {
            const BrowserEntry& e = b->entries()[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = (e.directory ? "[folder]  " : "") + e.name + "###fb.e." + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), b->selected() == i,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                b->select(i);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || e.directory) activate = i;
            }
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
    if (input_text("File name###fb.name", name)) b->set_file_name(name);
    const bool enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    text_tooltip("A file name in the current folder, or a full path (\"~/\" for your home folder). A folder opens "
                 "it.");
    std::vector<const char*> filters;
    for (const FileFilter& f : b->filters()) filters.push_back(f.label.c_str());
    int active = static_cast<int>(b->active_filter());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
    if (ImGui::Combo("Show###fb.filter", &active, filters.data(), static_cast<int>(filters.size())))
        b->set_filter(static_cast<std::size_t>(active));
    text_tooltip("Which files are listed (folders are always listed).");
    ImGui::SameLine();
    bool hidden = b->show_hidden();
    if (ImGui::Checkbox("Hidden###fb.hidden", &hidden)) b->set_show_hidden(hidden);
    text_tooltip("Also list hidden files and folders (names starting with a dot).");
    ImGui::Spacing();
    const bool save = b->mode() == BrowseMode::Save;
    bool ok = dialog_button(save ? "Save###dlg.ok" : "Open###dlg.ok",
                            save ? "Use this file name [Enter]" : "Open the selected file [Enter]");
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Close the file browser [Esc]") ||
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
        if (!ImGui::IsPopupOpen("Replace file?###dlg.overwrite")) ImGui::OpenPopup("Replace file?###dlg.overwrite");
        if (ImGui::BeginPopupModal("Replace file?###dlg.overwrite", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("%s already exists.", b->pending_overwrite()->filename().string().c_str());
            ImGui::TextDisabled("%s", b->pending_overwrite()->parent_path().string().c_str());
            ImGui::TextUnformatted("Replace it? The old file is lost.");
            if (dialog_button("Replace###ow.yes", "Replace the existing file")) {
                ImGui::CloseCurrentPopup();
                (void)state_.browser_overwrite(true);
            }
            ImGui::SameLine();
            if (dialog_button("Keep it###ow.no", "Do not replace; choose another name [Esc]") ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                (void)state_.browser_overwrite(false);
            }
            ImGui::EndPopup();
        }
    }
}

void Gui::draw_add_floor() {
    AddFloorForm& f = state_.add_floor_form();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    input_text("Name###name", f.name);
    text_tooltip("For example \"Ground floor\" or \"Basement\".");
    ImGui::InputInt("Level###level", &f.level);
    text_tooltip("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level.");
    float height = static_cast<float>(f.height_m);
    if (ImGui::InputFloat("Height (m)###height", &height, 0.1F, 0.5F, "%.2f")) f.height_m = static_cast<double>(height);
    text_tooltip("Floor-to-floor height in metres (more than 0, at most 50), used for the slab between floors.");
    ImGui::BeginDisabled(true);
    ImGui::Button("Choose a plan file...###af.plan_file");
    ImGui::EndDisabled();
    text_tooltip("Choose the plan (PDF or image) of the floor and trim it. Not available in this version yet: the "
                 "floor is added without a plan.");
    ImGui::Spacing();
    if (dialog_button("Add floor###dlg.ok", "Add the floor [Enter]") ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        (void)state_.confirm_add_floor();
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Close without adding a floor [Esc]") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_about() {
    const AboutView a = state_.about();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5F);
    ImGui::Text("%s %s", a.name.c_str(), a.version.c_str());
    ImGui::PopFont();
    ImGui::TextUnformatted("Indoor coverage survey");
    ImGui::Spacing();
    ImGui::Text("Licence: %s", a.licence.c_str());
    if (!a.source_url.empty()) ImGui::Text("Source code: %s", a.source_url.c_str());
    ImGui::Text("Homepage: %s", a.homepage.c_str());
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
    ImGui::TextUnformatted(a.statement.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (dialog_button("Close###dlg.cancel", "Close [Esc]") || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter))
        state_.cancel_dialog();
}

void Gui::draw_floor_properties() {
    FloorForm& f = state_.floor_properties_form();
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    input_text("Name###name", f.name);
    text_tooltip("For example \"Ground floor\" or \"Basement\".");
    ImGui::InputInt("Level###level", &f.level);
    text_tooltip("0 = ground floor, 1 = first floor, -1 = basement. Every floor has its own level; the tabs are "
                 "ordered by level.");
    float height = static_cast<float>(f.height_m);
    if (ImGui::InputFloat("Height (m)###height", &height, 0.1F, 0.5F, "%.2f")) f.height_m = static_cast<double>(height);
    text_tooltip("Floor-to-floor height in metres (more than 0, at most 50), used for the slab between floors.");
    ImGui::Spacing();
    if (dialog_button("Apply###dlg.ok", "Store the changes [Enter]") ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::GetIO().WantTextInput))
        (void)state_.confirm_floor_properties();
    ImGui::SameLine();
    if (dialog_button("Cancel###dlg.cancel", "Close without changes [Esc]") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
    error_line(state_);
}

void Gui::draw_remove_floor() {
    ProjectWindow* w = state_.project_window();
    if (w == nullptr || !w->pending_remove()) {
        state_.cancel_dialog();
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0F);
    ImGui::TextUnformatted(w->pending_remove()->question.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (dialog_button("Remove###dlg.ok", "Remove the floor and everything on it"))
        (void)state_.confirm_remove_floor();
    ImGui::SameLine();
    if (dialog_button("Keep it###dlg.cancel", "Do not remove the floor [Esc]") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        state_.cancel_dialog();
}

void Gui::draw_dialog_body(Dialog d) {
    switch (d) {
    case Dialog::NewProject: draw_new_project(); break;
    case Dialog::Browse: draw_browser(); break;
    case Dialog::AddFloor: draw_add_floor(); break;
    case Dialog::About: draw_about(); break;
    case Dialog::FloorProperties: draw_floor_properties(); break;
    case Dialog::RemoveFloor: draw_remove_floor(); break;
    case Dialog::None: break;
    }
}

void Gui::draw_dialogs() {
    const Dialog d = state_.dialog();
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
    // Dialogs fit small windows too (UI-13).
    const float width = std::min(d == Dialog::Browse ? 820.0F : 600.0F, vp->WorkSize.x * 0.95F);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0F), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        // Closed by ImGui (it should not happen for modals without a close
        // button): give up the dialog.
        state_.cancel_dialog();
        shown_ = Dialog::None;
        return;
    }
    draw_dialog_body(d);
    // A command moved to another dialog (or closed it): close this popup;
    // the next frame opens the next one.
    if (state_.dialog() != d) {
        ImGui::CloseCurrentPopup();
        shown_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void Gui::draw() {
    UserTexture::collect();
    handle_shortcuts();
    draw_menu_bar();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float top = vp->WorkPos.y;
    const float status_h = ImGui::GetFrameHeight() + 6.0F;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, top));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y - status_h - top));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0F, 20.0F));
    if (ImGui::Begin(kMainWindow, nullptr, flags)) {
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
    draw_status_bar();
    draw_dialogs();
}

}  // namespace yantra::app
