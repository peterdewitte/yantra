// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ImGui user textures, see user_texture.h.
#include "user_texture.h"

#include <imgui_internal.h>

#include <cstring>
#include <utility>
#include <vector>

namespace yantra::app {

namespace {

std::vector<ImTextureData*>& retired() {
    static std::vector<ImTextureData*> list;
    return list;
}

int& live() {
    static int n = 0;
    return n;
}

void free_texture(ImTextureData* t) {
    if (ImGui::GetCurrentContext() != nullptr) ImGui::UnregisterUserTexture(t);
    IM_DELETE(t);
    --live();
}

}  // namespace

UserTexture::~UserTexture() { reset(); }

UserTexture::UserTexture(UserTexture&& other) noexcept : tex_(std::exchange(other.tex_, nullptr)) {}

UserTexture& UserTexture::operator=(UserTexture&& other) noexcept {
    if (this != &other) {
        reset();
        tex_ = std::exchange(other.tex_, nullptr);
    }
    return *this;
}

void UserTexture::set(int width, int height, const std::uint8_t* rgba) {
    reset();
    if (width <= 0 || height <= 0 || rgba == nullptr || ImGui::GetCurrentContext() == nullptr) return;
    tex_ = IM_NEW(ImTextureData)();
    tex_->Create(ImTextureFormat_RGBA32, width, height);
    std::memcpy(tex_->Pixels, rgba, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    tex_->UsedRect = ImTextureRect{0, 0, static_cast<unsigned short>(width), static_cast<unsigned short>(height)};
    ImGui::RegisterUserTexture(tex_);
    ++live();
}

void UserTexture::reset() {
    if (tex_ == nullptr) return;
    ImTextureData* t = std::exchange(tex_, nullptr);
    // Never made by the backend, or already destroyed by it (a backend
    // that shut down leaves it WantCreate again): free now. Callers never
    // reset a texture drawn in the current frame.
    if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_Destroyed) {
        free_texture(t);
        return;
    }
    // ImGui moves it to WantDestroy at the next frame; the backend then
    // destroys it (Destroyed, kept so because the destroy was planned).
    t->WantDestroyNextFrame = true;
    retired().push_back(t);
}

ImTextureRef UserTexture::ref() const noexcept { return tex_ != nullptr ? tex_->GetTexRef() : ImTextureRef(); }
int UserTexture::width() const noexcept { return tex_ != nullptr ? tex_->Width : 0; }
int UserTexture::height() const noexcept { return tex_ != nullptr ? tex_->Height : 0; }

void UserTexture::collect() {
    auto& list = retired();
    for (auto it = list.begin(); it != list.end();) {
        if ((*it)->Status == ImTextureStatus_Destroyed) {
            free_texture(*it);
            it = list.erase(it);
        } else {
            ++it;
        }
    }
}

void UserTexture::release_all() {
    for (ImTextureData* t : retired()) free_texture(t);
    retired().clear();
}

int UserTexture::live_count() { return live(); }

}  // namespace yantra::app
