// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/plan/tiles.cpp.

#include "yantra/core/plan/tiles.h"

#include <algorithm>
#include <cmath>

namespace yantra::plan {

using Code = PlanError::Code;

std::size_t TileKeyHash::operator()(const TileKey& k) const noexcept {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(static_cast<std::uint64_t>(k.page));
    mix(static_cast<std::uint64_t>(k.lod));
    mix(static_cast<std::uint64_t>(k.tx));
    mix(static_cast<std::uint64_t>(k.ty));
    return static_cast<std::size_t>(h);
}

TileLayout::TileLayout(PageSize extent, double base_scale, int tile_px)
    : extent_(extent), base_scale_(base_scale), tile_px_(tile_px) {
    if (!(extent.width > 0.0) || !(extent.height > 0.0) || !std::isfinite(extent.width) || !std::isfinite(extent.height))
        throw PlanError(Code::Invalid, "tile layout: the extent must be > 0");
    if (tile_px < 16 || tile_px > 4096) throw PlanError(Code::Invalid, "tile layout: tile size must be 16..4096 px");
    (void)page_pixels(extent, base_scale);  // validates the scale
    while (max_lod_ < 40) {
        const PixelSize p = pixels(max_lod_);
        if (p.width <= tile_px_ && p.height <= tile_px_) break;
        ++max_lod_;
    }
}

double TileLayout::scale(int lod) const { return std::ldexp(base_scale_, -lod); }

PixelSize TileLayout::pixels(int lod) const { return page_pixels(extent_, scale(lod)); }

std::int64_t TileLayout::tiles_x(int lod) const { return (pixels(lod).width + tile_px_ - 1) / tile_px_; }

std::int64_t TileLayout::tiles_y(int lod) const { return (pixels(lod).height + tile_px_ - 1) / tile_px_; }

PixelRect TileLayout::tile_rect(const TileKey& key) const {
    if (key.lod < 0 || key.lod > max_lod_) throw PlanError(Code::Invalid, "no tile level " + std::to_string(key.lod));
    const PixelSize p = pixels(key.lod);
    const std::int64_t x = key.tx * tile_px_;
    const std::int64_t y = key.ty * tile_px_;
    if (key.tx < 0 || key.ty < 0 || x >= p.width || y >= p.height) throw PlanError(Code::Invalid, "tile outside the extent");
    return {x, y, static_cast<int>(std::min<std::int64_t>(tile_px_, p.width - x)),
            static_cast<int>(std::min<std::int64_t>(tile_px_, p.height - y))};
}

int TileLayout::lod_for(double view_scale) const {
    if (!(view_scale > 0.0)) return max_lod_;
    int lod = 0;
    while (lod < max_lod_ && scale(lod + 1) >= view_scale) ++lod;
    return lod;
}

std::vector<TileKey> TileLayout::tiles_for(int page, int lod, double x0, double y0, double x1, double y1) const {
    std::vector<TileKey> out;
    if (lod < 0 || lod > max_lod_) return out;
    const double s = scale(lod);
    const auto clampi = [](double v, std::int64_t hi) {
        if (!(v > 0.0)) return std::int64_t{0};
        return std::min<std::int64_t>(hi, static_cast<std::int64_t>(std::min(v, 9.0e15)));
    };
    const std::int64_t nx = tiles_x(lod);
    const std::int64_t ny = tiles_y(lod);
    const std::int64_t tx0 = clampi(std::floor(x0 * s / tile_px_), nx);
    const std::int64_t ty0 = clampi(std::floor(y0 * s / tile_px_), ny);
    const std::int64_t tx1 = clampi(std::ceil(x1 * s / tile_px_), nx);
    const std::int64_t ty1 = clampi(std::ceil(y1 * s / tile_px_), ny);
    for (std::int64_t ty = ty0; ty < ty1; ++ty)
        for (std::int64_t tx = tx0; tx < tx1; ++tx) out.push_back({page, lod, tx, ty});
    return out;
}

// ---- TileRenderer -----------------------------------------------------------

TileRenderer::TileRenderer(TileLayout layout, RenderFn render, std::size_t budget_bytes, ReadyFn ready)
    : layout_(std::move(layout)), render_(std::move(render)), ready_(std::move(ready)) {
    stats_.budget_bytes = budget_bytes;
    worker_ = std::thread([this] { run(); });
}

TileRenderer::~TileRenderer() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        ++generation_;
        queue_.clear();
    }
    work_cv_.notify_all();
    worker_.join();
}

std::shared_ptr<const Raster> TileRenderer::find(const TileKey& key) {
    std::lock_guard lock(mutex_);
    const auto it = cache_.find(key);
    if (it == cache_.end()) {
        ++stats_.misses;
        return nullptr;
    }
    ++stats_.hits;
    lru_.splice(lru_.begin(), lru_, it->second.pos);
    return it->second.tile;
}

void TileRenderer::request(const std::vector<TileKey>& keys) {
    {
        std::lock_guard lock(mutex_);
        std::deque<TileKey> next;
        for (const TileKey& k : keys) {
            if (cache_.count(k) || (busy_ && in_flight_ == k)) continue;
            if (std::find(next.begin(), next.end(), k) != next.end()) continue;
            (void)layout_.tile_rect(k);  // throws for keys outside the layout
            next.push_back(k);
        }
        for (const TileKey& old : queue_)
            if (std::find(next.begin(), next.end(), old) == next.end()) ++stats_.cancelled;
        queue_ = std::move(next);
    }
    work_cv_.notify_all();
    idle_cv_.notify_all();
}

void TileRenderer::cancel() {
    std::lock_guard lock(mutex_);
    stats_.cancelled += queue_.size() + (busy_ ? 1u : 0u);
    queue_.clear();
    ++generation_;
    idle_cv_.notify_all();
}

bool TileRenderer::idle() const {
    std::lock_guard lock(mutex_);
    return queue_.empty() && !busy_;
}

void TileRenderer::wait_idle() {
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [&] { return queue_.empty() && !busy_; });
}

TileStats TileRenderer::stats() const {
    std::lock_guard lock(mutex_);
    TileStats s = stats_;
    s.tiles = cache_.size();
    return s;
}

std::string TileRenderer::last_error() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

void TileRenderer::insert_locked(const TileKey& key, std::shared_ptr<const Raster> tile) {
    const std::size_t size = tile->bytes();
    if (size > stats_.budget_bytes) {
        ++stats_.evicted;  // too large to keep at all
        return;
    }
    while (stats_.bytes + size > stats_.budget_bytes && !lru_.empty()) {
        const TileKey victim = lru_.back();
        lru_.pop_back();
        const auto it = cache_.find(victim);
        if (it == cache_.end()) continue;
        if (const Raster* t = it->second.tile.get()) stats_.bytes -= t->bytes();
        cache_.erase(it);
        ++stats_.evicted;
    }
    lru_.push_front(key);
    cache_[key] = Entry{std::move(tile), lru_.begin()};
    stats_.bytes += size;
}

void TileRenderer::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        work_cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const TileKey key = queue_.front();
        queue_.pop_front();
        busy_ = true;
        in_flight_ = key;
        const std::uint64_t gen = generation_;
        lock.unlock();

        std::shared_ptr<const Raster> tile;
        std::string error;
        try {
            const PixelRect r = layout_.tile_rect(key);
            tile = std::make_shared<const Raster>(render_(key.page, layout_.scale(key.lod), r));
        } catch (const std::exception& e) {
            error = e.what();
        }

        lock.lock();
        bool ready = false;
        if (!error.empty()) {
            ++stats_.failed;
            last_error_ = error;
        } else if (gen == generation_ && !stop_) {
            ++stats_.rendered;
            if (!cache_.count(key)) insert_locked(key, std::move(tile));
            ready = true;
        }
        if (ready && ready_) {
            lock.unlock();
            ready_(key);
            lock.lock();
        }
        busy_ = false;  // only now: wait_idle() also waits for the callback
        if (queue_.empty()) idle_cv_.notify_all();
    }
}

}  // namespace yantra::plan
