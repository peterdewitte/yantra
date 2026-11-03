// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lower MAC of the TETRA downlink on osmo-tetra (RAD-101, DEC-01, DEC-02,
// DEC-05, DEC-10, DEC-11). osmo-tetra's tetra_burst_rx_cb splits each
// burst of the burst synchroniser (burst_sync.hpp) into its blocks and
// calls the TP-SAP entry point tp_sap_udata_ind, which this library
// defines (in place of osmo-tetra's tetra_lower_mac.c, which is not
// built: it pulls in crypto/, global state and SDS). Per block:
// descrambling, de-interleaving, de-puncturing, Viterbi decoding and the
// CRC with osmo-tetra's and libosmocore's functions, on soft bits
// throughout; the AACH's RM(30,14) code is decoded with up to 3 bit
// errors corrected (osmo-tetra only strips its parity).
//
// Soft bits: Burst::soft already uses osmo-tetra's / libosmocore's sbit
// convention (+127 = surely 0, -127 = surely 1, 0 = erased; osmo-tetra's
// viterbi_dec_sb1_wrapper maps bit 0 to +127). The only conversion is
// clamping -128 to -127 so a sign flip (descrambling) cannot overflow.
//
// TDMA time (DEC-11): taken from MAC-SYNC (timeslot, frame, multiframe)
// and MAC-SYSINFO (hyperframe), counted on by one slot per burst or missed
// slot in between; every slot result and every block carries it.
//
// Instances: no global state of its own; the LowerMac travels through the
// callback's `priv` pointer, so several carriers decode in parallel, one
// LowerMac each. osmo-tetra and libosmocore have two lazily initialised
// globals (the RM(30,14) generator rows of tetra_rm3014_init, and the
// Viterbi kernel selection of libosmocore's osmo_conv_init, which sets its
// "done" flag before the kernel pointers); osmo_tetra_init() fills both
// once under std::call_once, and every LowerMac calls it on construction.
#pragma once

#include "yantra/receiver/broadcast.hpp"
#include "yantra/receiver/burst.hpp"
#include "yantra/receiver/tdma.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace yantra::receiver::tetra {

/// One decoded block of a burst.
struct DecodedBlock {
    LogicalChannel channel = LogicalChannel::Unknown;
    int block = 0;  ///< 1 or 2 for half-slot blocks (BSCH = 1, SB2 = 2), 0 for a full slot
    TdmaTime time;    ///< the slot's H-M-F-S (DEC-11)
    int64_t utc_ns = 0;  ///< wall-clock UTC of the slot (set by CarrierDecoder; 0 without a reference)
    bool crc_ok = false;
    int type1_bits = 0;
    /// Type-1 bits, one per byte, when crc_ok (for the MLE layer of step
    /// 4.3). Zero for a block whose MAC PDU is encrypted: its content is
    /// only flagged and counted, never kept (DEC-10, RAD-17).
    std::array<uint8_t, 268> bits{};
    /// Channel bit errors of a block whose CRC passed: its type-1 bits
    /// encoded again (CRC, tail, convolutional code, puncturing,
    /// interleaving) and compared with the hard decisions of the received
    /// bits (BER estimate). Zero when the CRC failed.
    int coded_bits = 0;
    int bit_errors = 0;
    MacPduInfo mac;                      ///< when crc_ok
    std::optional<SyncInfo> sync;        ///< BSCH with crc_ok
    std::optional<SysinfoInfo> sysinfo;  ///< MAC-SYSINFO with crc_ok
};

/// The AACH of a burst (DEC-05).
struct AachResult {
    bool decoded = false;  ///< RM(30,14) decoded (at most 3 bit errors)
    int corrected = 0;     ///< bit errors corrected
    std::array<uint8_t, 14> bits{};
    AccessAssignInfo info;
};

/// Everything decoded from one timeslot.
struct SlotResult {
    BurstType burst = BurstType::NormalFull;
    bool time_known = false;  ///< false before the first BSCH was decoded
    TdmaTime time;
    bool time_jump = false;   ///< a BSCH or SYSINFO disagreed with the counted time
    /// The burst's AACH was seen (every burst has one); it is decoded
    /// (`aach.decoded`) only once the cell's scrambling code is known.
    bool aach_present = false;
    AachResult aach;
    std::vector<DecodedBlock> blocks;
};

/// Decodes one RM(30,14) code word (first bit in bit 29). Returns the
/// 14 information bits and the number of corrected errors, or nothing
/// when more than 3 bits are wrong (detected).
std::optional<std::pair<uint16_t, int>> rm3014_decode(uint32_t word);

/// Initialises osmo-tetra's and libosmocore's lazily filled globals once
/// per process (thread-safe). Called by LowerMac; harmless to call again.
void osmo_tetra_init();

class LowerMac {
public:
    LowerMac();
    LowerMac(const LowerMac&) = delete;
    LowerMac& operator=(const LowerMac&) = delete;

    /// Decodes one burst; the TDMA time moves on by one slot first.
    SlotResult decode(const Burst& burst);
    /// A slot without a burst: only the time moves on.
    void missed_slot();
    /// Burst sync was lost: the time is unknown until the next BSCH.
    void lost_sync();
    void reset();

    bool time_known() const noexcept { return time_known_; }
    const TdmaTime& time() const noexcept { return time_; }
    /// The cell's scrambling code once a BSCH was decoded.
    std::optional<uint32_t> scrambling_code() const { return scrambling_; }
    /// The cell's SYNC once a BSCH was decoded (time fields as last sent).
    const std::optional<SyncInfo>& cell() const noexcept { return cell_; }

    /// TP-SAP entry for osmo-tetra's burst split (internal).
    void on_block(int type, int block, const SoftBit* bits, unsigned len);

private:
    void decode_aach(const SoftBit* bits);
    LogicalChannel half_slot_channel(int type) const;

    bool time_known_ = false;
    TdmaTime time_;
    std::optional<uint32_t> scrambling_;
    std::optional<SyncInfo> cell_;
    SlotResult* current_ = nullptr;
};

}  // namespace yantra::receiver::tetra
