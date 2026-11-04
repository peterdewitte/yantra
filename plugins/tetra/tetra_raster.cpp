// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA channel raster (tetra_raster.h). The duplex spacing table is
// copied from yantra-radios 205ca7a: receiver/src/decoder/broadcast.cpp
// (kDuplexSpacingKhz, TS 100 392-15), so both sides use the same data.

#include "tetra_raster.h"

namespace yantra::tetra {

namespace {

// Duplex spacing in kHz per duplex spacing code (row) and band code
// (column); -1 = reserved. Code 2 is "0 Hz" (simplex) in every band.
constexpr std::int32_t kDuplexSpacingKhz[8][16] = {
    {-1, 1600, 10000, 10000, 10000, 10000, 10000, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, 4500, -1, 36000, 7000, -1, -1, -1, 45000, 45000, -1, -1, -1, -1, -1, -1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {-1, -1, -1, 8000, 8000, -1, -1, -1, 18000, 18000, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, 18000, 5000, -1, 30000, 30000, -1, 39000, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, 9500, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

}  // namespace

std::optional<std::uint64_t> downlink_hz(const Carrier& c) noexcept
{
    if (c.band > kMaxBand || c.carrier > kMaxCarrier || c.offset > 3)
        return std::nullopt;
    const std::int64_t f = static_cast<std::int64_t>(c.band * kBandHz) +
                           static_cast<std::int64_t>(c.carrier) * static_cast<std::int64_t>(kRasterHz) +
                           kOffsetHz[c.offset];
    if (f <= 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(f);
}

std::optional<Carrier> carrier_of(std::uint64_t f) noexcept
{
    const std::uint64_t band = f / kBandHz;
    if (band > kMaxBand)
        return std::nullopt;
    const std::int64_t rest = static_cast<std::int64_t>(f - band * kBandHz);
    for (std::uint32_t off = 0; off < 4; ++off) {
        const std::int64_t on_raster = rest - kOffsetHz[off];
        if (on_raster < 0 || on_raster % static_cast<std::int64_t>(kRasterHz) != 0)
            continue;
        const std::int64_t carrier = on_raster / static_cast<std::int64_t>(kRasterHz);
        if (carrier > kMaxCarrier)
            return std::nullopt;
        return Carrier{static_cast<std::uint32_t>(band), static_cast<std::uint32_t>(carrier), off};
    }
    return std::nullopt;
}

std::optional<std::uint64_t> duplex_spacing_hz(std::uint32_t band, std::uint32_t duplex) noexcept
{
    if (band > kMaxBand || duplex > 7)
        return std::nullopt;
    const std::int32_t khz = kDuplexSpacingKhz[duplex][band];
    if (khz < 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(khz) * 1000u;
}

std::optional<std::uint64_t> uplink_hz(const Carrier& c, std::uint32_t duplex, bool reverse) noexcept
{
    const auto dl = downlink_hz(c);
    const auto spacing = duplex_spacing_hz(c.band, duplex);
    if (!dl || !spacing)
        return std::nullopt;
    if (reverse)
        return *dl + *spacing;
    if (*dl < *spacing)
        return std::nullopt;
    return *dl - *spacing;
}

std::optional<std::uint32_t> channel_of(std::uint64_t f) noexcept
{
    const auto c = carrier_of(f);
    if (!c)
        return std::nullopt;
    for (const BandDef& b : kBands)
        if (c->band == b.band && c->carrier >= b.first_carrier && c->carrier <= b.last_carrier)
            return c->carrier;
    return std::nullopt;
}

std::optional<std::uint64_t> frequency_of(std::uint32_t channel) noexcept
{
    for (const BandDef& b : kBands)
        if (channel >= b.first_carrier && channel <= b.last_carrier)
            return downlink_hz({b.band, channel, 0});
    return std::nullopt;
}

}  // namespace yantra::tetra
