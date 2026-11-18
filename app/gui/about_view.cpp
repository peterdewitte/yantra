// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the "About building" tab of the project window (the first tab,
// fixed: it cannot be removed or moved) and the camera window. The form
// writes each field when it is left; the map shows OpenStreetMap tiles
// (fetched only while shown, osm_map.h) with the attribution, a marker at
// the stored position, drag to pan, wheel or +/- to zoom, and a right-click
// menu to move the marker; the photo opens from a file or is taken with a
// camera (camera.h).

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gui.h"
#include "gui_widgets.h"
#include "yantra/app/i18n.h"
#include "yantra/core/about/photo.h"

namespace fs = std::filesystem;

namespace yantra::app {

using namespace gui_detail;

namespace {

bool multiline(const char* label, std::string& s, float height, std::size_t capacity = 16384) {
    std::string buf = s;
    buf.resize(std::max(capacity, s.size() + 1), '\0');
    const bool changed = ImGui::InputTextMultiline(label, buf.data(), buf.size(), ImVec2(-1.0F, height));
    if (changed) s = std::string(buf.c_str());
    return changed;
}

std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The marker: a red pin with its point at (x, y).
void draw_pin(ImDrawList* dl, ImVec2 at, float size) {
    const float r = size * 0.36F;
    const ImVec2 c(at.x, at.y - size + r);
    dl->AddCircleFilled(c, r + 1.5F, IM_COL32(255, 255, 255, 255));
    dl->AddTriangleFilled(ImVec2(c.x - r * 0.75F, c.y + r * 0.55F), ImVec2(c.x + r * 0.75F, c.y + r * 0.55F), at,
                          IM_COL32(214, 39, 40, 255));
    dl->AddCircleFilled(c, r, IM_COL32(214, 39, 40, 255));
    dl->AddCircleFilled(c, r * 0.4F, IM_COL32(255, 255, 255, 255));
}

}  // namespace

void Gui::draw_about_page() {
    AboutBuildingView* a = state_.about_building();
    if (a == nullptr) return;
    building_.shown = true;
    if (a->poll()) about_message_.clear();
    const auto saved = [&](const Result& r) {
        about_message_ = r.ok ? std::string() : r.message;
    };
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y = std::max(avail.y, em(320.0F));
    const bool wide = avail.x >= em(720.0F);
    const float form_w = wide ? std::clamp(avail.x * 0.42F, em(300.0F), em(520.0F)) : avail.x;
    ImGui::BeginChild("###about.form", ImVec2(form_w, wide ? avail.y : 0.0F),
                      wide ? ImGuiChildFlags_Borders : (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY));
    wrap_texts();
    help_button("13.6");
    ImGui::TextDisabled("%s", tr("About the building: where it is, what it looks like, who surveys it and when. The report "
                                 "starts with it."));
    AboutForm& f = a->form();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    ImGui::TextUnformatted(tr("Address"));
    multiline("###about.address", f.address, line * 3.0F + ImGui::GetStyle().FramePadding.y * 2.0F, 1024);
    if (ImGui::IsItemDeactivatedAfterEdit()) saved(a->save_form());
    text_tooltip(tr("The building's address as you write it (street, number, postcode, town, country)."));
    {
        const MapSearchView s = a->search();
        ImGui::BeginDisabled(!s.can_search);
        if (ImGui::Button(tr("Find on map###about.find"), ImVec2(0.0F, target_h()))) saved(a->find_on_map());
        ImGui::EndDisabled();
        text_tooltip(tr("Look the address up on OpenStreetMap (Nominatim) and show it on the map. Only the address is "
                        "sent, and only when you press this button."));
        if (!s.text.empty()) {
            same_line_if(ImGui::CalcTextSize(s.text.c_str()).x);
            if (s.failed)
                ImGui::TextColored(kErrorColour, "%s", s.text.c_str());
            else
                ImGui::TextDisabled("%s", s.text.c_str());
        }
    }
    ImGui::Spacing();
    ImGui::TextUnformatted(tr("Surveyor"));
    ImGui::SetNextItemWidth(-1.0F);
    input_text("###about.surveyor", f.surveyor, 256);
    if (ImGui::IsItemDeactivatedAfterEdit()) saved(a->save_form());
    text_tooltip(tr("Who makes the survey (printed in the report)."));
    ImGui::TextUnformatted(tr("Survey date"));
    {
        const char* today = tr("Today###about.today");
        ImGui::SetNextItemWidth(std::max(em(110.0F), ImGui::GetContentRegionAvail().x - label_w(today)));
        input_text_hint("###about.date", "YYYY-MM-DD", f.survey_date, 32);
        if (ImGui::IsItemDeactivatedAfterEdit()) saved(a->save_form());
        text_tooltip(tr("The day of the survey, as YYYY-MM-DD. A new project starts with the day it was made."));
        ImGui::SameLine();
        if (ImGui::Button(today)) saved(a->set_date_today());
        text_tooltip(tr("Set the survey date to today."));
    }
    ImGui::TextUnformatted(tr("Notes"));
    multiline("###about.notes", f.notes, line * 6.0F + ImGui::GetStyle().FramePadding.y * 2.0F);
    if (ImGui::IsItemDeactivatedAfterEdit()) saved(a->save_form());
    text_tooltip(tr("Anything about the building or the survey: access, contacts on site, special rooms."));
    if (!about_message_.empty()) ImGui::TextColored(kErrorColour, "%s", about_message_.c_str());

    // The photo.
    ImGui::Separator();
    ImGui::TextUnformatted(tr("Photo of the building"));
    const auto target = [a](const Photo& p) { return a->set_photo(p); };
    if (ImGui::Button(tr("Open photo...###about.photo_open"))) state_.browse_photo(target);
    text_tooltip(tr("Choose a picture file (JPEG, PNG, BMP, GIF, ...). It is copied into the project."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Take photo...###about.photo_take"))) (void)state_.take_photo(target);
    text_tooltip(tr("Take the photo with a camera of this computer (a webcam or a connected camera)."));
    const project::AboutBuilding& st = a->stored();
    if (!st.photo.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Remove photo###about.photo_remove"))) saved(a->remove_photo());
        text_tooltip(tr("Remove the photo from the project."));
    }
    if (photo_generation_ != a->photo_generation()) {
        photo_generation_ = a->photo_generation();
        photo_tex_.reset();
        if (!st.photo.empty()) {
            if (const auto r = about::decode_photo(st.photo, 1024)) photo_tex_.set(r->width, r->height, r->rgba.data());
        }
    }
    if (photo_tex_.valid()) {
        const float w = ImGui::GetContentRegionAvail().x;
        const float scale = std::min(w / static_cast<float>(photo_tex_.width()), em(260.0F) / static_cast<float>(photo_tex_.height()));
        ImGui::Image(photo_tex_.ref(), ImVec2(static_cast<float>(photo_tex_.width()) * scale,
                                              static_cast<float>(photo_tex_.height()) * scale));
        text_tooltip(st.photo_source == "camera" ? tr("Taken with a camera.") : st.photo_name.c_str());
        building_.photo = true;
    } else {
        ImGui::TextDisabled("%s", tr("No photo yet."));
    }
    ImGui::EndChild();
    if (wide) ImGui::SameLine();
    draw_about_map(*a, ImVec2(wide ? 0.0F : avail.x, wide ? avail.y : std::max(em(300.0F), avail.x * 0.6F)));
}

void Gui::draw_about_map(AboutBuildingView& a, ImVec2 size) {
    ImGui::BeginChild("###about.map", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const project::AboutBuilding& st = a.stored();
    // The view's buttons.
    if (ImGui::Button("+###about.zoom_in")) (void)a.zoom(+1);
    text_tooltip(tr("Zoom in [mouse wheel]"));
    ImGui::SameLine();
    if (ImGui::Button("-###about.zoom_out")) (void)a.zoom(-1);
    text_tooltip(tr("Zoom out [mouse wheel]"));
    ImGui::SameLine();
    ImGui::BeginDisabled(!st.has_position());
    if (ImGui::Button(tr("Centre###about.centre"))) a.centre_on_position();
    ImGui::EndDisabled();
    text_tooltip(tr("Show the building's position in the middle of the map."));
    {
        const char* hint = tr("Drag to pan, wheel to zoom, right click to move the marker");
        same_line_if(ImGui::CalcTextSize(hint).x);
        ImGui::TextDisabled("%s", hint);
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area(std::max(1.0F, ImGui::GetContentRegionAvail().x), std::max(em(200.0F), ImGui::GetContentRegionAvail().y));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y), IM_COL32(226, 226, 226, 255));
    ImGui::InvisibleButton("###about.map_area", area, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0F)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        a.pan(d.x, d.y);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0F) (void)a.zoom(ImGui::GetIO().MouseWheel > 0.0F ? +1 : -1);
    const int z = a.view_zoom();
    const about::TilePoint centre = about::to_tile(a.view_centre(), z);
    // The map position under the mouse (right-click menu).
    const auto at_screen = [&](ImVec2 p) {
        about::TilePoint t{centre.x + (p.x - origin.x - area.x * 0.5F) / about::kTileSize,
                           centre.y + (p.y - origin.y - area.y * 0.5F) / about::kTileSize};
        return about::from_tile(t, z);
    };
    if (ImGui::BeginPopupContextItem("###about.map_menu")) {
        if (ImGui::IsWindowAppearing()) menu_position_ = at_screen(ImGui::GetMousePosOnOpeningCurrentPopup());
        if (ImGui::MenuItem(tr("Move the marker here###about.marker_here"))) about_message_ = a.set_position(menu_position_).message;
        text_tooltip(tr("Put the building's position where you clicked (when the address search found a place nearby)."));
        if (ImGui::MenuItem(tr("Remove the marker###about.marker_remove"), nullptr, false, st.has_position()))
            about_message_ = a.clear_position().message;
        ImGui::EndPopup();
    }
    dl->PushClipRect(origin, ImVec2(origin.x + area.x, origin.y + area.y), true);
    building_.map = false;
    building_.tiles_drawn = 0;
    building_.tiles_missing = 0;
    building_.tiles_loading = 0;
    if (!st.has_position()) {
        // No position: no tiles (nothing is fetched); say how to get one.
        const char* t = tr("No position yet: write the address and press Find on map (or right click on the map after zooming in).");
        ImGui::SetCursorScreenPos(ImVec2(origin.x + em(12.0F), origin.y + em(12.0F)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + area.x - em(24.0F));  // window-local x
        ImGui::TextDisabled("%s", t);
        ImGui::PopTextWrapPos();
        building_.placeholder = true;
    } else {
        if (tile_generation_ != a.tiles().generation() || tile_zoom_ != z) {
            // New tiles arrived or the zoom changed: rebuild the textures on demand.
            if (tile_zoom_ != z || tile_textures_.size() > 96) tile_textures_.clear();
            tile_generation_ = a.tiles().generation();
            tile_zoom_ = z;
        }
        for (const about::ViewTile& v : about::view_tiles(centre, z, area.x, area.y)) {
            const ImVec2 p0(origin.x + static_cast<float>(v.left), origin.y + static_cast<float>(v.top));
            const ImVec2 p1(p0.x + about::kTileSize, p0.y + about::kTileSize);
            fs::path file;
            const MapTiles::State s = a.tiles().tile(v.id, &file);
            if (s == MapTiles::State::Ready) {
                auto it = tile_textures_.find(v.id);
                if (it == tile_textures_.end()) {
                    UserTexture tex;
                    if (const auto r = about::decode_photo(read_file(file), about::kTileSize))
                        tex.set(r->width, r->height, r->rgba.data());
                    it = tile_textures_.emplace(v.id, std::move(tex)).first;
                }
                if (it->second.valid()) {
                    dl->AddImage(it->second.ref(), p0, p1);
                    ++building_.tiles_drawn;
                    continue;
                }
            }
            if (s == MapTiles::State::Loading)
                ++building_.tiles_loading;
            else
                ++building_.tiles_missing;
        }
        building_.map = building_.tiles_drawn > 0;
        building_.placeholder = building_.tiles_drawn == 0;
        if (building_.tiles_drawn == 0) {
            const char* t = building_.tiles_loading > 0 ? tr("Loading the map...")
                                                        : tr("No map: these map tiles are not cached and Yantra is offline (or the "
                                                             "tile server did not answer).");
            ImGui::SetCursorScreenPos(ImVec2(origin.x + em(12.0F), origin.y + em(12.0F)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + area.x - em(24.0F));  // window-local x
            ImGui::TextDisabled("%s", t);
            ImGui::PopTextWrapPos();
        }
        // The marker at the stored position.
        const about::TilePoint m = about::to_tile(about::LatLon{*st.latitude, *st.longitude}, z);
        draw_pin(dl,
                 ImVec2(origin.x + area.x * 0.5F + static_cast<float>((m.x - centre.x) * about::kTileSize),
                        origin.y + area.y * 0.5F + static_cast<float>((m.y - centre.y) * about::kTileSize)),
                 em(28.0F));
    }
    // The attribution, always visible (OpenStreetMap licence).
    {
        const char* text = about::kAttribution;
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float pad = em(4.0F);
        const ImVec2 b1(origin.x + area.x, origin.y + area.y);
        const ImVec2 b0(b1.x - ts.x - 2.0F * pad, b1.y - ts.y - 2.0F * pad);
        dl->AddRectFilled(b0, b1, IM_COL32(255, 255, 255, 220));
        dl->AddText(ImVec2(b0.x + pad, b0.y + pad), IM_COL32(40, 40, 40, 255), text);
        building_.attribution = true;
        if (ImGui::IsMouseHoveringRect(b0, b1) && hovered) {
            ImGui::SetTooltip("%s", trf("Map data and tiles: {} (ODbL). {}", {about::kAttribution, about::kCopyrightUrl}).c_str());
        }
    }
    dl->PopClipRect();
    if (!a.online()) {
        // Offline: say it under the buttons' line, once.
        building_.offline = true;
    }
    ImGui::EndChild();
}

void Gui::draw_camera_dialog() {
    CameraCapture* cam = state_.camera();
    camera_.shown = true;
    const std::vector<CameraInfo>& cams = state_.cameras();
    camera_.cameras = cams.size();
    if (cams.empty() || cam == nullptr) {
        ImGui::TextColored(kErrorColour, "%s",
                           tr("No camera found. Connect a camera, or use Open photo... with a picture file."));
    } else {
        std::string current = state_.selected_camera();
        std::string label = current;
        for (const CameraInfo& c : cams)
            if (c.id == current) label = c.name;
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("###camera.list", label.c_str())) {
            for (const CameraInfo& c : cams)
                if (ImGui::Selectable((c.name + "###camera." + c.id).c_str(), c.id == current)) state_.select_camera(c.id);
            ImGui::EndCombo();
        }
        text_tooltip(tr("The camera to take the photo with."));
        CameraImage frame;
        const std::uint64_t n = cam->frame(nullptr);
        if (n != camera_frame_ && cam->frame(&frame) > 0 && !frame.empty()) {
            camera_frame_ = n;
            camera_tex_.set(frame.width, frame.height, frame.rgba.data());
        }
        if (cam->state() == CameraCapture::State::Failed) {
            ImGui::TextColored(kErrorColour, "%s", trf("The camera does not work: {}", {cam->error()}).c_str());
        } else if (n == 0) {
            ImGui::TextDisabled("%s", tr("Starting the camera..."));
        } else if (camera_tex_.valid()) {
            const float w = ImGui::GetContentRegionAvail().x;
            // At most 360 em high and under half the window (the buttons stay on screen).
            const float h = std::min(em(360.0F), ImGui::GetMainViewport()->Size.y * 0.45F);
            const float scale = std::min(w / static_cast<float>(camera_tex_.width()), h / static_cast<float>(camera_tex_.height()));
            ImGui::Image(camera_tex_.ref(), ImVec2(static_cast<float>(camera_tex_.width()) * scale,
                                                   static_cast<float>(camera_tex_.height()) * scale));
            ImGui::TextDisabled("%s", cam->format().c_str());
            camera_.preview = true;
        }
    }
    ImGui::Separator();
    const bool ready = cam != nullptr && cam->frame(nullptr) > 0;
    ImGui::BeginDisabled(!ready);
    if (dialog_button(tr("Take###camera.take"), tr("Keep the picture shown now as the photo."))) (void)state_.camera_take();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (dialog_button(tr("Look again###camera.refresh"), tr("List the cameras again (after connecting one)."))) state_.refresh_cameras();
    ImGui::SameLine();
    if (dialog_button(tr("Cancel###camera.cancel"), tr("Close without a photo."))) {
        state_.camera_cancel();
        camera_tex_.reset();
        camera_frame_ = 0;
    }
    if (state_.dialog() != Dialog::Camera) {
        camera_tex_.reset();
        camera_frame_ = 0;
    }
}

}  // namespace yantra::app
