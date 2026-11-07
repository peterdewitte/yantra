// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The application icon (BRD-06), see app_icon.h.
#include "yantra/app/app_icon.h"

#include <stb_image.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "yantra/app/data_paths.h"

namespace yantra::app {

namespace fs = std::filesystem;

fs::path app_icon_relative(int size) {
    return fs::path("logo") / "png" / ("yantra_logo_" + std::to_string(size) + ".png");
}

fs::path curtat_logo_relative(bool dark, int size) {
    return fs::path("branding") / ("curtat_logo_" + std::string(dark ? "dark" : "light") + "_" + std::to_string(size) + ".png");
}

namespace {

/// The PNG as 8-bit RGBA with its transparency kept (logos on any
/// theme; the plan reader composites on white paper instead).
std::optional<plan::Raster> read_png(const fs::path& file, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (bytes.empty() || bytes.size() > (64U << 20)) {
        error = file.string() + ": empty or too large";
        return std::nullopt;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 4);
    if (px == nullptr || w <= 0 || h <= 0 || static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) > (16U << 20)) {
        const char* why = stbi_failure_reason();
        error = file.string() + ": cannot be decoded (" + std::string(why != nullptr ? why : "too large") + ")";
        if (px != nullptr) stbi_image_free(px);
        return std::nullopt;
    }
    plan::Raster r(w, h);
    std::memcpy(r.rgba.data(), px, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    stbi_image_free(px);
    return r;
}

}  // namespace

std::optional<plan::Raster> load_data_png(const std::vector<fs::path>& data_folders, const fs::path& relative,
                                          std::string& error) {
    const auto file = find_data_file(data_folders, relative);
    if (!file) {
        error = relative.string() + " not found";
        return std::nullopt;
    }
    return read_png(*file, error);
}

std::vector<plan::Raster> load_app_icons(const std::vector<fs::path>& data_folders, std::string& error) {
    std::vector<plan::Raster> out;
    auto note = [&](const std::string& m) { error += (error.empty() ? "" : "; ") + m; };
    for (const int size : kAppIconSizes) {
        std::string e;
        auto r = load_data_png(data_folders, app_icon_relative(size), e);
        if (!r) {
            note(e);
            continue;
        }
        if (r->width != size || r->height != size) {
            note(app_icon_relative(size).string() + " is not " + std::to_string(size) + " x " + std::to_string(size) + " px");
            continue;
        }
        out.push_back(std::move(*r));
    }
    return out;
}

}  // namespace yantra::app
