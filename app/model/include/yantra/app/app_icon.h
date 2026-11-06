// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application and window icon (BRD-06): the Yantra logo PNGs of
// data/logo/png (installed to share/yantra/logo/png), decoded through the
// core's stb_image plan reader (as BRD-03), so the app links no image
// library of its own. app/src/main.cpp hands them to glfwSetWindowIcon.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "yantra/core/plan/source.h"

namespace yantra::app {

/// The sizes given to the window system (it picks the best fit).
inline constexpr int kAppIconSizes[] = {16, 24, 32, 48, 64, 128, 256};

/// "logo/png/yantra_logo_<size>.png", relative to a data folder.
std::filesystem::path app_icon_relative(int size);

/// The icon images in kAppIconSizes order, RGBA, from the first data
/// folder that has each file. A missing or unreadable file (or one with
/// another pixel size) is left out and named in `error`.
std::vector<plan::Raster> load_app_icons(const std::vector<std::filesystem::path>& data_folders, std::string& error);

}  // namespace yantra::app
