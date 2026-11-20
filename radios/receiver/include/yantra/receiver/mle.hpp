// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Upper-layer parsing of the downlink control channels through the Rust
// protocol crate (receiver/rust, C ABI yantra/proto.h; RAD-101, DEC-02,
// DEC-03, DEC-04, DEC-06): the MAC PDUs of a signalling block
// (MAC-RESOURCE, MAC-FRAG, MAC-END -> LLC with its FCS -> MLE),
// D-NWRK-BROADCAST with the cell re-select parameters, network time and
// neighbour cell information, channel allocations (counted for DEC-06),
// whole (reassembled) TM-SDUs, and the field-by-field decode text (for
// GSMTAP/debugging, DEC-04, RAD-115). The reassembly of fragments per
// timeslot is in fragment.hpp.
//
// Scope (RAD-17): no address ever reaches this side (only the address
// type); encrypted TM-SDUs are only flagged (DEC-10); MM PDUs are named
// (their non-address fields only in the field-by-field text); CMCE (calls,
// SDS) and SNDCP PDUs are only typed. Nothing here crosses the radio API
// (RAD-26): the plugins turn it into record fields.
// Copied from yantra 68ea20a: plugins/tetra/decoder/include/yantra/tetra/
// decoder/mle.h, adapted (namespace, ReselectParams and the frequency
// helpers of broadcast.hpp).
#pragma once

#include "yantra/receiver/broadcast.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yantra::receiver::tetra {

/// TETRA network time (18.5.24).
struct NetworkTimeInfo {
    uint32_t utc_time = 0;  ///< 2 s units since 1 January 00:00 UTC
    bool local_offset_negative = false;
    uint8_t local_offset = 0;  ///< 15 min units
    uint8_t year = 0;          ///< since 2000
    uint16_t reserved = 0;
    bool operator==(const NetworkTimeInfo&) const = default;
    /// "YYYY-MM-DD hh:mm:ss UTC+hh:mm".
    std::string text() const;
};

/// Main carrier number extension of a neighbour (18.5.12).
struct CarrierExtensionInfo {
    uint8_t frequency_band = 0;
    uint8_t offset = 0;
    uint8_t duplex_spacing = 0;
    bool reverse_operation = false;
    bool operator==(const CarrierExtensionInfo&) const = default;
};

/// Neighbour cell information for CA (18.5.17); optional elements are
/// empty when not sent.
struct NeighbourInfo {
    uint8_t cell_id = 0;
    uint8_t reselection_types = 0;
    bool synchronised = false;
    uint8_t cell_load = 0;
    uint16_t main_carrier = 0;
    std::optional<CarrierExtensionInfo> carrier_extension;
    std::optional<uint16_t> mcc;
    std::optional<uint16_t> mnc;
    std::optional<uint16_t> location_area;
    std::optional<uint8_t> max_ms_txpwr;
    std::optional<uint8_t> min_rx_access_level;
    std::optional<uint16_t> subscriber_class;
    std::optional<uint16_t> service_details;
    std::optional<uint8_t> timeshare_security;
    std::optional<uint8_t> tdma_frame_offset;
    bool operator==(const NeighbourInfo&) const = default;

    /// Downlink frequency; without a carrier extension the serving cell's
    /// band and offset apply.
    uint64_t downlink_hz(uint8_t serving_band, uint8_t serving_offset) const;
};

/// D-NWRK-BROADCAST (18.4.1.4.1).
struct NwrkBroadcastInfo {
    ReselectParams reselect;
    uint8_t cell_load = 0;  ///< 0 unknown, 1 low, 2 medium, 3 high
    std::optional<NetworkTimeInfo> network_time;
    bool neighbours_present = false;  ///< the number of neighbour cells was sent
    std::vector<NeighbourInfo> neighbours;
    bool operator==(const NwrkBroadcastInfo&) const = default;
};

/// Part of a fragmented TM-SDU a MAC PDU carries.
enum class Fragment : uint8_t { None = 0, Start = 1, Frag = 2, End = 3 };

/// The LLC frame check sequence of a TM-SDU.
enum class Fcs : uint8_t { None = 0, Ok = 1, Error = 2 };

/// Channel allocation element (21.5.2): where a channel moves to (DEC-06).
/// No address: only the carrier, timeslots and direction.
struct ChannelAllocationInfo {
    uint8_t allocation_type = 0;  ///< 0 replace, 1 additional, 2 quit and go, 3 replace + CSS
    uint8_t timeslots = 0;        ///< 4 bits, timeslot 1 in bit 3
    uint8_t up_downlink = 0;      ///< 1 downlink, 2 uplink, 3 both
    bool cell_change = false;
    uint16_t carrier = 0;
    std::optional<CarrierExtensionInfo> extension;
    bool operator==(const ChannelAllocationInfo&) const = default;
    /// "carrier 3612, timeslots 2,3, uplink and downlink".
    std::string text() const;
};

/// One downlink MAC PDU of a signalling block, or a whole TM-SDU
/// reassembled from fragments (`reassembled`).
struct BlockPdu {
    int offset = 0;            ///< bit offset in the block
    /// The block's last PDU: nothing but fill bits follow (or the PDU
    /// runs to the block's end). False when parsing stopped at an error.
    bool last = false;
    uint8_t mac_pdu_type = 0;  ///< 0 MAC-RESOURCE, 1 MAC-FRAG/END; others only typed
    bool null_pdu = false;
    bool encrypted = false;    ///< TM-SDU encrypted: never decoded (DEC-10)
    Fragment fragment = Fragment::None;
    bool first_fragment = false;  ///< fragment == Start
    bool reassembled = false;     ///< a TM-SDU put together from its fragments
    int fragments = 0;            ///< reassembled: number of fragments
    int sdu_start = 0;            ///< TM-SDU (part) bit range in the block
    int sdu_end = 0;
    Fcs fcs = Fcs::None;
    std::optional<ChannelAllocationInfo> channel_allocation;
    std::vector<uint8_t> sdu_bits;  ///< reassembled: the TM-SDU (for the field-by-field text)
    std::optional<int> length_octets;
    std::optional<uint8_t> address_type;  ///< type only, never the address
    bool broadcast = false;
    std::optional<uint8_t> llc_type;
    std::optional<uint8_t> pdisc;
    std::optional<uint8_t> pdu_type;
    int sdu_status = 0;  ///< YANTRA_PROTO_* status of the LLC/MLE decode
    std::optional<NwrkBroadcastInfo> nwrk;

    /// "BL-UDATA MLE D-NWRK-BROADCAST, 3 neighbours" and the like.
    std::string summary() const;
};

/// The MAC PDUs of a downlink signalling block (type-1 bits, one per
/// byte), in order; stops at the first one that cannot be parsed.
std::vector<BlockPdu> parse_block_pdus(const uint8_t* bits, int nbits);

/// Decodes a whole TM-SDU (reassembled): LLC with FCS, then MLE/MM.
BlockPdu parse_tm_sdu(const std::vector<uint8_t>& bits);

enum class DescribeKind : uint32_t { Sync = 1, Sysinfo = 2, DlPdu = 3, TmSdu = 4 };

/// Field-by-field decode text, one line per field: offset, width, name,
/// raw value ("-" when not shown), meaning, tab-separated; a last line
/// "error<TAB>..." when decoding stopped.
std::string describe(DescribeKind kind, const uint8_t* bits, int nbits, int offset = 0);

/// MAC-SYSINFO + D-MLE-SYSINFO parsed by the protocol crate (the 124
/// type-1 bits of a BNCH); empty when the block is not a SYSINFO PDU. The
/// lower MAC uses osmo-tetra's parse_sysinfo (broadcast.hpp); this second,
/// independent parser cross-checks it (tests) and the frequency helpers
/// below cross-check carrier_downlink_hz / duplex_spacing_hz. A cipher key
/// identifier sent in place of the hyperframe is never kept.
std::optional<SysinfoInfo> parse_sysinfo_proto(const uint8_t* bits);
/// Downlink frequency and duplex spacing computed by the protocol crate.
uint64_t carrier_downlink_hz_proto(uint8_t band, uint16_t carrier, uint8_t offset);
std::optional<uint64_t> duplex_spacing_hz_proto(uint8_t band, uint8_t duplex);

/// The protocol crate's C ABI matches the header this was built with.
bool proto_abi_matches();

/// Names of protocol values (shared with the plugins' records and texts).
std::string llc_type_name(uint8_t t);
std::string mle_pdu_name(uint8_t pdisc, std::optional<uint8_t> type);
std::string cell_load_name(uint8_t code);
/// Population classes allowed by a 16-bit class bitmap (class 1 in the
/// most significant bit): "none", "1-16", "1,3,5-7".
std::string class_list_text(uint16_t bitmap);
/// MS_TXPWR_MAX_CELL code to dBm (15 + 5 (n - 1)); 0 for the reserved 0.
inline int ms_txpwr_dbm(uint8_t code) { return code == 0 ? 0 : 15 + 5 * (code - 1); }
/// RXLEV_ACCESS_MIN code to dBm (-125 + 5 n).
inline int rxlev_access_min_dbm(uint8_t code) { return -125 + 5 * code; }

}  // namespace yantra::receiver::tetra
