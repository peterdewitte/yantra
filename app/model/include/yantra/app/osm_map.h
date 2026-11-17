// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the network side of the About building tab's map, through the
// app's Downloader (sheet_fetch.h; libcurl in the app, a fake in the
// tests: no test uses the network). Usage policies of the OpenStreetMap
// Foundation (docs/ARCHITECTURE.md "OpenStreetMap"):
//
// - MapTiles: a tile is fetched only when the map shows it and it is not
//   in the disk cache (<user cache>/yantra/tiles/osm/<z>/<x>/<y>.png), one
//   at a time on one worker thread, with the client name "Yantra/<version>";
//   a cached tile older than kTileMaxAge is fetched again when shown and
//   online, and used as it is meanwhile and offline. A failed tile is not
//   asked again for kRetryAfter. Tiles the map no longer shows are dropped
//   from the queue (cancel_pending()).
// - Geocoder: one Nominatim search per press of "Find on map", never
//   automatically; a new search waits until the previous one is done and
//   at least one second has passed (Nominatim: at most 1 request/s).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "yantra/app/sheet_fetch.h"
#include "yantra/core/about/osm.h"

namespace yantra::app {

inline constexpr std::chrono::hours kTileMaxAge{24 * 7};
inline constexpr std::chrono::seconds kRetryAfter{300};

class MapTiles {
public:
    enum class State {
        Ready,    ///< `file` is the cached tile (it may be old and asked for again)
        Loading,  ///< not cached yet; asked for
        Missing,  ///< not cached and not asked for (offline, or it failed)
    };
    /// `cache` "" keeps no tiles (and fetches none); `downloader` nullptr
    /// is offline (cached tiles only). `tile_base` is the tile server.
    MapTiles(std::filesystem::path cache, std::shared_ptr<Downloader> downloader,
             std::string tile_base = about::kTileBase);
    ~MapTiles();
    MapTiles(const MapTiles&) = delete;
    MapTiles& operator=(const MapTiles&) = delete;

    void set_downloader(std::shared_ptr<Downloader> downloader);
    bool online() const;
    const std::filesystem::path& cache() const { return cache_; }

    /// A tile the map shows now.
    State tile(const about::TileId& id, std::filesystem::path* file = nullptr);
    /// Drops the queued tiles (the map is no longer shown).
    void cancel_pending();
    /// Changes when a fetched tile arrived (the view reloads its textures).
    std::uint64_t generation() const { return generation_.load(); }
    /// Requests sent so far (tests).
    std::size_t requests() const { return requests_.load(); }
    /// True while tiles are queued or being fetched.
    bool busy() const;
    /// The last failure ("" none).
    std::string last_error() const;

private:
    void run();
    std::filesystem::path cache_;
    std::string base_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<Downloader> downloader_;
    std::deque<about::TileId> queue_;
    std::set<about::TileId> queued_;
    std::map<about::TileId, std::chrono::steady_clock::time_point> failed_;
    std::optional<about::TileId> current_;
    std::string error_;
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::size_t> requests_{0};
    bool stop_ = false;
    std::thread worker_;
};

class Geocoder {
public:
    struct Outcome {
        std::optional<about::GeocodeResult> result;
        std::string error;  ///< "" when found
    };
    /// `scratch` holds the answer while it is read (the cache folder).
    Geocoder(std::shared_ptr<Downloader> downloader, std::filesystem::path scratch,
             std::string base = about::kNominatimBase);
    ~Geocoder();
    Geocoder(const Geocoder&) = delete;
    Geocoder& operator=(const Geocoder&) = delete;

    void set_downloader(std::shared_ptr<Downloader> downloader);
    bool online() const;
    /// Starts one search; false (with `why`) when offline, the address is
    /// empty or a search is running.
    bool start(const std::string& address, std::string* why = nullptr);
    bool busy() const { return busy_.load(); }
    /// The finished search, once.
    std::optional<Outcome> take();
    std::size_t requests() const { return requests_.load(); }

private:
    std::shared_ptr<Downloader> downloader_;
    std::filesystem::path scratch_;
    std::string base_;
    mutable std::mutex mutex_;
    std::optional<Outcome> done_;
    std::atomic<bool> busy_{false};
    std::atomic<std::size_t> requests_{0};
    std::chrono::steady_clock::time_point last_{};
    std::thread worker_;
};

}  // namespace yantra::app
