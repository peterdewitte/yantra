// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: photos as image-file bytes (photo.h), through the plan image
// reader (stb_image, EXIF orientation).

#include "yantra/core/about/photo.h"

#include <algorithm>
#include <climits>

#include "stb_image.h"

namespace yantra::about {

bool is_photo(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT_MAX)) return false;
    int w = 0, h = 0, comp = 0;
    return stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp) != 0 && w > 0 && h > 0;
}

std::optional<plan::Raster> decode_photo(std::span<const std::uint8_t> bytes, int max_px, std::string* error) {
    if (!is_photo(bytes)) {
        if (error != nullptr) *error = "not an image Yantra reads (JPEG, PNG, BMP, GIF, TGA, PSD, HDR, PNM)";
        return std::nullopt;
    }
    try {
        const auto source = plan::open_plan_source(bytes, "photo");
        const plan::PageSize page = source->page_size(0);
        const int longest = static_cast<int>(std::max(page.width, page.height));
        return plan::render_thumbnail(*source, 0, std::clamp(longest, 1, std::max(1, max_px)));
    } catch (const std::exception& e) {
        if (error != nullptr) *error = e.what();
        return std::nullopt;
    }
}

}  // namespace yantra::about
