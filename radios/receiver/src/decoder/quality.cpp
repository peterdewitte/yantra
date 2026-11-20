// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/quality.cpp,
// adapted for RAD-113.

#include "yantra/receiver/quality.hpp"

#include "yantra/receiver/carrier_decoder.hpp"

#include <algorithm>
#include <cmath>

namespace yantra::receiver::tetra {

std::optional<double> m2m4_snr_db(double m2, double m4)
{
    const double d = 2.0 * m2 * m2 - m4;
    if (!(m2 > 0.0) || !(d > 0.0))
        return std::nullopt;  // noise only (or too few samples to tell)
    const double s = std::sqrt(d);
    const double n = m2 - s;
    if (!(n > 0.0))
        return 99.0;
    return std::min(99.0, 10.0 * std::log10(s / n));
}

QualityMeter::QualityMeter(const QualityConfig& config) : cfg_(config) {}

void QualityMeter::reset()
{
    slots_.clear();
    last_time_.reset();
    symbols_ = snr_symbols_ = freq_samples_ = 0;
    proj_ = p2_ = m2_ = m4_ = freq_sum_ = 0.0;
}

void QualityMeter::restart_windows()
{
    slots_.clear();
    last_time_.reset();
}

void QualityMeter::add_symbols(const DemodSymbol* s, std::size_t n, bool demod_locked, bool burst_locked)
{
    if (!demod_locked && !burst_locked)
        return;
    for (std::size_t k = 0; k < n; ++k) {
        const cf z = s[k].z;
        const double p = static_cast<double>(std::norm(z));
        if (!std::isfinite(p))
            continue;
        m2_ += p;
        m4_ += p * p;
        ++snr_symbols_;
        if (demod_locked) {
            proj_ += static_cast<double>((z * std::conj(s[k].ideal)).real());
            p2_ += p;
            ++symbols_;
        }
    }
}

void QualityMeter::add_frequency(double offset_hz, uint64_t samples)
{
    if (!std::isfinite(offset_hz))
        return;
    freq_sum_ += offset_hz * static_cast<double>(samples);
    freq_samples_ += samples;
}

void QualityMeter::add_slot(const DecodedSlot& slot, bool counted)
{
    if (slot.lost) {
        restart_windows();  // what came before belongs to the lost lock
        return;
    }
    if (slot.found && slot.result.time_known)
        last_time_ = slot.result.time;
    if (!counted)
        return;
    SlotEntry e;
    e.found = slot.found;
    if (slot.found) {
        const SlotResult& r = slot.result;
        if (r.aach_present) {
            e.aach_failed = !r.aach.decoded;
            const auto a = static_cast<std::size_t>(LogicalChannel::Aach);
            e.blocks[a] = 1;
            e.errors[a] = e.aach_failed ? 1 : 0;
        }
        for (const DecodedBlock& b : r.blocks) {
            if (b.channel == LogicalChannel::Tch || b.channel == LogicalChannel::Unknown)
                continue;  // traffic is never decoded: no CRC
            const auto c = static_cast<std::size_t>(b.channel);
            ++e.blocks[c];
            e.errors[c] += b.crc_ok ? 0 : 1;
            if (b.crc_ok) {
                e.bits = static_cast<uint16_t>(e.bits + b.coded_bits);
                e.bit_errors = static_cast<uint16_t>(e.bit_errors + b.bit_errors);
            }
        }
    }
    slots_.push_back(e);
    const std::size_t keep = std::max(cfg_.erasure_window_slots, cfg_.crc_window_slots);
    while (slots_.size() > keep)
        slots_.pop_front();
}

QualityReport QualityMeter::report(double sample_pos, uint64_t samples, const DemodStatus& demod,
                                   bool burst_locked)
{
    QualityReport q;
    q.sample_pos = sample_pos;
    q.samples = samples;
    q.burst_locked = burst_locked;
    q.demod_locked = demod.locked;
    q.time = last_time_;

    // Newest first: the erasure window is the last erasure_window_slots of
    // all counted slots; the CRC window the last crc_window_slots of them,
    // of which only those with a burst have blocks.
    std::size_t k = 0;
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it, ++k) {
        const SlotEntry& e = *it;
        if (k < cfg_.erasure_window_slots) {
            ++q.erasure_slots;
            q.aach_failures += e.found && e.aach_failed ? 1 : 0;
            q.missed_slots += e.found ? 0 : 1;
        }
        if (k < cfg_.crc_window_slots && e.found) {
            ++q.crc_slots;
            for (std::size_t c = 0; c < kLogicalChannelCount; ++c) {
                q.channels[c].blocks += e.blocks[c];
                q.channels[c].crc_errors += e.errors[c];
            }
            q.ber_bits += e.bits;
            q.ber_errors += e.bit_errors;
        }
    }

    q.symbols = symbols_;
    if (symbols_ > 0) {
        const double n = static_cast<double>(symbols_);
        const double a = proj_ / n;  // least-squares gain onto the ideal points
        const double err = p2_ / n - a * a;
        if (a > 0.0) {
            const double mer = err > 0.0 ? std::min(99.0, 10.0 * std::log10(a * a / err)) : 99.0;
            q.modulation_error_ratio_db = mer;
            q.evm_percent = 100.0 * std::pow(10.0, -mer / 20.0);
        }
    }
    q.snr_symbols = snr_symbols_;
    if (snr_symbols_ > 0) {
        const double n = static_cast<double>(snr_symbols_);
        q.snr_db = m2m4_snr_db(m2_ / n, m4_ / n);
    }
    if (freq_samples_ > 0)
        q.frequency_error_hz = freq_sum_ / static_cast<double>(freq_samples_);
    symbols_ = snr_symbols_ = freq_samples_ = 0;
    proj_ = p2_ = m2_ = m4_ = freq_sum_ = 0.0;
    return q;
}

}  // namespace yantra::receiver::tetra
