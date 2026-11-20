// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target tetra_bursts (RAD-96, DEC-12, RAD-17): soft-bit bursts
// through the lower MAC (osmo-tetra's burst split, descrambling,
// de-interleaving, Viterbi, CRC, RM(30,14)), the reassembly of fragmented
// TM-SDUs and the GSMTAP scope filter, the way the carrier decoder chains
// them. Property: every GSMTAP datagram holds only in-scope PDUs (no
// address, nothing encrypted, no other MAC PDU type).
// Input: bursts of 1 type byte (mod 3: sync, normal full, normal half) and
// 510 soft bits (int8); a short last burst is padded with erasures.
// Copied from yantra 68ea20a: tests/fuzz/target_tetra_bursts.cpp, adapted
// (receiver namespace).

#include "fuzz.hpp"

#include "yantra/receiver/fragment.hpp"
#include "yantra/receiver/gsmtap.hpp"
#include "yantra/receiver/lower_mac.hpp"
#include "yantra/receiver/mle.hpp"

namespace dec = yantra::receiver::tetra;
namespace tp = yantra::receiver::tetra;
using yantra::fuzz::fail;

namespace {

void check_datagram(const std::vector<uint8_t>& d)
{
    if (d.size() < tp::kGsmtapHeaderBytes || d[0] != tp::kGsmtapVersion || d[1] != 4 || d[2] != tp::kGsmtapTypeTetra ||
        d[3] > 3)
        fail("GSMTAP header");
    const uint8_t sub = d[12];
    int nbits = 0;
    switch (sub) {
    case tp::kGsmtapBsch: nbits = 60; break;
    case tp::kGsmtapAach: nbits = 14; break;
    case tp::kGsmtapBnch: nbits = 124; break;
    case tp::kGsmtapSchHd:
    case tp::kGsmtapStch: nbits = 124; break;
    case tp::kGsmtapSchF: nbits = 268; break;
    default: fail("GSMTAP sub-type");
    }
    if (d.size() != tp::kGsmtapHeaderBytes + static_cast<size_t>((nbits + 7) / 8))
        fail("GSMTAP payload length");
    if (sub != tp::kGsmtapSchF && sub != tp::kGsmtapSchHd && sub != tp::kGsmtapStch)
        return;
    const std::vector<uint8_t> bits =
        yantra::fuzz::unpack(d.data() + tp::kGsmtapHeaderBytes, d.size() - tp::kGsmtapHeaderBytes);
    for (const dec::BlockPdu& p : dec::parse_block_pdus(bits.data(), nbits)) {
        if (p.encrypted)
            fail("GSMTAP sent an encrypted PDU");
        if (p.mac_pdu_type > 1)
            fail("GSMTAP sent another MAC PDU type");
        if (p.mac_pdu_type == 0 && !p.null_pdu && !p.broadcast)
            fail("GSMTAP sent an addressed PDU");
        if (p.mac_pdu_type == 0 && !p.null_pdu && p.fragment == dec::Fragment::None &&
            !tp::GsmtapFilter::content_in_scope(p))
            fail("GSMTAP sent out-of-scope content");
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    yantra::fuzz::Reader in(data, size);
    dec::LowerMac mac;
    dec::Reassembler reasm;
    tp::GsmtapFilter filter(check_datagram);
    uint64_t slot = 0;
    for (int guard = 0; guard < 256 && !in.done(); ++guard) {
        dec::Burst b;
        b.type = static_cast<dec::BurstType>(in.u8() % 3);
        for (auto& s : b.soft)
            s = static_cast<dec::SoftBit>(in.u8());
        dec::SlotResult r = mac.decode(b);
        ++slot;
        const int ts = r.time_known ? r.time.timeslot : 1;
        if (ts < 1 || ts > 4)
            fail("timeslot out of range");
        for (dec::DecodedBlock& blk : r.blocks) {
            if (blk.type1_bits < 0 || blk.type1_bits > static_cast<int>(blk.bits.size()))
                fail("type-1 bit count");
            if (blk.channel == dec::LogicalChannel::Tch)
                continue;
            if (!blk.crc_ok) {
                reasm.block_lost(ts);
                continue;
            }
            std::vector<dec::BlockPdu> whole;
            for (const dec::BlockPdu& p : blk.pdus)
                if (auto w = reasm.push(ts, slot, blk.bits.data(), p))
                    whole.push_back(std::move(*w));
            for (auto& w : whole)
                blk.pdus.push_back(std::move(w));
        }
        reasm.tick(slot);
        filter.slot(r, 1);
    }
    const auto& c = reasm.counters();
    if (c.completed + c.lost() > c.started)
        fail("reassembly counters");
    return 0;
}
