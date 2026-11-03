// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Broadcast PDUs of the TETRA test-signal generator (RAD-70), encoded field
// by field after EN 300 392-2 (clause 21 for the MAC, 18 for the MLE):
// MAC-SYNC + D-MLE-SYNC (BSCH), MAC-SYSINFO + D-MLE-SYSINFO (BNCH),
// ACCESS-ASSIGN (AACH, header 00), MAC-RESOURCE carrying LLC BL-UDATA with
// D-NWRK-BROADCAST and its neighbour cell information (SCH/F), and the MAC
// Null PDU. Scope (RAD-17, Yantra 16.5): broadcast and system information only; the
// only address used is the broadcast address.
//
// Every numeric member is the value sent in the field (the spec's coding),
// unless the comment says otherwise; the encoders refuse values that do
// not fit their field (std::out_of_range naming the field).

#pragma once

#include "bits.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace yantra::gen {

// ---------------------------------------------------------------- BSCH --

/// D-MLE-SYNC (18.4.2.1), 29 bits.
struct MleSync {
    uint16_t mcc = 901;  ///< 10 bits; 901 is a fictitious test network
    uint16_t mnc = 1;    ///< 14 bits
    /// Neighbour cell broadcast (2 bits): first bit D-NWRK-BROADCAST
    /// broadcast supported, second bit enquiry supported.
    uint8_t neighbour_cell_broadcast = 0b10;
    uint8_t cell_service_level = 0;  ///< 2 bits (0 = unknown)
    bool late_entry_supported = true;

    bool operator==(const MleSync&) const = default;
};

/// MAC-SYNC (21.4.4.2), 31 bits, followed by D-MLE-SYNC: 60 bits for the
/// BSCH.
struct MacSync {
    uint8_t system_code = 0;  ///< 4 bits
    uint8_t colour_code = 1;  ///< 6 bits
    uint8_t timeslot = 1;     ///< 1..4 (sent as TN - 1 in 2 bits)
    uint8_t frame = 18;       ///< 1..18 (5 bits)
    uint8_t multiframe = 1;   ///< 1..60 (6 bits)
    uint8_t sharing_mode = 0;        ///< 2 bits (0 = continuous transmission)
    uint8_t ts_reserved_frames = 0;  ///< 3 bits
    bool u_plane_dtx = false;
    bool frame18_extension = false;
    MleSync mle;

    bool operator==(const MacSync&) const = default;
};

/// MAC-SYNC + D-MLE-SYNC as the 60 type-1 bits of the BSCH (the 1-bit
/// reserved field is sent as 0).
Bits encode_sync(const MacSync& p);

// ---------------------------------------------------------------- BNCH --

/// BS service details (18.5.2), 12 flags in transmission order.
struct BsServiceDetails {
    bool registration = true;
    bool deregistration = true;
    bool priority_cell = false;
    bool no_minimum_mode = false;  ///< "minimum mode service": 1 = never minimum mode
    bool migration = false;
    bool system_wide_services = true;
    bool voice_service = false;  ///< advertised flag only, no voice is generated
    bool circuit_mode_data = false;
    bool reserved = false;
    bool sndcp_service = false;
    bool air_interface_encryption = false;  ///< encryption flag (RAD-70)
    bool advanced_link = false;

    bool operator==(const BsServiceDetails&) const = default;
    /// The 12 bits, first flag in the most significant bit.
    uint16_t pack() const;
};

/// D-MLE-SYSINFO (18.4.2.2), 42 bits.
struct MleSysinfo {
    uint16_t location_area = 1;          ///< 14 bits
    uint16_t subscriber_class = 0xFFFF;  ///< 16 bits
    BsServiceDetails bs_service_details;

    bool operator==(const MleSysinfo&) const = default;
};

/// Meaning of the 20-bit option value of MAC-SYSINFO (2-bit optional field
/// flag).
enum class SysinfoOption : uint8_t {
    EvenMultiframe = 0,     ///< TS_COMMON_FRAMES for even multiframes
    OddMultiframe = 1,      ///< TS_COMMON_FRAMES for odd multiframes
    DefaultAccessCodeA = 2, ///< default definition for access code A
    ExtendedServices = 3,   ///< extended services broadcast
};

/// Extended services broadcast (option 3 of MAC-SYSINFO), 20 bits.
struct ExtendedServices {
    /// Security information (8 bits): authentication required, security
    /// class 1, class 2 or 3, then the class-dependent bits; a cell flag
    /// (RAD-70 "encryption flags"), not key material. Default: security
    /// class 1 only (clear).
    uint8_t security_information = 0b01000000;
    uint8_t sds_tl_addressing_method = 0;  ///< 2 bits (a capability flag only)
    bool gck_supported = false;
    uint8_t section = 0;               ///< 2 bits
    uint8_t section_information = 0;   ///< 7 bits

    bool operator==(const ExtendedServices&) const = default;
    uint32_t pack() const;
};

/// MAC-SYSINFO (21.4.4.1), 82 bits, followed by D-MLE-SYSINFO: 124 bits
/// for the BNCH. Starts with MAC PDU type 10 (broadcast) and broadcast
/// type 00 (SYSINFO).
struct MacSysinfo {
    uint16_t main_carrier = 0;   ///< 12 bits
    uint8_t frequency_band = 3;  ///< 4 bits (3 = 300 MHz band, e.g. 390 MHz)
    uint8_t offset = 0;          ///< 2 bits (0 = no offset, 1 = +6.25, 2 = -6.25, 3 = +12.5 kHz)
    uint8_t duplex_spacing = 0;  ///< 3 bits
    bool reverse_operation = false;
    uint8_t num_common_secondary_cch = 0;  ///< 2 bits
    uint8_t ms_txpwr_max_cell = 5;         ///< 3 bits
    uint8_t rxlev_access_min = 2;          ///< 4 bits
    uint8_t access_parameter = 7;          ///< 4 bits
    uint8_t radio_downlink_timeout = 3;    ///< 4 bits
    bool cck_flag = false;  ///< false: next field is the hyperframe number; true: CCK id
    uint16_t hyperframe_or_cck = 0;  ///< 16 bits
    SysinfoOption option = SysinfoOption::ExtendedServices;
    uint32_t option_value = 0;  ///< 20 bits (ExtendedServices::pack() for option 3)
    MleSysinfo mle;

    bool operator==(const MacSysinfo&) const = default;
};

/// MAC-SYSINFO + D-MLE-SYSINFO as the 124 type-1 bits of the BNCH.
Bits encode_sysinfo(const MacSysinfo& p);

// ---------------------------------------------------------------- AACH --

/// One access field (21.4.7.2), 6 bits.
struct AccessField {
    uint8_t access_code = 0;        ///< 2 bits (A..D)
    uint8_t base_frame_length = 0;  ///< 4 bits (0 = reserved subslot)

    bool operator==(const AccessField&) const = default;
};

/// ACCESS-ASSIGN (21.4.7.2) of frames 1..17. Header 00: two access
/// fields; 01 and 10: downlink usage marker (field1 unused) and access
/// field2; 11: downlink and uplink usage markers (no access field).
/// Usage markers: 0 unallocated, 1 assigned control, 2 common control,
/// 3 reserved, 4..63 traffic.
struct AccessAssign {
    AccessField field1;
    AccessField field2;
    uint8_t header = 0;    ///< 2 bits
    uint8_t dl_usage = 0;  ///< 6 bits, headers 01, 10, 11
    uint8_t ul_usage = 0;  ///< 6 bits, header 11

    bool operator==(const AccessAssign&) const = default;
};

/// The 14 bits of ACCESS-ASSIGN, for encode_aach().
uint16_t encode_access_assign(const AccessAssign& p);

// ------------------------------------------------------------ MAC blocks --

/// MAC-RESOURCE (21.4.3.1) header size with the broadcast address and no
/// power control, slot granting or channel allocation: 43 bits.
inline constexpr int kMacResourceHeaderBits = 43;
/// The broadcast SSI (all ones, 24 bits): no subscriber address is sent.
inline constexpr uint32_t kBroadcastSsi = 0xFFFFFF;
/// MAC Null PDU: a MAC-RESOURCE with address type 000, 16 bits.
inline constexpr int kNullPduBits = 16;

/// The MAC Null PDU (length 2 octets, no fill bits).
Bits encode_null_pdu();

/// A MAC block of `block_bits` (124 for SCH/HD, 268 for SCH/F) holding
/// only a Null PDU, the rest set to 0.
Bits null_block(int block_bits);

/// The largest TM-SDU (bits) that fits one MAC-RESOURCE in a block of
/// `block_bits`: the length indication counts whole octets, at most 34.
int max_tm_sdu_bits(int block_bits);

/// A MAC block of `block_bits`: MAC-RESOURCE to the broadcast address with
/// `tm_sdu`, fill bits (a 1, then 0s) up to the next octet boundary with
/// the fill bit indication set when needed; if 16 bits or more are left,
/// a Null PDU follows; the rest is 0. std::length_error when the SDU does
/// not fit (no fragmentation).
Bits mac_resource_block(const Bits& tm_sdu, int block_bits);

/// Channel allocation element (21.5.2) without extended carrier numbering
/// (DEC-06 tests): where a channel moves to, no address.
struct ChannelAllocation {
    uint8_t allocation_type = 0;     ///< 2 bits
    uint8_t timeslots = 0b1000;      ///< 4 bits, timeslot 1 in bit 3
    uint8_t up_downlink = 3;         ///< 1 downlink, 2 uplink, 3 both (0 not allowed)
    bool cell_change = false;
    uint16_t carrier = 0;            ///< 12 bits
    uint8_t monitoring_pattern = 1;  ///< 2 bits; 0 adds a frame-18 pattern 00
};

Bits encode_channel_allocation(const ChannelAllocation& c);

/// Options of a MAC-RESOURCE (still to the broadcast address).
struct ResourceOptions {
    uint8_t encryption_mode = 0;  ///< 2 bits; != 0: the TM-SDU counts as encrypted
};

/// mac_resource_block with options.
Bits mac_resource_block(const Bits& tm_sdu, int block_bits, const ResourceOptions& o);

/// A TM-SDU too long for one block, fragmented (21.4.3): a MAC-RESOURCE
/// "start of fragmentation" filling the first block, MAC-FRAG blocks and
/// a MAC-END (with `end_allocation` when given, then a Null PDU when 16
/// bits are left). One block of `block_bits` each, in sending order.
/// std::invalid_argument when the SDU fits one block.
std::vector<Bits> fragment_blocks(const Bits& tm_sdu, int block_bits, const ResourceOptions& o = {},
                                  const std::optional<ChannelAllocation>& end_allocation = std::nullopt);

// ------------------------------------------------- LLC + MLE broadcast --

/// LLC BL-UDATA without FCS (PDU type 0010, 21.2.1) around an MLE PDU.
Bits llc_bl_udata(const Bits& mle_pdu);

/// The LLC frame check sequence (CRC-32, 22.2.3.4) of `tl_sdu`.
uint32_t llc_fcs(const Bits& tl_sdu);

/// LLC BL-UDATA with FCS (PDU type 0110); `corrupt_fcs` sends it wrong.
Bits llc_bl_udata_fcs(const Bits& mle_pdu, bool corrupt_fcs = false);

/// Cell re-select parameters (18.5.4), 4 x 4 bits.
struct CellReselectParameters {
    uint8_t slow_threshold = 2;
    uint8_t slow_hysteresis = 2;
    uint8_t fast_threshold = 4;
    uint8_t fast_hysteresis = 2;

    bool operator==(const CellReselectParameters&) const = default;
};

/// TETRA network time (18.5.24), 48 bits.
struct NetworkTime {
    uint32_t utc_time = 0;  ///< 24 bits, 2 s units since the start of the year
    bool local_offset_negative = false;
    uint8_t local_offset = 0;  ///< 6 bits, 15 min units
    uint8_t year = 26;         ///< 6 bits, years since 2000
    uint16_t reserved = 0x7FF; ///< 11 bits reserved, sent as given

    bool operator==(const NetworkTime&) const = default;
};

/// Main carrier number extension (18.5.12), 10 bits.
struct CarrierExtension {
    uint8_t frequency_band = 3;  ///< 4 bits
    uint8_t offset = 0;          ///< 2 bits
    uint8_t duplex_spacing = 0;  ///< 3 bits
    bool reverse_operation = false;

    bool operator==(const CarrierExtension&) const = default;
};

/// Neighbour cell information (18.5.17). The optional elements are sent
/// behind an o-bit and one p-bit each, in this order.
struct NeighbourCell {
    uint8_t cell_id = 1;             ///< 5 bits
    uint8_t reselection_types = 1;   ///< 2 bits
    bool synchronised = true;
    uint8_t cell_load = 0;           ///< 2 bits
    uint16_t main_carrier = 0;       ///< 12 bits
    std::optional<CarrierExtension> carrier_extension;
    std::optional<uint16_t> mcc;                ///< 10 bits
    std::optional<uint16_t> mnc;                ///< 14 bits
    std::optional<uint16_t> location_area;      ///< 14 bits
    std::optional<uint8_t> max_ms_txpwr;        ///< 3 bits
    std::optional<uint8_t> min_rx_access_level; ///< 4 bits
    std::optional<uint16_t> subscriber_class;   ///< 16 bits
    std::optional<BsServiceDetails> bs_service_details;
    std::optional<uint8_t> timeshare_security;  ///< 5 bits (timeshare cell and AI encryption)
    std::optional<uint8_t> tdma_frame_offset;   ///< 6 bits

    bool operator==(const NeighbourCell&) const = default;
};

/// Bits used by one neighbour cell information element.
int neighbour_cell_bits(const NeighbourCell& n);

/// D-NWRK-BROADCAST (18.4.1.4.1).
struct NwrkBroadcast {
    CellReselectParameters reselect;
    uint8_t cell_load = 0;  ///< 2 bits
    std::optional<NetworkTime> network_time;
    /// Number of neighbour cells (3 bits, 0..7) and their information;
    /// nullopt leaves the element out.
    std::optional<std::vector<NeighbourCell>> neighbours;

    bool operator==(const NwrkBroadcast&) const = default;
};

/// The MLE PDU: protocol discriminator 101 (MLE), PDU type 010, elements.
Bits encode_nwrk_broadcast(const NwrkBroadcast& p);

/// D-NWRK-BROADCAST in LLC BL-UDATA in MAC-RESOURCE: one SCH/F block (268
/// type-1 bits). std::length_error when it does not fit one block.
Bits nwrk_broadcast_block(const NwrkBroadcast& p);

/// Splits a neighbour list of any length (cell identifiers are 5 bits, so
/// at most 32) into D-NWRK-BROADCAST PDUs of at most 7 neighbours that each
/// fit one SCH/F block, in list order (one PDU when the whole list fits);
/// every PDU repeats the re-select
/// parameters, cell load and network time. std::length_error when one
/// neighbour alone does not fit.
std::vector<NwrkBroadcast> split_nwrk_broadcast(const NwrkBroadcast& all);

}  // namespace yantra::gen
