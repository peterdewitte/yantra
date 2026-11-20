// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/burst_sync.hpp"

#include <algorithm>
#include <cmath>

namespace yantra::receiver::tetra {
namespace {

const uint8_t* const kNormal1 = kNormalTraining1.data();
const uint8_t* const kNormal2 = kNormalTraining2.data();
const uint8_t* const kSyncSeq = kSyncTraining.data();
const uint8_t* const kFcf = kFrequencyCorrection.data();
constexpr int kFcfBit = kFrequencyCorrectionBit;
constexpr uint64_t kSlot = static_cast<uint64_t>(kSlotSymbols);

SoftBit to_soft(float v, float scale)
{
    const float s = std::round(v * scale);
    return static_cast<SoftBit>(std::clamp(s, -127.0f, 127.0f));
}

}  // namespace

std::span<const uint8_t> training_sequence(BurstType type) noexcept
{
    switch (type) {
    case BurstType::Sync:
        return kSyncTraining;
    case BurstType::NormalFull:
        return kNormalTraining1;
    case BurstType::NormalHalf:
        break;
    }
    return kNormalTraining2;
}

int training_sequence_bit(BurstType type) noexcept
{
    return type == BurstType::Sync ? kSyncTrainingBit : kNormalTrainingBit;
}

int bit_errors(std::span<const SoftBit> soft, std::span<const uint8_t> pattern) noexcept
{
    int errors = 0;
    const std::size_t n = std::min(soft.size(), pattern.size());
    for (std::size_t i = 0; i < n; ++i)
        errors += (soft[i] < 0 ? 1 : 0) != pattern[i];
    return errors;
}

BurstSync::BurstSync(const BurstSyncConfig& config) : cfg_(config) {}

void BurstSync::reset()
{
    soft_.clear();
    pos_.clear();
    base_ = end_ = search_ = next_start_ = 0;
    locked_ = false;
    missed_ = 0;
}

uint8_t BurstSync::hard_bit(uint64_t bit_index) const
{
    return soft_[static_cast<std::size_t>(bit_index - 2 * base_)] < 0 ? 1 : 0;
}

int BurstSync::errors_at(uint64_t start, int bit, const uint8_t* pattern, int length) const
{
    const uint64_t first = 2 * start + static_cast<uint64_t>(bit);
    int errors = 0;
    for (int i = 0; i < length; ++i)
        errors += hard_bit(first + static_cast<uint64_t>(i)) != pattern[i];
    return errors;
}

bool BurstSync::have(uint64_t first_symbol, uint64_t end_symbol) const
{
    return first_symbol >= base_ && end_symbol <= end_;
}

void BurstSync::push(const receiver::DemodSymbol* symbols, std::size_t n, std::vector<SlotEvent>& out)
{
    for (std::size_t i = 0; i < n; ++i) {
        soft_.push_back(to_soft(symbols[i].soft[0], cfg_.soft_scale));
        soft_.push_back(to_soft(symbols[i].soft[1], cfg_.soft_scale));
        pos_.push_back(symbols[i].sample_pos);
        ++end_;
    }
    for (;;) {
        if (!locked_) {
            acquire();
            if (!locked_)
                break;
        }
        if (!track(out))
            break;
    }
    compact();
}

// Looks for a synchronisation burst: training sequence and frequency
// correction field. `search_` is the candidate burst start.
void BurstSync::acquire()
{
    constexpr uint64_t kSyncSym = kSyncTrainingBit / 2;
    if (search_ < base_)
        search_ = base_;
    while (search_ + kSyncSym + 19 <= end_) {
        const uint64_t s = search_;
        const int e = errors_at(s, kSyncTrainingBit, kSyncSeq, 38);
        if (e <= cfg_.acquire_max_sync_errors &&
            errors_at(s, kFcfBit, kFcf, 80) <= cfg_.acquire_max_fcf_errors) {
            // track() then checks this start and its two neighbours.
            locked_ = true;
            missed_ = 0;
            next_start_ = s;
            return;
        }
        ++search_;
    }
}

// Decides the slot at `next_start_` when its symbols (and one more each
// side for the slip check) are there. Returns false when it has to wait.
bool BurstSync::track(std::vector<SlotEvent>& out)
{
    const bool coasting = cfg_.coast_slots > 0 && missed_ >= cfg_.max_missed;
    const int window = coasting ? std::max(1, cfg_.coast_window) : 1;
    const uint64_t w = static_cast<uint64_t>(window);
    const uint64_t lo = next_start_ > w ? next_start_ - w : 0;
    if (!have(lo, next_start_ + kSlot + w))
        return false;
    Candidate best{BurstType::NormalFull, 1 << 20, 0};
    double best_share = 2.0;
    // The expected position first, then outwards, early before late (a
    // tie keeps the first).
    for (int d = 0; d <= window; ++d) {
        for (int slip : {-d, d}) {
            if ((d == 0 && slip > 0) || (slip < 0 && next_start_ < static_cast<uint64_t>(-slip)))
                continue;
            const uint64_t s = next_start_ + static_cast<uint64_t>(static_cast<int64_t>(slip));
            Candidate c[3] = {
                {BurstType::NormalFull, errors_at(s, kNormalTrainingBit, kNormal1, 22), slip},
                {BurstType::NormalHalf, errors_at(s, kNormalTrainingBit, kNormal2, 22), slip},
                {BurstType::Sync, errors_at(s, kSyncTrainingBit, kSyncSeq, 38), slip}};
            if (coasting && c[2].errors <= cfg_.acquire_max_sync_errors &&
                errors_at(s, kFcfBit, kFcf, 80) > cfg_.acquire_max_fcf_errors)
                c[2].errors = 1 << 20;  // while coasting a sync burst needs its FCF too
            for (const Candidate& k : c) {
                const int limit = k.type == BurstType::Sync
                                      ? (coasting ? cfg_.acquire_max_sync_errors : cfg_.track_max_sync_errors)
                                      : (coasting ? cfg_.coast_max_normal_errors : cfg_.track_max_normal_errors);
                if (k.errors > limit)
                    continue;
                // Compare as error shares; the expected position wins a tie.
                const double share = static_cast<double>(k.errors) / (k.type == BurstType::Sync ? 38.0 : 22.0);
                if (share < best_share - 1e-9) {
                    best = k;
                    best_share = share;
                }
            }
        }
    }
    if (best_share <= 1.0) {
        missed_ = 0;
        emit(next_start_ + static_cast<uint64_t>(static_cast<int64_t>(best.slip)), best.type, best.errors,
             best.slip, out);
        return true;
    }
    SlotEvent ev;
    ev.found = false;
    ev.coasting = coasting;
    ev.burst.sample_pos = pos_[static_cast<std::size_t>(next_start_ - base_)];
    if (++missed_ >= cfg_.max_missed + std::max(0, cfg_.coast_slots)) {
        ev.lost = true;
        locked_ = false;
        search_ = next_start_ + kSlot;
    }
    next_start_ += kSlot;
    out.push_back(ev);
    return true;
}

void BurstSync::emit(uint64_t start, BurstType type, int errors, int slip, std::vector<SlotEvent>& out)
{
    SlotEvent ev;
    ev.found = true;
    ev.slip = slip;
    ev.burst.type = type;
    ev.burst.training_errors = errors;
    ev.burst.sample_pos = pos_[static_cast<std::size_t>(start - base_)];
    const std::size_t first = static_cast<std::size_t>(2 * (start - base_));
    std::copy_n(soft_.begin() + static_cast<std::ptrdiff_t>(first), kSlotBits, ev.burst.soft.begin());
    next_start_ = start + kSlot;
    out.push_back(std::move(ev));
}

// Drops symbols nobody needs any more (keeps one slot of history).
void BurstSync::compact()
{
    const uint64_t margin = static_cast<uint64_t>(std::max(2, cfg_.coast_window + 1));
    const uint64_t keep_from = locked_ ? (next_start_ > margin ? next_start_ - margin : 0) : search_;
    if (keep_from <= base_ + 4096)
        return;
    const uint64_t drop = keep_from - base_;
    soft_.erase(soft_.begin(), soft_.begin() + static_cast<std::ptrdiff_t>(2 * drop));
    pos_.erase(pos_.begin(), pos_.begin() + static_cast<std::ptrdiff_t>(drop));
    base_ = keep_from;
}

}  // namespace yantra::receiver::tetra
