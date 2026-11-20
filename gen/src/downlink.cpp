// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Downlink logical-channel schedule of the TETRA test-signal generator
// (RAD-70). Copied from yantra 68ea20a (plugins/tetra/gen/src/cell_tx.cpp),
// adapted: stops at the coded blocks (bursts and modulation are separate).

#include "yantra/gen/downlink.hpp"

#include "yantra/gen/coding.hpp"

#include <cstddef>
#include <stdexcept>

namespace yantra::gen {

double downlink_frequency_hz(int frequency_band, int main_carrier, int offset_code)
{
    if (frequency_band < 0 || frequency_band > 15)
        throw std::out_of_range("frequency band must be 0..15");
    if (main_carrier < 0 || main_carrier > 4095)
        throw std::out_of_range("main carrier must be 0..4095");
    static constexpr double kOffset[4] = {0.0, 6250.0, -6250.0, 12500.0};
    if (offset_code < 0 || offset_code > 3)
        throw std::out_of_range("offset code must be 0..3");
    return frequency_band * 100e6 + main_carrier * 25e3 + kOffset[offset_code];
}

CellConfig CellConfig::defaults()
{
    CellConfig c;
    c.sync.colour_code = 1;
    c.sync.mle.mcc = 901;
    c.sync.mle.mnc = 1;
    c.sysinfo.main_carrier = 3600;
    c.sysinfo.frequency_band = 3;
    c.sysinfo.option = SysinfoOption::ExtendedServices;
    c.sysinfo.option_value = ExtendedServices{}.pack();
    c.sysinfo.mle.location_area = 1;
    return c;
}

uint32_t CellConfig::scrambling() const
{
    return scrambling_init(sync.mle.mcc, sync.mle.mnc, sync.colour_code);
}

const AccessAssign& planned_access_assign(const CellConfig& cfg, const TdmaTime& t)
{
    if (t.fn == kFramesPerMultiframe || cfg.access_assign_plan.empty())
        return cfg.access_assign;
    const auto k = static_cast<std::size_t>((t.fn - 1) * 4 + t.tn - 1);
    return cfg.access_assign_plan[k % cfg.access_assign_plan.size()];
}

SlotPdus downlink_pdus(const CellConfig& cfg, const TdmaTime& t)
{
    if (!t.valid())
        throw std::invalid_argument("TDMA time out of range");
    SlotPdus out;
    out.access_assign = encode_access_assign(planned_access_assign(cfg, t));
    if (t.fn == kFramesPerMultiframe) {
        MacSync sync = cfg.sync;
        sync.timeslot = t.tn;
        sync.frame = t.fn;
        sync.multiframe = t.mn;
        MacSysinfo sysinfo = cfg.sysinfo;
        if (!sysinfo.cck_flag)
            sysinfo.hyperframe_or_cck = t.hn;
        out.content = SlotContent::Sync;
        out.first = encode_sync(sync);
        out.second = encode_sysinfo(sysinfo);
        return out;
    }
    if (cfg.nwrk_every_frames < 1)
        throw std::invalid_argument("nwrk_every_frames must be at least 1");
    const bool nwrk = t.tn == 1 && cfg.nwrk_broadcast && (t.fn - 1) % cfg.nwrk_every_frames == 0;
    const NwrkBroadcast* pdu = nwrk ? &*cfg.nwrk_broadcast : nullptr;
    if (nwrk && !cfg.nwrk_more.empty()) {
        // The n-th D-NWRK-BROADCAST frame of the hyperframe sends PDU n mod count.
        const int per_multiframe = (kFramesPerMultiframe - 2) / cfg.nwrk_every_frames + 1;
        const int n = (t.mn - 1) * per_multiframe + (t.fn - 1) / cfg.nwrk_every_frames;
        const auto k = static_cast<std::size_t>(n) % (cfg.nwrk_more.size() + 1);
        if (k > 0)
            pdu = &cfg.nwrk_more[k - 1];
    }
    const Bits* planned = nullptr;
    for (const CellConfig::BlockSequence& q : cfg.block_sequences) {
        const int k = t.fn - q.first_frame;
        if (q.timeslot == t.tn && k >= 0 && k < static_cast<int>(q.blocks.size()))
            planned = &q.blocks[static_cast<std::size_t>(k)];
    }
    if (planned) {
        out.content = SlotContent::BlockSequence;
        out.first = *planned;
    } else if (pdu) {
        out.content = SlotContent::NwrkBroadcast;
        out.first = nwrk_broadcast_block(*pdu);
    } else {
        out.content = SlotContent::Null;
        out.first = null_block(kSchF.type1);
    }
    return out;
}

SlotBlocks downlink_blocks(const CellConfig& cfg, const TdmaTime& t)
{
    const SlotPdus p = downlink_pdus(cfg, t);
    const uint32_t cell = cfg.scrambling();
    SlotBlocks out;
    out.content = p.content;
    out.aach = encode_aach(p.access_assign, cell);
    if (p.content == SlotContent::Sync) {
        out.block1 = encode_bsch(p.first);
        out.block2 = encode_sch_hd(p.second, cell);
        return out;
    }
    const Bits block = encode_sch_f(p.first, cell);
    out.block1.assign(block.begin(), block.begin() + kBlockFieldBits);
    out.block2.assign(block.begin() + kBlockFieldBits, block.end());
    return out;
}

void validate(const CellConfig& cfg)
{
    TdmaTime t = cfg.start;
    if (!t.valid())
        throw std::invalid_argument("start time out of range");
    t.fn = kFramesPerMultiframe;
    downlink_blocks(cfg, t);
    t.fn = 1;
    t.tn = 1;
    downlink_blocks(cfg, t);
    t.tn = 2;
    downlink_blocks(cfg, t);
    for (const NwrkBroadcast& p : cfg.nwrk_more)
        nwrk_broadcast_block(p);
    for (const AccessAssign& a : cfg.access_assign_plan)
        encode_access_assign(a);
    for (const CellConfig::BlockSequence& q : cfg.block_sequences) {
        const int last = q.first_frame + static_cast<int>(q.blocks.size()) - 1;
        if (q.timeslot < 1 || q.timeslot > 4 || q.first_frame < 1 || last > kFramesPerMultiframe - 1)
            throw std::out_of_range("block sequence outside timeslots 1..4, frames 1..17");
        for (const Bits& b : q.blocks)
            if (b.size() != static_cast<std::size_t>(kSchF.type1))
                throw std::length_error("block sequence: a block is not an SCH/F block");
    }
}

}  // namespace yantra::gen
