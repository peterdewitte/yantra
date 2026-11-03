// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoder of one TETRA downlink carrier (RAD-101, DEC-01..03, DEC-05,
// DEC-10, DEC-11): channel IQ at 72 kHz (tetra_params.hpp, e.g. one output
// of the Channelizer) -> pi/4-DQPSK demodulator (soft bits) ->
// bit-error-tolerant burst sync -> lower MAC on osmo-tetra. It reports
// every timeslot with its H-M-F-S time and wall-clock UTC (DEC-11), the
// cell's broadcast identity, system information and DEC-03 parameter set
// when first complete or changed (DEC-02, DEC-03), and per period (one
// multiframe by default) the level, MER, frequency offset, block and CRC
// error counts per logical channel, encrypted block counts (DEC-10) and
// the AACH downlink usage per timeslot (DEC-05). Above the lower MAC the
// protocol crate parses the MAC PDUs of every signalling block
// (MAC-RESOURCE/FRAG/END -> LLC -> MLE, mle.hpp); fragmented TM-SDUs are
// put together per timeslot (fragment.hpp, DEC-04); D-NWRK-BROADCAST
// gives the cell re-select parameters (ServingCellParams::reselect,
// DEC-03) and the neighbour list (CellInfo::network, DEC-02); channel
// allocations are counted per carrier and timeslot (DEC-06: counts only,
// no call is followed). Per quality window (quality.hpp, RAD-113) it
// reports the message erasure rate, slot CRC error rates, BER estimate,
// modulation error ratio/EVM, SNR and frequency error.
//
// One CarrierDecoder per carrier; instances share no state, so carriers
// decode in parallel threads (one decoder per thread at a time). Its
// output stays inside yantra-radios: the plugins turn it into records
// (RAD-20..26).
#pragma once

#include "yantra/receiver/broadcast.hpp"
#include "yantra/receiver/burst_sync.hpp"
#include "yantra/receiver/fragment.hpp"
#include "yantra/receiver/kernels.hpp"
#include "yantra/receiver/lower_mac.hpp"
#include "yantra/receiver/mle.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/receiver/quality.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace yantra::receiver::tetra {

/// A timeslot as decoded.
struct DecodedSlot {
    double sample_pos = 0.0;  ///< channel sample (72 kHz since the decoder's start) of the slot start
    int64_t utc_ns = 0;       ///< wall-clock UTC of the slot start (DEC-11); 0 without a reference
    bool found = false;       ///< false: missed (no training sequence)
    bool lost = false;        ///< burst sync was lost after this slot
    bool coasting = false;    ///< missed while the burst sync coasts
    int training_errors = 0;
    SlotResult result;        ///< when found
};

/// What the cell broadcasts in D-NWRK-BROADCAST (DEC-02): the latest cell
/// re-select parameters, cell load and network time, and the neighbour
/// cells of all D-NWRK-BROADCAST PDUs so far, merged by cell identifier
/// (one PDU holds at most 7; a cell may spread a longer list, up to 31
/// entries, over several PDUs). Neighbours are never dropped while the
/// cell stays the same; a new cell identity starts an empty list.
struct NetworkInfo {
    ReselectParams reselect;
    uint8_t cell_load = 0;  ///< 0 unknown, 1 low, 2 medium, 3 high
    std::optional<NetworkTimeInfo> network_time;
    std::vector<NeighbourInfo> neighbours;  ///< sorted by cell identifier
    uint64_t broadcasts = 0;                ///< D-NWRK-BROADCAST PDUs received

    /// Same parameters and neighbours (network time and count aside).
    bool same_content(const NetworkInfo& o) const
    {
        return reselect == o.reselect && cell_load == o.cell_load && neighbours == o.neighbours;
    }
    /// Merges one D-NWRK-BROADCAST.
    void merge(const NwrkBroadcastInfo& b);
};

/// Broadcast identity and system information of the cell on the carrier.
struct CellInfo {
    SyncInfo sync;        ///< time fields (timeslot, frame, multiframe) zero
    SysinfoInfo sysinfo;  ///< hyperframe as last received
    /// The DEC-03 set; its counts (encrypted blocks, cell load) as of the
    /// report; `reselect` once a D-NWRK-BROADCAST was received.
    ServingCellParams params;
    std::optional<NetworkInfo> network;  ///< once a D-NWRK-BROADCAST was received
    /// Same cell and broadcast content; hyperframe, counts, load, network
    /// time and broadcast count move on by themselves and are ignored.
    bool same_content(const CellInfo& o) const;
};

struct CellEvent {
    double sample_pos = 0.0;
    int64_t utc_ns = 0;
    CellInfo cell;
    bool changed = false;  ///< first report or different from the previous one
};

/// Blocks per logical channel in one period.
struct ChannelCounters {
    uint64_t blocks = 0;
    uint64_t crc_errors = 0;  ///< blocks whose CRC failed (AACH: RM(30,14) not decodable)
    uint64_t encrypted = 0;   ///< MAC PDUs sent encrypted (DEC-10), only counted
    uint64_t fcs_errors = 0;  ///< TM-SDUs whose LLC frame check sequence failed
    void add(const ChannelCounters& o)
    {
        blocks += o.blocks;
        crc_errors += o.crc_errors;
        encrypted += o.encrypted;
        fcs_errors += o.fcs_errors;
    }
};

/// Channel allocations (MAC-RESOURCE / MAC-END, 21.5.2) to one carrier
/// (DEC-06): how often the cell sent a channel there, per timeslot. A
/// cell load figure only: no address, call or voice is followed.
struct ChannelAssignmentCount {
    uint16_t carrier = 0;  ///< carrier number
    /// Band, offset and duplex of the allocation's extension, if sent
    /// (otherwise the serving cell's band and offset apply).
    std::optional<CarrierExtensionInfo> extension;
    uint64_t downlink_hz = 0;  ///< 0 while the serving cell's band is unknown
    uint64_t allocations = 0;
    /// Per timeslot 1..4 (index 0..3); an allocation of several timeslots
    /// counts in each.
    std::array<uint64_t, 4> timeslots{};
    uint64_t mcch_timeslots = 0;  ///< allocations to "the MCCH sharing timeslots" (bitmap 0)
    bool same_carrier(const ChannelAssignmentCount& o) const
    {
        return carrier == o.carrier && extension == o.extension;
    }
    void add(const ChannelAssignmentCount& o);
};

/// AACH of one timeslot number in one period (DEC-05).
struct AachCounters {
    uint64_t blocks = 0;     ///< AACHs seen once the cell was known
    uint64_t decoded = 0;    ///< RM(30,14) decoded
    /// Downlink usage markers (frames 1..17): unallocated, assigned
    /// control, common control, reserved, traffic (>= 4).
    uint64_t dl_unallocated = 0;
    uint64_t dl_assigned_control = 0;
    uint64_t dl_common_control = 0;
    uint64_t dl_reserved = 0;
    uint64_t dl_traffic = 0;
    uint64_t dl_marked() const
    {
        return dl_unallocated + dl_assigned_control + dl_common_control + dl_reserved + dl_traffic;
    }
};

struct PeriodReport {
    double sample_pos = 0.0;  ///< end of the period (channel samples)
    uint64_t samples = 0;
    double level_dbfs = -300.0;   ///< mean power of the channel IQ
    double mer_db = 0.0;          ///< modulation error ratio (demodulator)
    double freq_offset_hz = 0.0;
    bool demod_locked = false;
    uint64_t slots = 0;           ///< slots while burst-locked (found + missed)
    uint64_t slots_found = 0;
    std::optional<SyncInfo> cell;  ///< identity of the decoded cell, if any (time fields zero)
    bool time_known = false;
    TdmaTime time;                 ///< time of the last slot in the period
    std::array<ChannelCounters, kLogicalChannelCount> channels{};
    std::array<AachCounters, 4> aach{};  ///< per timeslot number 1..4 (index 0..3)
    ReassemblyCounters fragments;        ///< reassembly of fragmented TM-SDUs (DEC-04)
    uint64_t nwrk_broadcasts = 0;        ///< D-NWRK-BROADCAST PDUs decoded
    /// Channel allocations per carrier (DEC-06), sorted by carrier.
    std::vector<ChannelAssignmentCount> assignments;
    /// Cell load from the AACH: (assigned control + traffic) / marked
    /// slots, in percent; empty without marked slots.
    std::optional<double> cell_load_percent() const;
};

struct DecoderOutput {
    std::vector<DecodedSlot> slots;
    std::vector<CellEvent> cells;
    std::vector<PeriodReport> periods;
    std::vector<QualityReport> quality;  ///< one per quality window (RAD-113)
    void clear()
    {
        slots.clear();
        cells.clear();
        periods.clear();
        quality.clear();
    }
};

struct CarrierDecoderConfig {
    Isa isa = Isa::Simd;
    uint64_t period_samples = 72u * 1020u;  ///< one multiframe (72 slots) at 72 kHz
    int cell_repeat_periods = 10;           ///< unchanged cell reported again after this many periods
    /// A demodulator lock without any training sequence for this many
    /// samples restarts its acquisition (0 = never): two multiframes and
    /// four frames (about 2.3 s), so one frame 18 lost in a fade does not
    /// restart it.
    uint64_t resync_samples = 2u * 72u * 1020u + 16u * 1020u;
    /// Wall-clock UTC (DEC-11): without set_utc_reference(), the first
    /// process() call ties the end of its input to the system clock. False:
    /// slots carry utc_ns 0 until a reference is set (deterministic tests).
    bool utc_from_system_clock = true;
    /// Keep every DecodedSlot in DecoderOutput::slots (false: only cell
    /// events and periods, for long runs).
    bool keep_slots = true;
    /// Quality window (RAD-113) in channel samples: 1 s at 72 kHz, the
    /// measurement engine's default record interval; 0 = no quality
    /// reports.
    uint64_t quality_samples = 72000;
    QualityConfig quality;
    BurstSyncConfig sync;
    ReassemblyConfig reassembly;
};

class CarrierDecoder {
public:
    explicit CarrierDecoder(const CarrierDecoderConfig& config = {});

    /// Feeds channel IQ (72 kHz); appends what was decoded.
    void process(const cf* x, std::size_t n, DecoderOutput& out);
    void reset();
    /// Ties channel sample `sample_pos` to `utc_ns` (ns since 1970 UTC).
    void set_utc_reference(double sample_pos, int64_t utc_ns);

    const DemodStatus& demod_status() const { return demod_.status(); }
    bool burst_locked() const { return sync_.locked(); }
    const LowerMac& mac() const { return mac_; }
    /// The cell as decoded so far (SYNC and SYSINFO both received).
    std::optional<CellInfo> cell() const;
    /// The DEC-03 set of the current cell.
    std::optional<ServingCellParams> serving_cell() const;
    /// Counters since the start (or reset), per logical channel, the
    /// current period included.
    std::array<ChannelCounters, kLogicalChannelCount> totals() const;
    /// The current cell's D-NWRK-BROADCAST content (neighbours, re-select
    /// parameters), once one was received.
    const std::optional<NetworkInfo>& network() const { return network_; }
    /// Reassembly counters since the start (or reset), the current period
    /// included.
    ReassemblyCounters reassembly_totals() const;
    /// Channel allocations per carrier since the start (or reset), the
    /// current period included (DEC-06).
    std::vector<ChannelAssignmentCount> assignment_totals() const;
    /// Channel samples processed so far.
    uint64_t samples() const { return samples_in_; }
    /// Acquisition restarts by the no-training-sequence watchdog.
    uint64_t restarts() const { return restarts_; }

private:
    void on_slot(const SlotEvent& ev, DecoderOutput& out);
    void count(const SlotResult& r);
    void reassemble(SlotResult& r);
    void count_assignment(const ChannelAllocationInfo& a);
    void finish_period(double sample_pos, DecoderOutput& out);
    void finish_quality(double sample_pos, DecoderOutput& out);
    int64_t utc_at(double sample_pos) const;

    CarrierDecoderConfig cfg_;
    Pi4DqpskDemodulator demod_;
    BurstSync sync_;
    LowerMac mac_;
    std::vector<DemodSymbol> symbols_;
    std::vector<SlotEvent> events_;
    std::optional<SyncInfo> sync_info_;
    std::optional<SysinfoInfo> sysinfo_;
    std::optional<NetworkInfo> network_;
    Reassembler reassembler_;
    uint64_t slot_count_ = 0;  ///< slots seen (found and missed): the reassembly's clock
    ReassemblyCounters fragment_totals_;  ///< of finished periods
    std::vector<ChannelAssignmentCount> assignment_totals_;  ///< of finished periods
    std::optional<CellInfo> reported_;
    uint64_t encrypted_blocks_ = 0;           ///< since the current cell was found
    std::optional<double> last_load_;         ///< cell load of the last period with markers
    int periods_since_report_ = 0;
    uint64_t samples_in_ = 0;
    PeriodReport period_;
    QualityMeter quality_;
    uint64_t quality_in_ = 0;  ///< samples in the current quality window
    std::array<ChannelCounters, kLogicalChannelCount> totals_{};  ///< of finished periods
    double power_acc_ = 0.0;
    double last_found_ = 0.0;  ///< sample of the last training sequence (or of the last time unlocked)
    uint64_t restarts_ = 0;
    uint64_t demod_origin_ = 0;  ///< decoder sample of the demodulator's last restart
    bool utc_set_ = false;
    double utc_sample_ = 0.0;
    int64_t utc_ns_ = 0;
};

}  // namespace yantra::receiver::tetra
