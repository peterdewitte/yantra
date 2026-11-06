// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A texture made from RGBA bytes (plan tiles, the interpolation layer) for
// ImGui's draw lists, without OpenGL code here: it is an ImTextureData
// registered with ImGui (RegisterUserTexture), which the renderer backend
// creates, updates and destroys as it does the font atlas
// (ImGuiBackendFlags_RendererHasTextures). So the Gui stays free of GL and
// the headless UI tests (tests/app/harness) serve these textures the same
// way.
//
// Lifetime: reset() asks the backend to destroy the texture; it is freed
// at a later frame's collect() once the backend has done so. Destroy the
// Gui after the renderer backend's shutdown (which destroys every
// texture) and before ImGui::DestroyContext(); release_all() then frees
// what is left. UI thread only, with the ImGui context current.
#pragma once

#include <cstdint>

#include <imgui.h>

namespace yantra::app {

class UserTexture {
public:
    UserTexture() = default;
    ~UserTexture();
    UserTexture(UserTexture&& other) noexcept;
    UserTexture& operator=(UserTexture&& other) noexcept;
    UserTexture(const UserTexture&) = delete;
    UserTexture& operator=(const UserTexture&) = delete;

    /// Takes `width` x `height` RGBA bytes (rows top to bottom) as the
    /// texture's content (a new texture each time).
    void set(int width, int height, const std::uint8_t* rgba);
    void reset();
    bool valid() const noexcept { return tex_ != nullptr; }
    ImTextureRef ref() const noexcept;
    int width() const noexcept;
    int height() const noexcept;

    /// Frees textures the backend has destroyed (call once per frame).
    static void collect();
    /// Frees every retired texture now (shutdown, see above).
    static void release_all();
    /// Live textures (made and not yet freed), for tests.
    static int live_count();

private:
    ImTextureData* tex_ = nullptr;
};

}  // namespace yantra::app
