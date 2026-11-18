// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Spectra from the radios (radio API 1.2, radios RAD-140; Yantra UI-24):
// a radio plugin with YR_CAP_SPECTRUM delivers, while its plan has
// YR_PLAN_SPECTRUM, about once a second the power per frequency bin of a
// band through yr_host_api::push_spectrum. The radio host checks each one
// (radio::validate_spectrum), copies it into a Spectrum and hands it to
// the SpectrumInput of RadioHostConfig on the plugin's thread.
//
// SpectrumStore keeps the latest spectrum and a bounded history (the
// waterfall, about the last 120 s) for the UI: thread-safe, never blocks
// longer than a copy, the power kept as float. A dBFS spectrum is
// corrected with the user's calibration (RF-02) at its centre frequency,
// as the records are; a dBm spectrum gets the antenna correction only.
//
// Power per bin only: no IQ, no DSP here (RAD-26). Spectra are never
// recorded (record files only name the capability, RAD-65).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yantra/core/calibration/key.h"
#include "yantra/core/measure/correction.h"

namespace yantra::radio {

// One spectrum, copied from a yr_spectrum.
struct Spectrum {
    std::string plugin_id;
    std::string device_key;
    std::uint64_t stream_id = 0;
    std::uint32_t flags = 0;  // YR_RECORD_* within YR_SPECTRUM_FLAGS_MASK, as delivered
    bool dbm = false;         // yr_record_level_is_dbm(capabilities, flags), or calibrated
    bool calibrated = false;  // a calibration entry turned dBFS into dBm (RF-02)
    bool simulated = false;   // the plugin is SIMULATED
    std::int64_t monotonic_ns = 0;
    std::int64_t utc_ns = 0;
    double start_hz = 0.0;     // lower edge of the first bin
    double stop_hz = 0.0;      // upper edge of the last bin
    double bin_width_hz = 0.0;
    std::vector<float> power_db;  // one value per bin
    std::shared_ptr<const calibration::Key> calibration_key;  // the device's key at its time

    std::size_t bin_count() const noexcept { return power_db.size(); }
    double bin_centre_hz(std::size_t i) const noexcept {
        return start_hz + (static_cast<double>(i) + 0.5) * bin_width_hz;
    }
};

// Receiver of the spectra of every stream (any plugin thread; never blocks).
class SpectrumInput {
public:
    virtual ~SpectrumInput() = default;
    virtual void on_spectrum(Spectrum spectrum) = 0;
};

// Latest spectrum and the history ring of the waterfall. Thread-safe.
class SpectrumStore final : public SpectrumInput {
public:
    static constexpr std::size_t kDefaultHistory = 120;  // ~120 s at one per second

    explicit SpectrumStore(std::size_t history = kDefaultHistory);

    // RF-02: corrects dBFS spectra (nullptr: none, they stay dBFS).
    void set_corrector(std::shared_ptr<const measure::LevelCorrector> corrector);

    void on_spectrum(Spectrum spectrum) override;

    // The newest spectrum (nullptr none).
    std::shared_ptr<const Spectrum> latest() const;
    // The kept spectra, oldest first (at most history()).
    std::vector<std::shared_ptr<const Spectrum>> history() const;
    std::size_t capacity() const noexcept { return capacity_; }
    // Spectra received since construction (changes with every new one).
    std::uint64_t received() const;
    // Forgets the kept spectra (another radio or band).
    void clear();

private:
    const std::size_t capacity_;
    mutable std::mutex m_;
    std::shared_ptr<const measure::LevelCorrector> corrector_;
    std::vector<std::shared_ptr<const Spectrum>> ring_;  // ring buffer of capacity_
    std::size_t next_ = 0;
    std::uint64_t received_ = 0;
};

}  // namespace yantra::radio
