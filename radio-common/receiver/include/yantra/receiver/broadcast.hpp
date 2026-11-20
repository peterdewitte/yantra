// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Broadcast PDUs the lower MAC decodes (RAD-101, DEC-02, DEC-03, DEC-05,
// DEC-10) as typed C++ structs: MAC-SYNC + D-MLE-SYNC (BSCH), MAC-SYSINFO
// + D-MLE-SYSINFO (BNCH, osmo-tetra's macpdu_decode_sysinfo),
// ACCESS-ASSIGN (AACH, osmo-tetra's macpdu_decode_access_assign), the MAC
// PDU type and encryption flag of every signalling block (MAC-RESOURCE
// header through osmo-tetra's macpdu_decode_resource), and the
// serving-cell parameter set of DEC-03 derived from them.
//
// Scope (RAD-17): addresses are never copied out of osmo-tetra's
// structures, only whether a PDU is addressed; nothing is decrypted,
// encrypted PDUs are only flagged and counted (DEC-10). These structs stay
// inside the radio parts: the plugins turn them into record fields, no PDU
// crosses the radio API (RAD-26).
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "yantra/receiver/tdma.hpp"

namespace yantra::receiver::tetra {

/// MAC-SYNC + D-MLE-SYNC (EN 300 392-2 21.4.4.2, 18.4.2.1), 60 bits.
struct SyncInfo {
    uint8_t system_code = 0;
    uint8_t colour_code = 0;
    uint8_t timeslot = 1;    ///< 1..4
    uint8_t frame = 1;       ///< 1..18
    uint8_t multiframe = 1;  ///< 1..60
    uint8_t sharing_mode = 0;
    uint8_t ts_reserved_frames = 0;
    bool u_plane_dtx = false;
    bool frame18_extension = false;
    uint16_t mcc = 0;
    uint16_t mnc = 0;
    uint8_t neighbour_broadcast = 0;  ///< 2 bits: broadcast supported, enquiry supported
    uint8_t cell_service_level = 0;
    bool late_entry = false;

    bool operator==(const SyncInfo&) const = default;
    /// Same network and cell (MCC, MNC, colour code), ignoring the time.
    bool same_cell(const SyncInfo& o) const
    {
        return mcc == o.mcc && mnc == o.mnc && colour_code == o.colour_code;
    }
};

/// Parses the 60 type-1 bits of a BSCH block (one bit per byte). Empty
/// when frame or multiframe number are out of range.
std::optional<SyncInfo> parse_sync(const uint8_t* bits);

/// MAC-SYSINFO + D-MLE-SYSINFO (21.4.4.1, 18.4.2.2), 124 bits.
struct SysinfoInfo {
    uint16_t main_carrier = 0;
    uint8_t frequency_band = 0;
    uint8_t offset = 0;
    uint8_t duplex_spacing = 0;
    bool reverse_operation = false;
    uint8_t common_scch = 0;  ///< number of common secondary control channels
    uint8_t ms_txpwr_max_cell = 0;
    uint8_t rxlev_access_min = 0;
    uint8_t access_parameter = 0;
    uint8_t radio_downlink_timeout = 0;
    bool hyperframe_present = false;  ///< false: a cipher key identifier was sent instead (never kept)
    uint16_t hyperframe = 0;
    uint8_t option = 0;          ///< optional field flag (0..3)
    uint32_t option_value = 0;   ///< its 20 bits
    uint16_t location_area = 0;
    uint16_t subscriber_class = 0;  ///< population classes 1..16 (bit 15 = class 1)
    uint16_t service_details = 0;   ///< BS service details, 12 bits (first flag in bit 11)

    bool operator==(const SysinfoInfo&) const = default;

    // Derived values (EN 300 392-2 21.4.4.1, 18.5.2).
    int ms_txpwr_max_dbm() const;     ///< 15 + 5 (n - 1) dBm; 0 when reserved (n = 0)
    int rxlev_access_min_dbm() const; ///< -125 + 5 n dBm
    int access_parameter_dbm() const; ///< -53 + 2 n dBm
    uint64_t downlink_hz() const;     ///< main carrier frequency (downlink)
    int offset_hz() const;            ///< carrier offset (0, +6250, -6250, +12500 Hz)
    /// Duplex spacing (TS 100 392-15 table 2); empty when reserved.
    std::optional<uint64_t> duplex_spacing_hz() const;
    /// Uplink frequency: downlink -/+ duplex spacing (+ with reverse
    /// operation, "RX above TX"); empty when the spacing is reserved.
    std::optional<uint64_t> uplink_hz() const;
    /// BS service details flags (18.5.2), in transmission order.
    bool registration() const { return (service_details & 0x800) != 0; }
    bool deregistration() const { return (service_details & 0x400) != 0; }
    bool priority_cell() const { return (service_details & 0x200) != 0; }
    bool no_minimum_mode() const { return (service_details & 0x100) != 0; }
    bool migration() const { return (service_details & 0x080) != 0; }
    /// System-wide services: wide-area trunking, else local site trunking
    /// (DEC-03).
    bool system_wide_services() const { return (service_details & 0x040) != 0; }
    bool voice_service() const { return (service_details & 0x020) != 0; }
    bool circuit_mode_data() const { return (service_details & 0x010) != 0; }
    bool sndcp_service() const { return (service_details & 0x004) != 0; }
    bool air_encryption() const { return (service_details & 0x002) != 0; }
    bool advanced_link() const { return (service_details & 0x001) != 0; }
    /// The security information octet of an extended services broadcast
    /// (option 3), else empty.
    std::optional<uint8_t> security_information() const;
    /// Security class 1..3 (DEC-10): 1 without air-interface encryption,
    /// else 3 or 2 after the "security class 2 or 3" bit of the security
    /// information (2 when it is not broadcast).
    int security_class() const;
};

/// Parses the 124 type-1 bits of a BNCH block with osmo-tetra's
/// macpdu_decode_sysinfo; empty when the block is not a MAC broadcast
/// SYSINFO PDU. A cipher key identifier sent in place of the hyperframe
/// number is never kept (hyperframe 0, hyperframe_present false).
std::optional<SysinfoInfo> parse_sysinfo(const uint8_t* bits);

/// Downlink carrier frequency (band x 100 MHz + carrier x 25 kHz +
/// offset), as osmo-tetra's tetra_dl_carrier_hz; 0 instead of a
/// negative value (band 0, carrier 0, offset -6.25 kHz).
uint64_t carrier_downlink_hz(uint8_t band, uint16_t carrier, uint8_t offset);
/// Duplex spacing of TS 100 392-15 table 2; empty when reserved.
std::optional<uint64_t> duplex_spacing_hz(uint8_t band, uint8_t duplex_spacing);

/// ACCESS-ASSIGN (21.4.7), the 14 bits of the AACH (DEC-05).
struct AccessAssignInfo {
    uint8_t header = 0;
    uint8_t field1 = 0;  ///< 6 bits as sent
    uint8_t field2 = 0;
    /// Downlink usage marker (0 unallocated, 1 assigned control, 2 common
    /// control, 3 reserved, >= 4 traffic), when the header carries one.
    std::optional<uint8_t> dl_usage;
    std::optional<uint8_t> ul_usage;
    bool access1 = false;  ///< field1 is an access field (access code 2 bits, base frame length 4)
    bool access2 = false;  ///< field2 is an access field

    bool operator==(const AccessAssignInfo&) const = default;
    bool traffic() const { return dl_usage && *dl_usage >= 4; }
};

/// Parses ACCESS-ASSIGN; `frame18` selects the frame-18 header meanings.
AccessAssignInfo parse_access_assign(const uint8_t* bits14, bool frame18);

/// MAC PDU kinds of a signalling block (never renumber).
enum class MacPduKind : uint8_t {
    Unknown = 0,
    MacSync = 1,
    MacSysinfo = 2,
    AccessDefine = 3,
    MacResource = 4,
    NullPdu = 5,
    MacFrag = 6,
    MacEnd = 7,
    MacDBlck = 8,
};
inline constexpr int kMacPduKindCount = 9;

std::string_view pdu_kind_name(MacPduKind k);

/// The MAC header of a downlink signalling block, without its address.
struct MacPduInfo {
    MacPduKind kind = MacPduKind::Unknown;
    bool encrypted = false;            ///< encryption mode != 0 (DEC-10): only flagged
    std::optional<int> length_octets;  ///< MAC-RESOURCE length indication
    bool addressed = false;            ///< carries an address (not the broadcast address)
};

/// Classifies the first MAC PDU of a decoded block of `nbits` type-1
/// bits on `channel`.
MacPduInfo parse_mac_pdu(const uint8_t* bits, int nbits, LogicalChannel channel);

/// Trunking mode of DEC-03 (from the system-wide services flag).
enum class TrunkingMode : uint8_t {
    LocalSite = 0,  ///< local site trunking (system-wide services not available)
    WideArea = 1,   ///< wide-area (system-wide services available)
};

/// Cell re-selection parameters of D-NWRK-BROADCAST (18.5.4), 4-bit codes
/// (2 dB steps), as parsed by the MLE layer (mle.hpp).
struct ReselectParams {
    uint8_t slow_threshold = 0;   ///< SRT
    uint8_t slow_hysteresis = 0;  ///< SRH
    uint8_t fast_threshold = 0;   ///< FRT
    uint8_t fast_hysteresis = 0;  ///< FRH
    bool operator==(const ReselectParams&) const = default;
    /// A 4-bit code in dB (2 dB steps).
    static int db(uint8_t code) { return 2 * code; }
};

/// The serving-cell parameter set of DEC-03 (with the encryption state of
/// DEC-10), from the cell's SYNC and SYSINFO plus the counts the carrier
/// decoder keeps. Values as decoded; the derived physical values next to
/// them.
struct ServingCellParams {
    // Identity (SYNC).
    uint16_t mcc = 0;
    uint16_t mnc = 0;
    uint8_t colour_code = 0;
    uint8_t system_code = 0;
    uint16_t location_area = 0;  ///< shown as hex and decimal by the host
    // Carrier (SYSINFO).
    uint16_t main_carrier = 0;
    uint8_t frequency_band = 0;
    uint8_t offset = 0;
    int offset_hz = 0;
    uint64_t downlink_hz = 0;
    uint8_t duplex_spacing = 0;
    std::optional<uint64_t> duplex_spacing_hz;
    bool reverse_operation = false;  ///< RX above TX
    std::optional<uint64_t> uplink_hz;
    /// MCCH: timeslot 1 of the main carrier (EN 300 392-2 9.5.2).
    uint16_t mcch_carrier = 0;
    uint64_t mcch_hz = 0;
    uint8_t common_scch = 0;
    // Access and power.
    uint8_t ms_txpwr_max_cell = 0;
    int ms_txpwr_max_dbm = 0;
    uint8_t rxlev_access_min = 0;
    int rxlev_access_min_dbm = 0;
    uint8_t access_parameter = 0;
    int access_parameter_dbm = 0;
    uint8_t radio_downlink_timeout = 0;
    // Services and classes.
    TrunkingMode trunking = TrunkingMode::LocalSite;
    uint16_t service_details = 0;
    uint16_t subscriber_class = 0;
    // Encryption state (DEC-10).
    bool air_encryption = false;
    int security_class = 1;
    std::optional<uint8_t> security_information;
    uint64_t encrypted_blocks = 0;  ///< signalling blocks sent encrypted since the cell was found
    // Cell load from the AACH downlink usage markers of the last period
    // (DEC-05): share of slots with a usage marker that are traffic or
    // assigned control, in percent; empty before a period with markers.
    std::optional<double> cell_load_percent;
    /// FRT/FRH/SRT/SRH from the cell's latest D-NWRK-BROADCAST; empty
    /// until one was received (filled by CarrierDecoder).
    std::optional<ReselectParams> reselect;
    std::optional<uint16_t> hyperframe;  ///< last broadcast hyperframe

    bool operator==(const ServingCellParams&) const = default;
};

/// The DEC-03 set from a cell's SYNC and SYSINFO (counts and load empty).
ServingCellParams serving_cell_params(const SyncInfo& sync, const SysinfoInfo& sysinfo);

}  // namespace yantra::receiver::tetra
