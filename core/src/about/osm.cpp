// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: OpenStreetMap tile arithmetic, Nominatim URL and answer, tile
// cache layout and the map picture from cached tiles (osm.h).

#include "yantra/core/about/osm.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <numbers>

#include <nlohmann/json.hpp>

#include "stb_image.h"

namespace fs = std::filesystem;

namespace yantra::about {

namespace {

double world(int zoom) { return std::ldexp(1.0, std::clamp(zoom, 0, 30)); }

bool parse_degrees(const nlohmann::json& v, double& out) {
    try {
        if (v.is_string()) {
            std::size_t used = 0;
            const std::string s = v.get<std::string>();
            out = std::stod(s, &used);
            return used == s.size() && std::isfinite(out);
        }
        if (v.is_number()) {
            out = v.get<double>();
            return std::isfinite(out);
        }
    } catch (const std::exception&) {
    }
    return false;
}

}  // namespace

LatLon normalised(LatLon p) {
    p.lat = std::clamp(p.lat, -kMaxLatitude, kMaxLatitude);
    p.lon = std::fmod(p.lon + 180.0, 360.0);
    if (p.lon < 0.0) p.lon += 360.0;
    p.lon -= 180.0;
    return p;
}

TilePoint to_tile(LatLon p, int zoom) {
    p = normalised(p);
    const double n = world(zoom);
    const double lat = p.lat * std::numbers::pi / 180.0;
    TilePoint t;
    t.x = (p.lon + 180.0) / 360.0 * n;
    t.y = (1.0 - std::asinh(std::tan(lat)) / std::numbers::pi) / 2.0 * n;
    return t;
}

LatLon from_tile(TilePoint t, int zoom) {
    const double n = world(zoom);
    LatLon p;
    p.lon = t.x / n * 360.0 - 180.0;
    p.lat = std::atan(std::sinh(std::numbers::pi * (1.0 - 2.0 * t.y / n))) * 180.0 / std::numbers::pi;
    return normalised(p);
}

std::string url_encode(std::string_view text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string nominatim_search_url(std::string_view address, std::string_view base) {
    // A line break typed in the address field is a comma for the search.
    std::string q;
    for (const char c : address) q += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    return std::string(base) + "/search?format=jsonv2&limit=1&q=" + url_encode(q);
}

std::optional<GeocodeResult> parse_nominatim(std::string_view text, std::string* error) {
    const auto fail = [&](const std::string& why) -> std::optional<GeocodeResult> {
        if (error != nullptr) *error = why;
        return std::nullopt;
    };
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return fail("the answer is not JSON");
    if (!j.is_array()) return fail("the answer is not a list of places");
    if (j.empty()) return fail("no place found");
    const nlohmann::json& first = j.front();
    if (!first.is_object() || !first.contains("lat") || !first.contains("lon"))
        return fail("the answer has no position");
    GeocodeResult r;
    if (!parse_degrees(first["lat"], r.position.lat) || !parse_degrees(first["lon"], r.position.lon) ||
        std::abs(r.position.lat) > 90.0 || std::abs(r.position.lon) > 180.0)
        return fail("the answer has no valid position");
    r.position = normalised(r.position);
    if (first.contains("display_name") && first["display_name"].is_string())
        r.display_name = first["display_name"].get<std::string>();
    if (error != nullptr) error->clear();
    return r;
}

std::string tile_url(TileId t, std::string_view base) {
    return std::string(base) + "/" + std::to_string(t.z) + "/" + std::to_string(t.x) + "/" + std::to_string(t.y) + ".png";
}

fs::path tile_file(const fs::path& cache, TileId t) {
    return cache / std::to_string(t.z) / std::to_string(t.x) / (std::to_string(t.y) + ".png");
}

std::vector<ViewTile> view_tiles(TilePoint centre, int zoom, double w, double h) {
    std::vector<ViewTile> out;
    const int n = static_cast<int>(world(zoom));
    const double left = centre.x * kTileSize - w / 2.0;  // world pixels at the view's left edge
    const double top = centre.y * kTileSize - h / 2.0;
    const int x0 = static_cast<int>(std::floor(left / kTileSize));
    const int x1 = static_cast<int>(std::floor((left + w - 1e-9) / kTileSize));
    const int y0 = std::max(0, static_cast<int>(std::floor(top / kTileSize)));
    const int y1 = std::min(n - 1, static_cast<int>(std::floor((top + h - 1e-9) / kTileSize)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            ViewTile v;
            v.id = TileId{zoom, ((x % n) + n) % n, y};
            v.left = x * static_cast<double>(kTileSize) - left;
            v.top = y * static_cast<double>(kTileSize) - top;
            out.push_back(v);
        }
    return out;
}

void draw_marker(exports::Canvas& c, double x, double y, double size) {
    const exports::Rgba8 red{214, 39, 40, 255};
    const exports::Rgba8 white{255, 255, 255, 255};
    const double r = size * 0.36;
    const double cy = y - size + r;
    c.fill_circle(x, cy, r + 1.5, white);
    c.fill_triangle(x - r * 0.75, cy + r * 0.55, x + r * 0.75, cy + r * 0.55, x, y, red);
    c.fill_circle(x, cy, r, red);
    c.fill_circle(x, cy, r * 0.4, white);
}

MapPicture compose_map(const fs::path& cache, LatLon centre, int zoom, int w, int h) {
    MapPicture m;
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m.image = exports::Canvas(std::max(1, w), std::max(1, h), exports::Rgba8{232, 232, 232, 255});
    const TilePoint c = to_tile(centre, zoom);
    for (const ViewTile& v : view_tiles(c, zoom, w, h)) {
        std::ifstream in(tile_file(cache, v.id), std::ios::binary);
        std::vector<unsigned char> bytes;
        if (in) bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        int tw = 0, th = 0, comp = 0;
        stbi_uc* px = bytes.empty() ? nullptr
                                    : stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &tw, &th, &comp, 4);
        if (px == nullptr) {
            ++m.missing;
            continue;
        }
        m.image.draw_rgba(px, tw, th, v.left, v.top, kTileSize, kTileSize);
        stbi_image_free(px);
        ++m.tiles;
    }
    draw_marker(m.image, w / 2.0, h / 2.0, std::max(16.0, std::min(w, h) / 10.0));
    return m;
}

}  // namespace yantra::about
