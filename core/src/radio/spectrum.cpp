// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Spectrum store (UI-24, RAD-140): latest + bounded history ring.

#include "yantra/core/radio/spectrum.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace yantra::radio {

SpectrumStore::SpectrumStore(std::size_t history) : capacity_(std::max<std::size_t>(1, history)) {
    ring_.reserve(capacity_);
}

void SpectrumStore::set_corrector(std::shared_ptr<const measure::LevelCorrector> corrector) {
    const std::lock_guard lock(m_);
    corrector_ = std::move(corrector);
}

void SpectrumStore::on_spectrum(Spectrum s) {
    std::shared_ptr<const measure::LevelCorrector> corrector;
    {
        const std::lock_guard lock(m_);
        corrector = corrector_;
    }
    if (corrector && !s.power_db.empty()) {
        // RF-02 as for the records: one correction at the centre frequency
        // (a spectrum covers a few MHz at most of one band).
        measure::LevelContext c;
        c.radio_plugin_id = s.plugin_id;
        c.device_key = s.device_key;
        c.frequency_hz = static_cast<std::uint64_t>(std::llround((s.start_hz + s.stop_hz) * 0.5));
        c.radio_dbm = s.dbm;
        c.simulated = s.simulated;
        c.calibration_key = s.calibration_key.get();
        const measure::LevelCorrection lc = corrector->correction(c);
        double add = lc.correction_db;
        if (!s.dbm && lc.calibrated) {
            add += lc.offset_db;
            s.dbm = true;
            s.calibrated = true;
        }
        if (add != 0.0)
            for (float& p : s.power_db) p = static_cast<float>(p + add);
    }
    auto item = std::make_shared<const Spectrum>(std::move(s));
    const std::lock_guard lock(m_);
    if (ring_.size() < capacity_) {
        ring_.push_back(std::move(item));
    } else {
        ring_[next_] = std::move(item);
    }
    next_ = (next_ + 1) % capacity_;
    ++received_;
}

std::shared_ptr<const Spectrum> SpectrumStore::latest() const {
    const std::lock_guard lock(m_);
    if (ring_.empty()) return nullptr;
    return ring_[(next_ + capacity_ - 1) % capacity_ % ring_.size()];
}

std::vector<std::shared_ptr<const Spectrum>> SpectrumStore::history() const {
    const std::lock_guard lock(m_);
    std::vector<std::shared_ptr<const Spectrum>> out;
    out.reserve(ring_.size());
    if (ring_.size() < capacity_) {
        out = ring_;
    } else {
        for (std::size_t i = 0; i < capacity_; ++i) out.push_back(ring_[(next_ + i) % capacity_]);
    }
    return out;
}

std::uint64_t SpectrumStore::received() const {
    const std::lock_guard lock(m_);
    return received_;
}

void SpectrumStore::clear() {
    const std::lock_guard lock(m_);
    ring_.clear();
    next_ = 0;
}

}  // namespace yantra::radio
