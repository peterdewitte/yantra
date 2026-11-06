// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application icon (BRD-06), see app_icon.h.
#include "yantra/app/app_icon.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "yantra/app/data_paths.h"

namespace yantra::app {

namespace fs = std::filesystem;

fs::path app_icon_relative(int size) {
    return fs::path("logo") / "png" / ("yantra_logo_" + std::to_string(size) + ".png");
}

std::vector<plan::Raster> load_app_icons(const std::vector<fs::path>& data_folders, std::string& error) {
    std::vector<plan::Raster> out;
    auto note = [&](const std::string& m) { error += (error.empty() ? "" : "; ") + m; };
    for (const int size : kAppIconSizes) {
        const fs::path rel = app_icon_relative(size);
        const auto file = find_data_file(data_folders, rel);
        if (!file) {
            note(rel.string() + " not found");
            continue;
        }
        std::ifstream in(*file, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        try {
            const auto source = plan::open_plan_source(bytes, file->filename().string());
            const plan::PageSize page = source->page_size(0);
            if (static_cast<int>(page.width) != size || static_cast<int>(page.height) != size) {
                note(file->string() + " is not " + std::to_string(size) + " x " + std::to_string(size) + " px");
                continue;
            }
            out.push_back(source->render(0, 1.0, plan::PixelRect{0, 0, size, size}));
        } catch (const std::exception& e) {
            note(file->string() + ": " + e.what());
        }
    }
    return out;
}

}  // namespace yantra::app
