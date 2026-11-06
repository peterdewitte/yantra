// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless UI test harness, see ui_harness.h.
#include "ui_harness.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <exception>

namespace yantra::test {

namespace {

std::string_view key_of(std::string_view label) {
    const auto p = label.find("###");
    return p == std::string_view::npos ? label : label.substr(p + 3);
}

// The key of the top-level window of an item: a child window's name is
// "<parent name>/<child name>_<hash>" (ImGui), so the part before the
// first '/' names the window or popup the item is in.
std::string_view top_window_key(std::string_view window) {
    const auto p = window.find('/');
    return key_of(p == std::string_view::npos ? window : window.substr(0, p));
}

}  // namespace

UiHarness::UiHarness() : UiHarness(Options{}) {}

UiHarness::UiHarness(Options options) : options_(options) {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(options_.width, options_.height);
    io.DeltaTime = options_.frame_s;
    // A "renderer" that accepts every texture request (serve_textures).
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendRendererName = "yantra-test-headless";
    io.BackendPlatformName = "yantra-test-headless";
    probe_ = std::make_unique<app::UiProbe>(ctx_);
    if (previous != nullptr) ImGui::SetCurrentContext(previous);
}

UiHarness::~UiHarness() {
    probe_.reset();
    ImGui::DestroyContext(ctx_);
}

void UiHarness::set_draw(std::function<void()> draw) { draw_ = std::move(draw); }

void UiHarness::set_display_size(float width, float height) {
    options_.width = width;
    options_.height = height;
}

void UiHarness::serve_textures() {
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd == nullptr || dd->Textures == nullptr) return;
    for (ImTextureData* t : *dd->Textures) {
        switch (t->Status) {
        case ImTextureStatus_WantCreate:
            t->SetTexID(static_cast<ImTextureID>(next_texture_++));
            t->SetStatus(ImTextureStatus_OK);
            break;
        case ImTextureStatus_WantUpdates: t->SetStatus(ImTextureStatus_OK); break;
        case ImTextureStatus_WantDestroy:
            if (t->UnusedFrames > 0) {
                t->SetTexID(ImTextureID_Invalid);
                t->SetStatus(ImTextureStatus_Destroyed);
            }
            break;
        default: break;
        }
    }
}

bool UiHarness::frame() {
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(options_.width, options_.height);
    io.DeltaTime = options_.frame_s;
    probe_->begin_frame();
    ImGui::NewFrame();
    ImGuiErrorRecoveryState recovery;
    ImGui::ErrorRecoveryStoreState(&recovery);
    bool ok = true;
    try {
        if (draw_) draw_();
    } catch (const std::exception& e) {
        error_ = std::string("the draw code threw: ") + e.what();
        ok = false;
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
    }
    ImGui::Render();
    serve_textures();
    probe_->end_frame();
    windows_.clear();
    for (const ImGuiWindow* w : ctx_->Windows)
        if (w->Active) windows_.emplace_back(w->Name);
    return ok;
}

bool UiHarness::frames(int n) {
    bool ok = true;
    for (int i = 0; i < n; ++i) ok = frame() && ok;
    return ok;
}

bool UiHarness::run_until(const std::function<bool()>& done, int max_frames) {
    for (int i = 0; i < max_frames; ++i) {
        if (!frame()) return false;
        if (done()) return true;
    }
    return false;
}

const std::vector<app::ProbeItem>& UiHarness::items() const { return probe_->items(); }

std::optional<app::ProbeItem> UiHarness::find(std::string_view key, std::string_view window_key) const {
    const auto& all = probe_->items();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (it->key() != key) continue;
        if (!window_key.empty() && top_window_key(it->window) != window_key) continue;
        return *it;
    }
    return std::nullopt;
}

std::string UiHarness::dump() const {
    std::string out;
    for (const app::ProbeItem& it : probe_->items()) {
        if (it.label.empty()) continue;
        out += std::string(it.key()) + (it.disabled ? " (disabled)" : "") + " in " + it.window + "\n";
    }
    out += "windows:";
    for (const std::string& w : windows_) out += " [" + w + "]";
    return out;
}

bool UiHarness::window_open(std::string_view key) const {
    for (const std::string& w : windows_)
        if (key_of(w) == key) return true;
    return false;
}

bool UiHarness::wait_window(std::string_view key) {
    // Open, and its contents drawn: an auto-sized window lays out its
    // first frame hidden, so its items come a frame or two later.
    const auto ready = [&] {
        if (!window_open(key)) return false;
        for (const app::ProbeItem& it : probe_->items())
            if (!it.label.empty() && it.label != it.window && top_window_key(it.window) == key)
                return true;
        return false;
    };
    if (!ready() && !run_until(ready, 30)) return false;
    // Two more frames: the window has its size, every item is in place.
    return frames(2) && window_open(key);
}

bool UiHarness::wait_window_closed(std::string_view key) {
    if (!window_open(key)) return true;
    return run_until([&] { return !window_open(key); }, 30);
}

void UiHarness::click_at(float x, float y) {
    mouse_move(x, y);
    frame();
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
}

void UiHarness::drag(float x0, float y0, float x1, float y1, int button, int steps) {
    mouse_move(x0, y0);
    frame();
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseButtonEvent(button, true);
    frame();
    steps = steps < 1 ? 1 : steps;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        mouse_move(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
        frame();
    }
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseButtonEvent(button, false);
    frame();
}

void UiHarness::wheel(float x, float y, float delta) {
    mouse_move(x, y);
    frame();
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseWheelEvent(0.0F, delta);
    frame();
}

void UiHarness::mouse_move(float x, float y) {
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(x, y);
}

bool UiHarness::hover(std::string_view key, std::string_view window_key, int n) {
    const auto it = find(key, window_key);
    if (!it) return false;
    mouse_move(it->cx(), it->cy());
    return frames(n);
}

bool UiHarness::click(std::string_view key, std::string_view window_key) {
    auto it = find(key, window_key);
    if (!it || it->disabled) return false;
    mouse_move(it->cx(), it->cy());
    frame();
    // The item may have moved (a hover highlight, a layout change).
    it = find(key, window_key);
    if (!it || it->disabled) return false;
    mouse_move(it->cx(), it->cy());
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    return frame();
}

bool UiHarness::click_menu(std::string_view menu, std::string_view item) {
    const std::string menu_key = "menu." + std::string(menu);
    if (!has(item) && !click(menu_key)) return false;
    if (!has(item) && !run_until([&] { return has(item); }, 10)) return false;
    return click(item);
}

void UiHarness::key(int imgui_key, int mods) {
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    const int mod_keys[] = {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super};
    for (int m : mod_keys)
        if ((mods & m) != 0) io.AddKeyEvent(static_cast<ImGuiKey>(m), true);
    io.AddKeyEvent(static_cast<ImGuiKey>(imgui_key), true);
    frame();
    io.AddKeyEvent(static_cast<ImGuiKey>(imgui_key), false);
    for (int m : mod_keys)
        if ((mods & m) != 0) io.AddKeyEvent(static_cast<ImGuiKey>(m), false);
    frame();
}

void UiHarness::type(std::string_view text) {
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddInputCharactersUTF8(std::string(text).c_str());
    frames(2);
}

bool UiHarness::fill(std::string_view key_name, std::string_view text, std::string_view window_key) {
    if (!click(key_name, window_key)) return false;
    key(ImGuiKey_A, ImGuiMod_Ctrl);
    key(ImGuiKey_Delete);
    type(text);
    return true;
}

}  // namespace yantra::test
