// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Checks a decoded D-NWRK-BROADCAST view (NetworkInfo) against the PDUs
// the generator sent: re-select parameters, cell load, network time and
// every element of every neighbour (DEC-02).
// Copied from yantra 68ea20a: tests/tetra_decoder/network_check.h.
#pragma once

#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

namespace dectest {

namespace gen = yantra::gen;
namespace dec = yantra::receiver::tetra;


inline uint16_t service_bits(const gen::BsServiceDetails& s)
{
    const bool f[12] = {s.registration,      s.deregistration, s.priority_cell, s.no_minimum_mode,
                        s.migration,         s.system_wide_services, s.voice_service, s.circuit_mode_data,
                        s.reserved,          s.sndcp_service,  s.air_interface_encryption, s.advanced_link};
    uint16_t v = 0;
    for (bool b : f)
        v = static_cast<uint16_t>((v << 1) | (b ? 1 : 0));
    return v;
}

/// What the decoder should report for a generated neighbour.
inline dec::NeighbourInfo expected(const gen::NeighbourCell& n)
{
    dec::NeighbourInfo e;
    e.cell_id = n.cell_id;
    e.reselection_types = n.reselection_types;
    e.synchronised = n.synchronised;
    e.cell_load = n.cell_load;
    e.main_carrier = n.main_carrier;
    if (n.carrier_extension)
        e.carrier_extension = dec::CarrierExtensionInfo{n.carrier_extension->frequency_band, n.carrier_extension->offset,
                                                        n.carrier_extension->duplex_spacing,
                                                        n.carrier_extension->reverse_operation};
    e.mcc = n.mcc;
    e.mnc = n.mnc;
    e.location_area = n.location_area;
    e.max_ms_txpwr = n.max_ms_txpwr;
    e.min_rx_access_level = n.min_rx_access_level;
    e.subscriber_class = n.subscriber_class;
    if (n.bs_service_details)
        e.service_details = service_bits(*n.bs_service_details);
    e.timeshare_security = n.timeshare_security;
    e.tdma_frame_offset = n.tdma_frame_offset;
    return e;
}

/// Checks a decoded network view against the PDUs a cell sends.
inline void check_network(const dec::NetworkInfo& got, const std::vector<gen::NwrkBroadcast>& sent)
{
    REQUIRE_FALSE(sent.empty());
    const gen::NwrkBroadcast& first = sent.front();
    CHECK(got.reselect.slow_threshold == first.reselect.slow_threshold);
    CHECK(got.reselect.slow_hysteresis == first.reselect.slow_hysteresis);
    CHECK(got.reselect.fast_threshold == first.reselect.fast_threshold);
    CHECK(got.reselect.fast_hysteresis == first.reselect.fast_hysteresis);
    CHECK(got.cell_load == first.cell_load);
    CHECK(got.network_time.has_value() == first.network_time.has_value());
    if (got.network_time && first.network_time) {
        CHECK(got.network_time->utc_time == first.network_time->utc_time);
        CHECK(got.network_time->local_offset_negative == first.network_time->local_offset_negative);
        CHECK(got.network_time->local_offset == first.network_time->local_offset);
        CHECK(got.network_time->year == first.network_time->year);
    }
    std::vector<dec::NeighbourInfo> want;
    for (const gen::NwrkBroadcast& p : sent)
        if (p.neighbours)
            for (const gen::NeighbourCell& n : *p.neighbours)
                want.push_back(expected(n));
    std::sort(want.begin(), want.end(), [](const auto& a, const auto& b) { return a.cell_id < b.cell_id; });
    REQUIRE(got.neighbours.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        INFO("neighbour ", int(want[i].cell_id));
        CHECK(got.neighbours[i] == want[i]);
    }
}

/// The D-NWRK-BROADCAST PDUs a cell configuration sends in turn.
inline std::vector<gen::NwrkBroadcast> sent_by(const gen::CellConfig& c)
{
    std::vector<gen::NwrkBroadcast> v;
    if (c.nwrk_broadcast)
        v.push_back(*c.nwrk_broadcast);
    v.insert(v.end(), c.nwrk_more.begin(), c.nwrk_more.end());
    return v;
}

}  // namespace dectest
