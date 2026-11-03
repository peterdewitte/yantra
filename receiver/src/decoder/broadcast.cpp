// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/pdu.cpp (SYNC,
// SYSINFO through osmo-tetra, ACCESS-ASSIGN, MAC PDU classification),
// adapted for RAD-101, DEC-02, DEC-03, DEC-10 (SYSINFO through osmo-tetra
// here; the protocol crate of step 4.3 adds its own parser as a
// cross-check).

#include "yantra/receiver/broadcast.hpp"

#include "osmo.hpp"

#include <cstring>

namespace yantra::receiver::tetra {
namespace {

uint32_t field(const uint8_t* bits, int& at, int n)
{
    const uint32_t v = bits_to_uint(bits + at, static_cast<unsigned>(n));
    at += n;
    return v;
}

// TS 100 392-15 table 2 in kHz, [duplex spacing field][frequency band];
// -1 = reserved. The same values as osmo-tetra's tetra_common.c, whose
// int16_t table cannot hold 36000 and 45000 (they wrap), hence our own.
constexpr int32_t kDuplexSpacingKhz[8][16] = {
    {-1, 1600, 10000, 10000, 10000, 10000, 10000, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, 4500, -1, 36000, 7000, -1, -1, -1, 45000, 45000, -1, -1, -1, -1, -1, -1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {-1, -1, -1, 8000, 8000, -1, -1, -1, 18000, 18000, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, 18000, 5000, -1, 30000, 30000, -1, 39000, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, 9500, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

constexpr int kCarrierOffsetHz[4] = {0, 6250, -6250, 12500};

}  // namespace

std::optional<SyncInfo> parse_sync(const uint8_t* bits)
{
    // osmo-tetra decodes these fields inline in tetra_lower_mac.c (not
    // linked); the layout is EN 300 392-2 table 21.73 and 18.4.2.1.
    SyncInfo s;
    int at = 0;
    s.system_code = static_cast<uint8_t>(field(bits, at, 4));
    s.colour_code = static_cast<uint8_t>(field(bits, at, 6));
    s.timeslot = static_cast<uint8_t>(field(bits, at, 2) + 1);
    s.frame = static_cast<uint8_t>(field(bits, at, 5));
    s.multiframe = static_cast<uint8_t>(field(bits, at, 6));
    s.sharing_mode = static_cast<uint8_t>(field(bits, at, 2));
    s.ts_reserved_frames = static_cast<uint8_t>(field(bits, at, 3));
    s.u_plane_dtx = field(bits, at, 1) != 0;
    s.frame18_extension = field(bits, at, 1) != 0;
    at += 1;  // reserved
    s.mcc = static_cast<uint16_t>(field(bits, at, 10));
    s.mnc = static_cast<uint16_t>(field(bits, at, 14));
    s.neighbour_broadcast = static_cast<uint8_t>(field(bits, at, 2));
    s.cell_service_level = static_cast<uint8_t>(field(bits, at, 2));
    s.late_entry = field(bits, at, 1) != 0;
    if (s.frame < 1 || s.frame > 18 || s.multiframe < 1 || s.multiframe > 60)
        return std::nullopt;
    return s;
}

std::optional<SysinfoInfo> parse_sysinfo(const uint8_t* bits)
{
    // MAC PDU type 10 (broadcast), broadcast type 00 (SYSINFO).
    if (bits_to_uint(bits, 2) != TETRA_PDU_T_BROADCAST || bits_to_uint(bits + 2, 2) != TETRA_MAC_BC_SYSINFO)
        return std::nullopt;
    tetra_si_decoded si;
    std::memset(&si, 0, sizeof si);
    macpdu_decode_sysinfo(&si, bits);
    SysinfoInfo s;
    s.main_carrier = si.main_carrier;
    s.frequency_band = si.freq_band;
    s.offset = si.freq_offset;
    s.duplex_spacing = si.duplex_spacing;
    s.reverse_operation = si.reverse_operation != 0;
    s.common_scch = si.num_of_csch;
    s.ms_txpwr_max_cell = si.ms_txpwr_max_cell;
    s.rxlev_access_min = si.rxlev_access_min;
    s.access_parameter = si.access_parameter;
    s.radio_downlink_timeout = si.radio_dl_timeout;
    s.hyperframe_present = si.cck_valid_no_hf == 0;
    s.hyperframe = s.hyperframe_present ? si.hyperframe_number : 0;  // never keep a key id
    s.option = si.option_field;
    s.option_value = si.frame_bitmap & 0xFFFFFu;  // union: the 20-bit value
    s.location_area = si.mle_si.la;
    s.subscriber_class = si.mle_si.subscr_class;
    s.service_details = si.mle_si.bs_service_details;
    std::memset(&si, 0, sizeof si);
    return s;
}

uint64_t carrier_downlink_hz(uint8_t band, uint16_t carrier, uint8_t offset)
{
    return static_cast<uint64_t>(band) * 100000000u + static_cast<uint64_t>(carrier) * 25000u +
           static_cast<uint64_t>(static_cast<int64_t>(kCarrierOffsetHz[offset & 3]));
}

std::optional<uint64_t> duplex_spacing_hz(uint8_t band, uint8_t duplex_spacing)
{
    const int32_t khz = kDuplexSpacingKhz[duplex_spacing & 7][band & 15];
    if (khz < 0)
        return std::nullopt;
    return static_cast<uint64_t>(khz) * 1000u;
}

int SysinfoInfo::ms_txpwr_max_dbm() const
{
    return ms_txpwr_max_cell == 0 ? 0 : 15 + 5 * (ms_txpwr_max_cell - 1);
}

int SysinfoInfo::rxlev_access_min_dbm() const { return -125 + 5 * rxlev_access_min; }

int SysinfoInfo::access_parameter_dbm() const { return -53 + 2 * access_parameter; }

uint64_t SysinfoInfo::downlink_hz() const { return carrier_downlink_hz(frequency_band, main_carrier, offset); }

int SysinfoInfo::offset_hz() const { return kCarrierOffsetHz[offset & 3]; }

std::optional<uint64_t> SysinfoInfo::duplex_spacing_hz() const
{
    return tetra::duplex_spacing_hz(frequency_band, duplex_spacing);
}

std::optional<uint64_t> SysinfoInfo::uplink_hz() const
{
    const auto spacing = duplex_spacing_hz();
    if (!spacing)
        return std::nullopt;
    const uint64_t dl = downlink_hz();
    if (reverse_operation)
        return dl + *spacing;
    if (dl < *spacing)
        return std::nullopt;
    return dl - *spacing;
}

std::optional<uint8_t> SysinfoInfo::security_information() const
{
    if (option != TETRA_MAC_OPT_FIELD_EXT_SERVICES)
        return std::nullopt;
    return static_cast<uint8_t>((option_value >> 12) & 0xFF);
}

int SysinfoInfo::security_class() const
{
    if (!air_encryption())
        return 1;
    const auto info = security_information();
    return info && (*info & 0x20) != 0 ? 3 : 2;
}

AccessAssignInfo parse_access_assign(const uint8_t* bits14, bool frame18)
{
    tetra_acc_ass_decoded aad;
    std::memset(&aad, 0, sizeof aad);
    macpdu_decode_access_assign(&aad, bits14, frame18 ? 1 : 0);
    AccessAssignInfo a;
    a.header = aad.hdr;
    a.field1 = static_cast<uint8_t>(bits_to_uint(bits14 + 2, 6));
    a.field2 = static_cast<uint8_t>(bits_to_uint(bits14 + 8, 6));
    // osmo-tetra stores traffic usage markers (>= 4) in its enum fields,
    // outside the enumerators: read them as plain integers (UBSan).
    const auto raw = [](const auto& e) {
        static_assert(sizeof e == sizeof(unsigned));
        unsigned v = 0;
        std::memcpy(&v, &e, sizeof v);
        return static_cast<uint8_t>(v);
    };
    if (aad.pres & TETRA_ACC_ASS_PRES_DL_USAGE)
        a.dl_usage = raw(aad.dl_usage);
    if (aad.pres & TETRA_ACC_ASS_PRES_UL_USAGE)
        a.ul_usage = raw(aad.ul_usage);
    a.access1 = (aad.pres & TETRA_ACC_ASS_PRES_ACCESS1) != 0;
    a.access2 = (aad.pres & TETRA_ACC_ASS_PRES_ACCESS2) != 0;
    return a;
}

MacPduInfo parse_mac_pdu(const uint8_t* bits, int nbits, LogicalChannel channel)
{
    MacPduInfo m;
    if (channel == LogicalChannel::Bsch) {
        m.kind = MacPduKind::MacSync;
        return m;
    }
    if (nbits < 16)
        return m;
    switch (bits_to_uint(bits, 2)) {
    case TETRA_PDU_T_MAC_RESOURCE: {
        tetra_resrc_decoded rsd;
        std::memset(&rsd, 0, sizeof rsd);
        macpdu_decode_resource(&rsd, bits, 0);
        m.encrypted = rsd.encryption_mode != 0;
        if (rsd.macpdu_length > 0)
            m.length_octets = rsd.macpdu_length;
        if (rsd.addr.type == ADDR_TYPE_NULL) {
            m.kind = MacPduKind::NullPdu;
        } else {
            m.kind = MacPduKind::MacResource;
            // Only whether it is addressed leaves this function, never the
            // address itself (RAD-17).
            m.addressed = !(rsd.addr.type == ADDR_TYPE_SSI && rsd.addr.ssi == 0xFFFFFFu);
        }
        std::memset(&rsd, 0, sizeof rsd);
        break;
    }
    case TETRA_PDU_T_MAC_FRAG_END:
        m.kind = bits[2] ? MacPduKind::MacEnd : MacPduKind::MacFrag;
        break;
    case TETRA_PDU_T_BROADCAST:
        switch (bits_to_uint(bits + 2, 2)) {
        case TETRA_MAC_BC_SYSINFO: m.kind = MacPduKind::MacSysinfo; break;
        case TETRA_MAC_BC_ACCESS_DEFINE: m.kind = MacPduKind::AccessDefine; break;
        default: break;
        }
        break;
    case TETRA_PDU_T_MAC_SUPPL:
        if (bits[2] == TETRA_MAC_SUPP_D_BLCK)
            m.kind = MacPduKind::MacDBlck;
        break;
    default: break;
    }
    return m;
}

std::string_view pdu_kind_name(MacPduKind k)
{
    switch (k) {
    case MacPduKind::MacSync: return "MAC-SYNC";
    case MacPduKind::MacSysinfo: return "MAC-SYSINFO";
    case MacPduKind::AccessDefine: return "ACCESS-DEFINE";
    case MacPduKind::MacResource: return "MAC-RESOURCE";
    case MacPduKind::NullPdu: return "Null PDU";
    case MacPduKind::MacFrag: return "MAC-FRAG";
    case MacPduKind::MacEnd: return "MAC-END";
    case MacPduKind::MacDBlck: return "MAC-D-BLCK";
    case MacPduKind::Unknown: break;
    }
    return "unknown";
}

ServingCellParams serving_cell_params(const SyncInfo& sync, const SysinfoInfo& si)
{
    ServingCellParams p;
    p.mcc = sync.mcc;
    p.mnc = sync.mnc;
    p.colour_code = sync.colour_code;
    p.system_code = sync.system_code;
    p.location_area = si.location_area;
    p.main_carrier = si.main_carrier;
    p.frequency_band = si.frequency_band;
    p.offset = si.offset;
    p.offset_hz = si.offset_hz();
    p.downlink_hz = si.downlink_hz();
    p.duplex_spacing = si.duplex_spacing;
    p.duplex_spacing_hz = si.duplex_spacing_hz();
    p.reverse_operation = si.reverse_operation;
    p.uplink_hz = si.uplink_hz();
    p.mcch_carrier = si.main_carrier;
    p.mcch_hz = p.downlink_hz;
    p.common_scch = si.common_scch;
    p.ms_txpwr_max_cell = si.ms_txpwr_max_cell;
    p.ms_txpwr_max_dbm = si.ms_txpwr_max_dbm();
    p.rxlev_access_min = si.rxlev_access_min;
    p.rxlev_access_min_dbm = si.rxlev_access_min_dbm();
    p.access_parameter = si.access_parameter;
    p.access_parameter_dbm = si.access_parameter_dbm();
    p.radio_downlink_timeout = si.radio_downlink_timeout;
    p.trunking = si.system_wide_services() ? TrunkingMode::WideArea : TrunkingMode::LocalSite;
    p.service_details = si.service_details;
    p.subscriber_class = si.subscriber_class;
    p.air_encryption = si.air_encryption();
    p.security_class = si.security_class();
    p.security_information = si.security_information();
    if (si.hyperframe_present)
        p.hyperframe = si.hyperframe;
    return p;
}

}  // namespace yantra::receiver::tetra
