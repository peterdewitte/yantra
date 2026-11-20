// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/fragment.cpp,
// adapted (namespace; encrypted parts counted, not kept).

#include "yantra/receiver/fragment.hpp"

namespace yantra::receiver::tetra {

ReassemblyCounters& ReassemblyCounters::operator+=(const ReassemblyCounters& o)
{
    started += o.started;
    completed += o.completed;
    lost_missing += o.lost_missing;
    lost_timeout += o.lost_timeout;
    lost_length += o.lost_length;
    orphans += o.orphans;
    return *this;
}

void Reassembler::drop(State& s, uint64_t& reason)
{
    if (!s.active)
        return;
    ++reason;
    s = State{};
}

bool Reassembler::active(int timeslot) const
{
    return timeslot >= 1 && timeslot <= 4 && slots_[static_cast<std::size_t>(timeslot - 1)].active;
}

bool Reassembler::encrypted(int timeslot) const
{
    return active(timeslot) && slots_[static_cast<std::size_t>(timeslot - 1)].encrypted;
}

std::optional<BlockPdu> Reassembler::push(int timeslot, uint64_t slot, const uint8_t* bits, const BlockPdu& p)
{
    if (timeslot < 1 || timeslot > 4 || p.fragment == Fragment::None)
        return std::nullopt;
    State& s = slots_[static_cast<std::size_t>(timeslot - 1)];
    if (s.active && slot - s.start_slot > cfg_.timeout_slots)
        drop(s, counters_.lost_timeout);
    if (p.sdu_end < p.sdu_start)
        return std::nullopt;
    const auto part = [&] {
        s.length += p.sdu_end - p.sdu_start;
        if (!s.encrypted)
            s.bits.insert(s.bits.end(), bits + p.sdu_start, bits + p.sdu_end);
    };
    if (p.fragment == Fragment::Start) {
        drop(s, counters_.lost_missing);  // the previous one never ended
        s.active = true;
        s.start_slot = slot;
        s.fragments = 1;
        s.encrypted = p.encrypted;
        s.first = p;
        part();
        ++counters_.started;
        return std::nullopt;
    }
    if (!s.active) {
        ++counters_.orphans;
        return std::nullopt;
    }
    part();
    ++s.fragments;
    if (s.length > cfg_.max_bits) {
        drop(s, counters_.lost_length);
        return std::nullopt;
    }
    if (p.fragment == Fragment::Frag)
        return std::nullopt;
    // MAC-END: the TM-SDU is whole.
    BlockPdu out;
    if (s.encrypted) {
        out.reassembled = true;
        out.encrypted = true;  // counted only (DEC-10)
        out.sdu_end = s.length;
    } else {
        out = parse_tm_sdu(s.bits);
    }
    out.offset = p.offset;
    out.fragments = s.fragments;
    out.address_type = s.first.address_type;
    out.broadcast = s.first.broadcast;
    out.channel_allocation = p.channel_allocation ? p.channel_allocation : s.first.channel_allocation;
    s = State{};
    ++counters_.completed;
    return out;
}

void Reassembler::block_lost(int timeslot)
{
    if (timeslot >= 1 && timeslot <= 4)
        drop(slots_[static_cast<std::size_t>(timeslot - 1)], counters_.lost_missing);
}

void Reassembler::tick(uint64_t slot)
{
    for (State& s : slots_)
        if (s.active && slot - s.start_slot > cfg_.timeout_slots)
            drop(s, counters_.lost_timeout);
}

void Reassembler::reset()
{
    for (State& s : slots_)
        drop(s, counters_.lost_missing);
}

ReassemblyCounters Reassembler::take_counters()
{
    ReassemblyCounters c = counters_;
    counters_ = {};
    return c;
}

}  // namespace yantra::receiver::tetra
