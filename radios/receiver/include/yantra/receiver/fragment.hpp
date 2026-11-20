// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reassembly of fragmented TM-SDUs (DEC-04): a MAC-RESOURCE with "start of
// fragmentation", then MAC-FRAG blocks and a MAC-END, all on the same
// timeslot. One reassembly per timeslot; time is counted in slots of frame
// time (DEC-11), never the wall clock. A fragment lost to a CRC error, a
// new start before the end, a timeout or a too long TM-SDU drops the
// reassembly (counted per reason); a MAC-FRAG or MAC-END without a start
// is an orphan. Encrypted TM-SDUs are followed for their length only:
// their bits are never kept or decoded (DEC-10, RAD-17).
// Copied from yantra 68ea20a: plugins/tetra/decoder/include/yantra/tetra/
// decoder/fragment.h, adapted (namespace, encrypted parts not kept).

#pragma once

#include "yantra/receiver/mle.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace yantra::receiver::tetra {

struct ReassemblyCounters {
    uint64_t started = 0;
    uint64_t completed = 0;
    uint64_t lost_missing = 0;  ///< a fragment lost to a CRC error, or a new start before the end
    uint64_t lost_timeout = 0;
    uint64_t lost_length = 0;   ///< longer than the limit
    uint64_t orphans = 0;       ///< MAC-FRAG / MAC-END without a start
    uint64_t lost() const { return lost_missing + lost_timeout + lost_length; }
    ReassemblyCounters& operator+=(const ReassemblyCounters& o);
};

struct ReassemblyConfig {
    /// Slots from the start to the end at most (2 multiframes).
    uint64_t timeout_slots = 2u * 18u * 4u;
    /// Longest TM-SDU taken, in bits.
    int max_bits = 4096;
};

class Reassembler {
public:
    explicit Reassembler(const ReassemblyConfig& config = {}) : cfg_(config) {}

    /// One MAC PDU of a decoded signalling block on `timeslot` (1..4) at
    /// slot `slot` (a slot counter); `bits` are the block's type-1 bits.
    /// Returns the whole TM-SDU, decoded, when this was the MAC-END.
    std::optional<BlockPdu> push(int timeslot, uint64_t slot, const uint8_t* bits, const BlockPdu& p);
    /// A signalling block on `timeslot` failed its CRC: a fragment is lost.
    void block_lost(int timeslot);
    /// Drops reassemblies older than the timeout at `slot`.
    void tick(uint64_t slot);
    /// Sync lost: everything in progress is dropped (as missing).
    void reset();

    const ReassemblyCounters& counters() const { return counters_; }
    /// Counters since the last call, then zeroed.
    ReassemblyCounters take_counters();
    bool active(int timeslot) const;
    /// A reassembly is in progress on `timeslot` and its TM-SDU is
    /// encrypted: the MAC-FRAG/MAC-END parts that follow are encrypted
    /// content too (the caller clears them, DEC-10).
    bool encrypted(int timeslot) const;

private:
    struct State {
        bool active = false;
        uint64_t start_slot = 0;
        int fragments = 0;
        bool encrypted = false;
        BlockPdu first;
        std::vector<uint8_t> bits;  ///< empty when encrypted
        int length = 0;             ///< bits so far
    };
    void drop(State& s, uint64_t& reason);

    ReassemblyConfig cfg_;
    std::array<State, 4> slots_{};
    ReassemblyCounters counters_;
};

}  // namespace yantra::receiver::tetra
