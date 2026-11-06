// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The item probe and Dear ImGui's test-engine hooks, see ui_probe.h.
#include "ui_probe.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace yantra::app {

std::string_view ProbeItem::key() const {
    const std::string_view l = label;
    const auto p = l.find("###");
    return p == std::string_view::npos ? l : l.substr(p + 3);
}

UiProbe::UiProbe(ImGuiContext* ctx) : ctx_(ctx) {
    ctx_->TestEngine = this;
    ctx_->TestEngineHookItems = true;
}

UiProbe::~UiProbe() {
    if (ctx_->TestEngine == this) {
        ctx_->TestEngine = nullptr;
        ctx_->TestEngineHookItems = false;
    }
}

void UiProbe::begin_frame() { current_.clear(); }

void UiProbe::end_frame() { done_.swap(current_); }

void UiProbe::add(std::uint32_t id, float x0, float y0, float x1, float y1, bool disabled, const char* window) {
    ProbeItem it;
    it.id = id;
    it.x0 = x0;
    it.y0 = y0;
    it.x1 = x1;
    it.y1 = y1;
    it.disabled = disabled;
    if (window != nullptr) it.window = window;
    current_.push_back(std::move(it));
}

void UiProbe::info(std::uint32_t id, const char* label, std::uint32_t status) {
    for (auto it = current_.rbegin(); it != current_.rend(); ++it) {
        if (it->id != id) continue;
        if (label != nullptr) it->label = label;
        it->status = status;
        return;
    }
    ProbeItem it;
    it.id = id;
    if (label != nullptr) it.label = label;
    it.status = status;
    current_.push_back(std::move(it));
}

}  // namespace yantra::app

// The hooks Dear ImGui calls with IMGUI_ENABLE_TEST_ENGINE (imgui_internal.h).
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data) {
    auto* probe = static_cast<yantra::app::UiProbe*>(ctx->TestEngine);
    if (probe == nullptr || id == 0) return;
    if (item_data == nullptr) {
        // A window (Begin): recorded with its name as the label.
        const ImGuiWindow* w = ImGui::FindWindowByID(id);
        probe->add(id, bb.Min.x, bb.Min.y, bb.Max.x, bb.Max.y, false, w != nullptr ? w->Name : nullptr);
        if (w != nullptr) probe->info(id, w->Name, 0);
        return;
    }
    const ImRect& r = item_data->Rect;
    const ImGuiWindow* w = ctx->CurrentWindow;
    probe->add(id, r.Min.x, r.Min.y, r.Max.x, r.Max.y, (item_data->ItemFlags & ImGuiItemFlags_Disabled) != 0,
               w != nullptr ? w->Name : nullptr);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    auto* probe = static_cast<yantra::app::UiProbe*>(ctx->TestEngine);
    if (probe == nullptr || id == 0) return;
    probe->info(id, label, static_cast<std::uint32_t>(flags));
}

void ImGuiTestEngineHook_Log(ImGuiContext* /*ctx*/, const char* /*fmt*/, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* /*ctx*/, ImGuiID /*id*/) { return nullptr; }
