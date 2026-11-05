// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiled, zoom-dependent rendering of large sheets (IND-02: sheets larger
// than 15000 px, e.g. A0 at a high resolution). A page (or a trimmed
// floor) is cut into square tiles at levels of detail: level 0 is the
// finest (base scale, e.g. 300 dpi for a PDF, 1:1 for an image), every
// next level halves the scale, up to the level where the whole extent
// fits in one tile. The view asks for the tiles it shows at the level
// that matches its zoom (TileLayout::lod_for); a worker thread renders
// missing tiles in the background into a cache with a memory budget
// (least recently used tiles are evicted first).
//
// Tiles are deterministic: a tile is a window of the level's raster
// (PlanSource::render, render_floor), so its pixels depend only on its
// key, and neighbouring tiles fit together exactly.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plan/tiles.h.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "yantra/core/plan/source.h"

namespace yantra::plan {

struct TileKey {
    int page = 0;
    int lod = 0;  ///< level of detail, 0 = finest
    std::int64_t tx = 0;
    std::int64_t ty = 0;
    bool operator==(const TileKey&) const = default;
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const noexcept;
};

/// How an extent is cut into tiles.
class TileLayout {
public:
    /// `extent` in units (page or floor units), `base_scale` pixels per
    /// unit at level 0, square tiles of `tile_px` pixels (16..4096).
    TileLayout(PageSize extent, double base_scale, int tile_px = 256);

    const PageSize& extent() const noexcept { return extent_; }
    int tile_px() const noexcept { return tile_px_; }
    double base_scale() const noexcept { return base_scale_; }
    /// The coarsest level: the whole extent fits in one tile.
    int max_lod() const noexcept { return max_lod_; }
    /// Pixels per unit at a level: base_scale / 2^lod.
    double scale(int lod) const;
    PixelSize pixels(int lod) const;
    std::int64_t tiles_x(int lod) const;
    std::int64_t tiles_y(int lod) const;
    /// The tile's window of the level raster (edge tiles are smaller).
    PixelRect tile_rect(const TileKey& key) const;
    /// The level to show at a view scale (screen pixels per unit): the
    /// coarsest level that is still at least as sharp as the view.
    int lod_for(double view_scale) const;
    /// Tiles of `lod` that cover the unit rectangle [x0, x1) x [y0, y1),
    /// row by row (clipped to the extent).
    std::vector<TileKey> tiles_for(int page, int lod, double x0, double y0, double x1, double y1) const;

private:
    PageSize extent_;
    double base_scale_ = 1.0;
    int tile_px_ = 256;
    int max_lod_ = 0;
};

struct TileStats {
    std::size_t budget_bytes = 0;
    std::size_t bytes = 0;  ///< pixel bytes of the cached tiles
    std::size_t tiles = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t rendered = 0;   ///< tiles rendered by the worker
    std::uint64_t evicted = 0;    ///< tiles dropped for the budget
    std::uint64_t cancelled = 0;  ///< queued or in-flight tiles dropped by cancel()/request()
    std::uint64_t failed = 0;     ///< renders that threw (last_error)
};

/// Background tile rendering with a memory-bounded LRU cache.
///
/// Threading: the public calls are made from one thread (the UI); the
/// render function and the ready callback run on the worker thread, the
/// callback without any lock held. The destructor cancels and joins.
class TileRenderer {
public:
    /// Renders `window` of the raster at `scale` (e.g. PlanSource::render
    /// or render_floor). Called on the worker thread only.
    using RenderFn = std::function<Raster(int page, double scale, const PixelRect& window)>;
    using ReadyFn = std::function<void(const TileKey&)>;

    TileRenderer(TileLayout layout, RenderFn render, std::size_t budget_bytes, ReadyFn ready = {});
    ~TileRenderer();
    TileRenderer(const TileRenderer&) = delete;
    TileRenderer& operator=(const TileRenderer&) = delete;

    const TileLayout& layout() const noexcept { return layout_; }
    /// A cached tile (and marks it recently used), or nullptr.
    std::shared_ptr<const Raster> find(const TileKey& key);
    /// The tiles the view needs now, most important first: queues those
    /// not cached and drops queued tiles of an earlier request that are no
    /// longer wanted (the view moved on).
    void request(const std::vector<TileKey>& keys);
    /// Drops every queued tile; a tile being rendered is discarded when it
    /// finishes.
    void cancel();
    /// No queued and no in-flight tile.
    bool idle() const;
    /// Blocks until idle() (tests and headless renders).
    void wait_idle();
    TileStats stats() const;
    /// The message of the last failed render, "" if none.
    std::string last_error() const;

private:
    void run();
    void insert_locked(const TileKey& key, std::shared_ptr<const Raster> tile);

    TileLayout layout_;
    RenderFn render_;
    ReadyFn ready_;
    mutable std::mutex mutex_;
    std::condition_variable work_cv_;
    mutable std::condition_variable idle_cv_;
    std::deque<TileKey> queue_;
    bool busy_ = false;
    TileKey in_flight_;
    std::uint64_t generation_ = 0;
    bool stop_ = false;
    std::list<TileKey> lru_;  // front = most recently used
    struct Entry {
        std::shared_ptr<const Raster> tile;
        std::list<TileKey>::iterator pos;
    };
    std::unordered_map<TileKey, Entry, TileKeyHash> cache_;
    TileStats stats_;
    std::string last_error_;
    std::thread worker_;  // last: started after everything above exists
};

}  // namespace yantra::plan
