// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test-only field parsers for the broadcast PDUs of the TETRA test-signal
// generator (RAD-70), written from the EN 300 392-2 tables and separate
// from the encoders, for round-trip tests. Never linked into the product.

#pragma once

#include "yantra/gen/pdu.hpp"

namespace yantra::gen::ref {

/// Reads fields MSB first; throws std::out_of_range past the end.
class BitReader {
public:
    explicit BitReader(const Bits& b) : bits_(b) {}
    uint64_t get(int width);
    bool flag() { return get(1) != 0; }
    size_t pos() const { return pos_; }
    size_t left() const { return bits_.size() - pos_; }

private:
    const Bits& bits_;
    size_t pos_ = 0;
};

MacSync parse_sync(const Bits& type1_60);
MacSysinfo parse_sysinfo(const Bits& type1_124);
BsServiceDetails parse_bs_service_details(uint16_t bits12);
ExtendedServices parse_extended_services(uint32_t bits20);
/// Header 00 only; std::invalid_argument for any other header.
AccessAssign parse_access_assign(uint16_t bits14);

/// One MAC-RESOURCE PDU as seen by a receiver.
struct MacResource {
    bool fill_bits = false;
    bool grant_position = false;
    unsigned encryption_mode = 0;
    bool random_access = false;
    unsigned length_octets = 0;
    unsigned address_type = 0;  ///< 0 = Null PDU
    uint32_t ssi = 0;
    Bits tm_sdu;  ///< without fill bits
};

/// Splits a MAC block into its MAC-RESOURCE PDUs (PDU association,
/// 23.4.3.3): stops after a Null PDU or when fewer than 16 bits are left.
std::vector<MacResource> parse_mac_block(const Bits& block);

/// LLC BL-UDATA + MLE D-NWRK-BROADCAST; std::invalid_argument when the
/// LLC/MLE types are not those.
NwrkBroadcast parse_nwrk_broadcast_sdu(const Bits& tm_sdu);

}  // namespace yantra::gen::ref
