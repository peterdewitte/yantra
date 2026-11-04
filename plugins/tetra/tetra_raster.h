// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA channel raster (PLG-02 bands and channel raster; DB-07 data side):
// carrier number <-> downlink frequency of EN 300 392-2 clause 21.4.4.1
// (MAC-SYSINFO main carrier, frequency band, offset) and the duplex
// spacing table of ETSI TS 100 392-15, both ways. Pure functions, no
// state (PLG-07). The technology plugin's bands (tetra_technology.cpp)
// and its frequency_to_channel / channel_to_frequency are built on them;
// the channel calculator (DB-07, phase 9) uses the full functions.
//
//   downlink = band x 100 MHz + carrier x 25 kHz + offset
//   offset   = 0, +6.25, -6.25 or +12.5 kHz (offset code 0..3)
//   uplink   = downlink - duplex spacing (+ with reverse operation)
//
// The duplex spacing table and the downlink formula are the ones the
// yantra-radios receiver decodes with (receiver/src/decoder/broadcast.cpp
// at yantra-radios 205ca7a, itself checked against osmo-tetra), so a
// channel Yantra computes is the channel the radio reports.
#pragma once

#include <cstdint>
#include <optional>

namespace yantra::tetra {

inline constexpr std::uint64_t kBandHz = 100'000'000;  // one frequency band code
inline constexpr std::uint64_t kRasterHz = 25'000;     // carrier raster
inline constexpr std::uint32_t kMaxCarrier = 4095;     // 12-bit carrier number
inline constexpr std::uint32_t kMaxBand = 15;          // 4-bit frequency band code
// Carrier offset per offset code (MAC-SYSINFO), Hz.
inline constexpr std::int64_t kOffsetHz[4] = {0, 6'250, -6'250, 12'500};

// A carrier as broadcast: band code 1..15, carrier number 0..4095, offset code 0..3.
struct Carrier {
    std::uint32_t band = 0;
    std::uint32_t carrier = 0;
    std::uint32_t offset = 0;
    bool operator==(const Carrier&) const = default;
};

// Downlink frequency of a carrier; empty for codes out of range or a
// frequency below 0 (band 0).
std::optional<std::uint64_t> downlink_hz(const Carrier& c) noexcept;

// The carrier of a downlink frequency: band = frequency / 100 MHz, the one
// offset that puts it on the 25 kHz raster, carrier = the rest / 25 kHz.
// The answer is unique; empty when the frequency is on none of the four
// offset rasters or out of range.
std::optional<Carrier> carrier_of(std::uint64_t downlink_hz) noexcept;

// Duplex spacing (TS 100 392-15 table) of a band code and duplex spacing
// code 0..7; empty when the combination is reserved.
std::optional<std::uint64_t> duplex_spacing_hz(std::uint32_t band, std::uint32_t duplex) noexcept;

// Uplink of a carrier with the duplex spacing code and reverse operation
// ("RX above TX") of the cell; empty when the spacing is reserved or the
// uplink would be below 0.
std::optional<std::uint64_t> uplink_hz(const Carrier& c, std::uint32_t duplex, bool reverse) noexcept;

// One downlink band of the plugin (yantra_band): carriers first..last of
// a band code, offset 0, 25 kHz apart. The carrier ranges of the bands do
// not overlap, so a carrier number alone names a frequency.
struct BandDef {
    const char* id;
    const char* label;
    std::uint32_t band;
    std::uint32_t first_carrier;
    std::uint32_t last_carrier;
};

// The TETRA downlink bands of the plugin (RAD-110 tuning range 350-470 MHz,
// the 25 kHz allocations of TS 100 392-15): 380-400, 410-430 and
// 450-470 MHz. 350-370 MHz is left out: its carrier numbers (band 3,
// 2000-2799) are those of 450-470 MHz (band 4), so a channel number would
// name two frequencies.
inline constexpr BandDef kBands[] = {
    {"tetra_380", "TETRA 380-400 MHz", 3, 3200, 3999},
    {"tetra_410", "TETRA 410-430 MHz", 4, 400, 1199},
    {"tetra_450", "TETRA 450-470 MHz", 4, 2000, 2799},
};
inline constexpr std::uint32_t kBandCount = sizeof kBands / sizeof kBands[0];

// Channel (= carrier number, the radio records' `channel`) of a downlink
// frequency inside one of kBands, at any of the four offsets; empty
// outside every band.
std::optional<std::uint32_t> channel_of(std::uint64_t downlink_hz) noexcept;
// Downlink frequency (offset 0) of a channel of kBands; empty for a
// carrier number outside every band.
std::optional<std::uint64_t> frequency_of(std::uint32_t channel) noexcept;

}  // namespace yantra::tetra
