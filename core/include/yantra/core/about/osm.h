// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the OpenStreetMap side of the "About building" tab, without any
// network code (the app fetches through libcurl, app/net; the tests use a
// fake): the Web Mercator tile arithmetic of the "slippy map" tiles, the
// Nominatim search URL and the parsing of its answer, the tile URLs and
// the layout of the on-disk tile cache, and a map picture made from the
// cached tiles only (the report, EXP-05).
//
// Usage policies (docs/ARCHITECTURE.md "OpenStreetMap"): Nominatim is
// asked once per press of "Find on map", never automatically, with the
// client name "Yantra/<version>" and no personal data; tiles are fetched
// only while the map is shown, cached on disk and reused offline. The map
// data is (c) OpenStreetMap contributors, ODbL; the attribution
// kAttribution is shown with every map, on screen and in reports.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/export/raster.h"

namespace yantra::about {

/// The attribution every map shows (OSM licence, ODbL).
inline constexpr const char* kAttribution = "\xc2\xa9 OpenStreetMap contributors";
/// Where the attribution links to.
inline constexpr const char* kCopyrightUrl = "https://www.openstreetmap.org/copyright";
inline constexpr const char* kNominatimBase = "https://nominatim.openstreetmap.org";
inline constexpr const char* kTileBase = "https://tile.openstreetmap.org";
inline constexpr int kTileSize = 256;  ///< pixels
inline constexpr int kMinZoom = 1;
inline constexpr int kMaxZoom = 19;
/// The Web Mercator latitude limit (the tiles cover +- this).
inline constexpr double kMaxLatitude = 85.0511287798066;

struct LatLon {
    double lat = 0.0;  ///< degrees north
    double lon = 0.0;  ///< degrees east
};

/// A tile of zoom z: x and y in 0 .. 2^z - 1.
struct TileId {
    int z = 0, x = 0, y = 0;
    bool operator==(const TileId&) const = default;
    auto operator<=>(const TileId&) const = default;
};

/// A position in tile units of a zoom level (x + 0.5 is the middle of tile x).
struct TilePoint {
    double x = 0.0, y = 0.0;
};

TilePoint to_tile(LatLon p, int zoom);
LatLon from_tile(TilePoint t, int zoom);
/// Latitude clamped to +-kMaxLatitude, longitude wrapped to -180..180.
LatLon normalised(LatLon p);

/// Percent-encoding of a query value (RFC 3986 unreserved characters kept).
std::string url_encode(std::string_view text);

/// The Nominatim search for one address: JSON, at most one result.
std::string nominatim_search_url(std::string_view address, std::string_view base = kNominatimBase);

struct GeocodeResult {
    LatLon position;
    std::string display_name;  ///< Nominatim's name of the place found
};

/// Nominatim's answer: the first result, or nullopt with `error` set
/// ("no place found" for an empty list; a reason for text that is not the
/// expected JSON).
std::optional<GeocodeResult> parse_nominatim(std::string_view json, std::string* error = nullptr);

/// The tile's URL ("{base}/{z}/{x}/{y}.png").
std::string tile_url(TileId t, std::string_view base = kTileBase);
/// The tile's file in the cache: "<cache>/<z>/<x>/<y>.png".
std::filesystem::path tile_file(const std::filesystem::path& cache, TileId t);

/// A tile of the view: where its top-left corner is drawn, relative to the
/// view's top-left corner (pixels).
struct ViewTile {
    TileId id;
    double left = 0.0, top = 0.0;
};

/// The tiles a view of w x h pixels centred on `centre` shows at `zoom`
/// (x wraps round the world; rows above and below the world are left out).
std::vector<ViewTile> view_tiles(TilePoint centre, int zoom, double w, double h);

/// A map picture of the cached tiles only (no network): w x h pixels,
/// centred on `centre`, with a marker there. Tiles missing from the
/// cache stay light grey.
struct MapPicture {
    exports::Canvas image;
    int tiles = 0;    ///< tiles drawn from the cache
    int missing = 0;  ///< tiles not in the cache (or not readable)
};
MapPicture compose_map(const std::filesystem::path& cache, LatLon centre, int zoom, int w, int h);

/// Draws the map marker (a red pin with its point at x, y).
void draw_marker(exports::Canvas& c, double x, double y, double size);

}  // namespace yantra::about
