// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GSMTAP output (RAD-115, DEC-12): the header of every datagram, the
// settings, the scope filter on decoded blocks (RAD-17: broadcast in-scope
// PDUs only, addressed PDUs never, MM/CMCE never, encrypted PDUs only
// counted, fragments held until their whole TM-SDU is known to be in
// scope), and a generator cell through the measurement engine into a UDP
// socket bound to an ephemeral loopback port (closed at the end; nothing
// keeps listening). Wireshark's own dissector is not run here (tshark is
// not on the tool lists); the datagrams are read back with the receiver's
// parsers instead.
// Copied from yantra 68ea20a: tests/tetra_plugin/test_gsmtap.cpp, adapted
// (engine instead of the plugin channel, no MM PDUs, local CMCE/MM bits).

#include "yantra/receiver/fragment.hpp"
#include "yantra/receiver/gsmtap.hpp"
#include "yantra/receiver/measurement_engine.hpp"
#include "yantra/receiver/mle.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <complex>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gen = yantra::gen;
namespace dec = yantra::receiver::tetra;
namespace tp = yantra::receiver::tetra;

namespace {

/// A made-up individual address for the tests (no real subscriber).
constexpr uint32_t kTestSsi = 0x123456;

/// An MLE PDU of another protocol (pdisc 3 bits, PDU type, zero payload):
/// MM (1) or CMCE (2); only typed, never sent.
gen::Bits other_protocol_pdu(uint8_t pdisc, uint8_t type, int type_bits, int payload_bits)
{
    gen::Bits b;
    for (int k = 2; k >= 0; --k)
        b.push_back(static_cast<uint8_t>((pdisc >> k) & 1));
    for (int k = type_bits - 1; k >= 0; --k)
        b.push_back(static_cast<uint8_t>((type >> k) & 1));
    b.insert(b.end(), static_cast<std::size_t>(payload_bits), 0);
    return b;
}
gen::Bits mm_pdu() { return other_protocol_pdu(1, 7, 4, 16); }     // D-LOCATION UPDATE REJECT
gen::Bits cmce_pdu() { return other_protocol_pdu(2, 15, 5, 80); }  // D-SDS-DATA

/// A MAC block with `tm_sdu` to the broadcast address, readdressed to
/// kTestSsi (address type 001 kept: SSI).
gen::Bits addressed_block(const gen::Bits& tm_sdu)
{
    gen::Bits b = gen::mac_resource_block(tm_sdu, 268);
    for (int k = 0; k < 24; ++k)
        b[16 + 3 + static_cast<std::size_t>(k)] = static_cast<uint8_t>((kTestSsi >> (23 - k)) & 1);
    return b;
}

std::vector<gen::Bits> fragmented_nwrk(const gen::ResourceOptions& o = {})
{
    gen::NwrkBroadcast p;
    p.neighbours.emplace();
    for (int i = 0; i < 7; ++i) {
        gen::NeighbourCell n;
        n.cell_id = static_cast<uint8_t>(i + 1);
        n.main_carrier = static_cast<uint16_t>(3601 + i);
        n.mcc = 901;
        n.mnc = 77;
        n.location_area = static_cast<uint16_t>(0x40 + i);
        p.neighbours->push_back(n);
    }
    return gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(p)), 268, o);
}

/// A decoded SCH/F block on `timeslot` of frame `fn` holding `bits`.
dec::SlotResult slot_of(const gen::Bits& bits, int timeslot, int fn, bool crc_ok = true)
{
    dec::SlotResult r;
    r.time_known = true;
    r.time.multiframe = 7;
    r.time.frame = static_cast<uint8_t>(fn);
    r.time.timeslot = static_cast<uint8_t>(timeslot);
    dec::DecodedBlock b;
    b.channel = dec::LogicalChannel::SchF;
    b.crc_ok = crc_ok;
    b.type1_bits = 268;
    std::copy(bits.begin(), bits.end(), b.bits.begin());
    if (crc_ok) {
        b.mac = dec::parse_mac_pdu(b.bits.data(), 268, b.channel);
        b.pdus = dec::parse_block_pdus(b.bits.data(), 268);
    }
    r.blocks.push_back(b);
    return r;
}

/// Runs blocks through a Reassembler (as the CarrierDecoder does) and a
/// GsmtapFilter; returns the datagrams.
struct FilterRun {
    std::vector<std::vector<uint8_t>> out;
    tp::GsmtapFilter filter{[this](const std::vector<uint8_t>& d) { out.push_back(d); }};
    dec::Reassembler reasm;
    uint64_t slot = 0;

    void push(const gen::Bits& bits, int timeslot = 1, bool crc_ok = true)
    {
        dec::SlotResult r = slot_of(bits, timeslot, static_cast<int>(1 + slot % 17), crc_ok);
        ++slot;
        dec::DecodedBlock& b = r.blocks[0];
        if (!crc_ok) {
            reasm.block_lost(timeslot);
        } else {
            std::vector<dec::BlockPdu> extra;
            for (const dec::BlockPdu& p : b.pdus)
                if (auto whole = reasm.push(timeslot, slot, b.bits.data(), p))
                    extra.push_back(std::move(*whole));
            for (auto& w : extra)
                b.pdus.push_back(std::move(w));
        }
        filter.slot(r, 3600);
    }
};

std::vector<uint8_t> unpack(const std::vector<uint8_t>& d)
{
    std::vector<uint8_t> bits;
    for (std::size_t k = tp::kGsmtapHeaderBytes; k < d.size(); ++k)
        for (int i = 7; i >= 0; --i)
            bits.push_back(static_cast<uint8_t>((d[k] >> i) & 1));
    return bits;
}

uint32_t be32(const std::vector<uint8_t>& d, std::size_t at)
{
    return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | d[at + 3];
}

/// Checks every MAC PDU of an SCH datagram is in scope.
void check_in_scope(const std::vector<uint8_t>& d)
{
    const uint8_t sub = d[12];
    if (sub != tp::kGsmtapSchF && sub != tp::kGsmtapSchHd && sub != tp::kGsmtapStch)
        return;
    const std::vector<uint8_t> bits = unpack(d);
    const int nbits = sub == tp::kGsmtapSchF ? 268 : 124;
    for (const dec::BlockPdu& p : dec::parse_block_pdus(bits.data(), nbits)) {
        CHECK_FALSE(p.encrypted);
        if (p.mac_pdu_type == 0 && !p.null_pdu) {
            CHECK(p.broadcast);
            if (p.fragment == dec::Fragment::None) {
                REQUIRE(p.pdisc.has_value());
                CHECK(*p.pdisc == 5);
            }
        }
    }
    // The test address never leaves, at any bit offset.
    for (std::size_t k = 0; k + 24 <= bits.size(); ++k) {
        uint32_t v = 0;
        for (std::size_t i = 0; i < 24; ++i)
            v = (v << 1) | bits[k + i];
        CHECK(v != kTestSsi);
    }
}

}  // namespace

TEST_CASE("gsmtap: header of a datagram as osmo-tetra sends it")
{
    dec::TdmaTime t;
    t.hyperframe = 3;
    t.hyperframe_known = true;
    t.multiframe = 12;
    t.frame = 18;
    t.timeslot = 4;
    const std::vector<uint8_t> bits = {1, 0, 1, 1, 0, 0, 0, 1, 1};
    const auto d = tp::gsmtap_packet(t, tp::kGsmtapBnch, 3600, 0, bits.data(), static_cast<int>(bits.size()));
    REQUIRE(d.size() == 16 + 2);
    CHECK(d[0] == 2);     // version
    CHECK(d[1] == 4);     // header length in 32-bit words
    CHECK(d[2] == 0x05);  // GSMTAP_TYPE_TETRA_I1
    CHECK(d[3] == 3);     // timeslot 0..3
    CHECK(((d[4] << 8) | d[5]) == 3600);
    CHECK(be32(d, 8) == (3u * 60u + 12u) * 18u + 18u);
    CHECK(d[12] == 0x06);  // GSMTAP_TETRA_BNCH
    CHECK(d[13] == 0);
    CHECK(d[14] == 0);
    CHECK(d[15] == 0);
    CHECK(d[16] == 0xB1);
    CHECK(d[17] == 0x80);
    t.hyperframe_known = false;
    CHECK(tp::gsmtap_frame_number(t) == 12u * 18u + 18u);
}

TEST_CASE("gsmtap: settings - off by default, numeric addresses only")
{
    std::map<std::string, std::string> s;
    const tp::GsmtapSettingFn get = [&](const char* k) -> std::optional<std::string> {
        const auto it = s.find(k);
        return it == s.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    std::string err;
    tp::GsmtapOptions o = tp::gsmtap_options(get, err);
    CHECK_FALSE(o.enabled);
    CHECK(o.host == "127.0.0.1");
    CHECK(o.port == 4729);
    CHECK(err.empty());
    s["gsmtap.enabled"] = "1";
    o = tp::gsmtap_options(get, err);
    CHECK(o.enabled);
    s["gsmtap.host"] = "::1";
    s["gsmtap.port"] = "5000";
    o = tp::gsmtap_options(get, err);
    CHECK(o.enabled);
    CHECK(o.host == "::1");
    CHECK(o.port == 5000);
    s["gsmtap.host"] = "localhost";  // no name lookups
    o = tp::gsmtap_options(get, err);
    CHECK_FALSE(o.enabled);
    CHECK_FALSE(err.empty());
    s["gsmtap.host"] = "127.0.0.1";
    for (const char* bad : {"0", "65536", "12x", "-1"}) {
        s["gsmtap.port"] = bad;
        o = tp::gsmtap_options(get, err);
        CHECK_FALSE(o.enabled);
        CHECK_FALSE(err.empty());
    }
    s["gsmtap.port"] = "4729";
    s["gsmtap.enabled"] = "maybe";
    o = tp::gsmtap_options(get, err);
    CHECK_FALSE(o.enabled);
    CHECK_FALSE(err.empty());
    CHECK(tp::is_loopback_address("127.0.0.1"));
    CHECK(tp::is_loopback_address("127.1.2.3"));
    CHECK(tp::is_loopback_address("::1"));
    CHECK_FALSE(tp::is_loopback_address("192.0.2.1"));  // documentation range
    CHECK_FALSE(tp::is_loopback_address("2001:db8::1"));
    CHECK_FALSE(tp::is_loopback_address("localhost"));
}

TEST_CASE("gsmtap: the filter sends broadcast in-scope blocks only")
{
    FilterRun f;
    // D-NWRK-BROADCAST to the broadcast address: sent.
    f.push(gen::nwrk_broadcast_block({}));
    CHECK(f.out.size() == 1);
    // A Null PDU block: sent (nothing in it).
    f.push(gen::null_block(268));
    CHECK(f.out.size() == 2);
    // An MM PDU addressed to an SSI: never sent.
    f.push(addressed_block(gen::llc_bl_udata(mm_pdu())));
    // CMCE short data even to the broadcast address: never sent.
    f.push(gen::mac_resource_block(gen::llc_bl_udata(cmce_pdu()), 268));
    // Encrypted: counted only.
    gen::ResourceOptions enc;
    enc.encryption_mode = 1;
    f.push(gen::mac_resource_block(gen::llc_bl_udata(gen::encode_nwrk_broadcast({})), 268, enc));
    // A wrong FCS: the content is not known to be what it seems.
    f.push(gen::mac_resource_block(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast({}), true), 268));
    CHECK(f.out.size() == 2);
    CHECK(f.filter.counters().sent == 2);
    CHECK(f.filter.counters().filtered == 3);
    CHECK(f.filter.counters().encrypted == 1);
    // MM D-LOCATION UPDATE REJECT even to the broadcast address: not
    // broadcast or system information (RAD-17), never sent.
    f.push(gen::mac_resource_block(gen::llc_bl_udata(mm_pdu()), 268));
    CHECK(f.out.size() == 2);
    CHECK(f.filter.counters().filtered == 4);
    // The D-NWRK-BROADCAST extension name is sent like the PDU itself: the
    // rule is on (pdisc, type) only.
    dec::BlockPdu p;
    p.broadcast = true;
    p.pdisc = 5;
    p.pdu_type = 3;
    CHECK(tp::GsmtapFilter::content_in_scope(p));
    p.pdu_type = 1;  // D-NEW-CELL
    CHECK_FALSE(tp::GsmtapFilter::content_in_scope(p));
    for (const auto& d : f.out)
        check_in_scope(d);
}

TEST_CASE("gsmtap: fragments are held until the whole TM-SDU is in scope")
{
    const auto blocks = fragmented_nwrk();
    REQUIRE(blocks.size() >= 3);
    {
        FilterRun f;
        for (std::size_t k = 0; k + 1 < blocks.size(); ++k) {
            f.push(blocks[k], 2);
            CHECK(f.out.empty());
        }
        f.push(blocks.back(), 2);
        CHECK(f.out.size() == blocks.size());
        CHECK(f.filter.counters().dropped == 0);
        for (std::size_t k = 0; k < f.out.size(); ++k) {
            CHECK(f.out[k][3] == 1);  // timeslot 2
            std::vector<uint8_t> bits = unpack(f.out[k]);
            REQUIRE(bits.size() == 272);  // 268 bits, padded to whole octets with 0
            CHECK(std::vector<uint8_t>(bits.begin() + 268, bits.end()) == std::vector<uint8_t>(4, 0));
            bits.resize(268);
            CHECK(bits == std::vector<uint8_t>(blocks[k].begin(), blocks[k].end()));
        }
    }
    {
        // A fragment lost to a CRC error: nothing of this TM-SDU is sent.
        FilterRun f;
        f.push(blocks[0], 2);
        f.push(blocks[1], 2, false);
        for (std::size_t k = 2; k < blocks.size(); ++k)
            f.push(blocks[k], 2);
        CHECK(f.out.empty());
        CHECK(f.filter.counters().dropped >= 1);
    }
    {
        // Encrypted fragments: counted, never sent.
        gen::ResourceOptions enc;
        enc.encryption_mode = 2;
        FilterRun f;
        for (const auto& b : fragmented_nwrk(enc))
            f.push(b, 3);
        CHECK(f.out.empty());
        CHECK(f.filter.counters().encrypted >= 1);
    }
    {
        // Fragments of a TM-SDU to an individual address: never sent.
        auto addr = blocks;
        for (int k = 0; k < 24; ++k)
            addr[0][19 + static_cast<std::size_t>(k)] = static_cast<uint8_t>((kTestSsi >> (23 - k)) & 1);
        FilterRun f;
        for (const auto& b : addr)
            f.push(b, 1);
        CHECK(f.out.empty());
    }
}

TEST_CASE("gsmtap: a generator cell through the engine into a loopback UDP socket")
{
    // Receiver on an ephemeral loopback port.
    const int rx = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(rx >= 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    REQUIRE(::bind(rx, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
    socklen_t alen = sizeof a;
    REQUIRE(::getsockname(rx, reinterpret_cast<sockaddr*>(&a), &alen) == 0);
    const uint16_t port = ntohs(a.sin_port);
    // Enough room for every datagram of the run (drained after each block).
    int rcvbuf = 1 << 20;
    ::setsockopt(rx, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

    gen::CellConfig cell = gen::CellConfig::defaults();
    gen::NwrkBroadcast nb;
    nb.neighbours.emplace();
    gen::NeighbourCell n;
    n.cell_id = 2;
    n.main_carrier = 3610;
    nb.neighbours->push_back(n);
    cell.nwrk_broadcast = nb;
    cell.block_sequences.push_back({1, 2, fragmented_nwrk()});
    gen::ResourceOptions enc;
    enc.encryption_mode = 1;
    cell.block_sequences.push_back(
        {1, 9,
         {addressed_block(gen::llc_bl_udata(mm_pdu())), gen::mac_resource_block(gen::llc_bl_udata(cmce_pdu()), 268),
          gen::mac_resource_block(gen::llc_bl_udata(gen::encode_nwrk_broadcast({})), 268, enc)}});
    constexpr double kRate = 250000.0;
    constexpr double kCentre = 390.0e6;
    gen::MultiCarrierTransmitter tx(kRate);
    tx.add(cell, {0.0, -20.0, 0.0});

    tp::EngineConfig cfg;
    cfg.carriers = {kCentre};
    cfg.search = false;
    cfg.gsmtap.enabled = true;
    cfg.gsmtap.host = "127.0.0.1";
    cfg.gsmtap.port = port;
    tp::MeasurementEngine engine(cfg);

    std::vector<std::vector<uint8_t>> got;
    auto drain = [&] {
        for (;;) {
            std::vector<uint8_t> buf(2048);
            const ssize_t len = ::recv(rx, buf.data(), buf.size(), MSG_DONTWAIT);
            if (len < 0)
                break;
            buf.resize(static_cast<std::size_t>(len));
            got.push_back(std::move(buf));
        }
    };
    const auto block = static_cast<std::size_t>(kRate / 10.0);
    std::vector<std::complex<float>> x(block);
    tp::EngineOutput out;
    for (int b = 0; b < 30; ++b) {
        tx.read(x.data(), block);
        tp::BlockInfo info;
        if (b == 0)
            info.monotonic_ns = 1'000'000'000;
        out.clear();
        engine.process(x.data(), block, kCentre, kRate, info, out);
        drain();
    }
    ::close(rx);

    CHECK(engine.gsmtap_send_failures() == 0);
    const tp::GsmtapCounters c = engine.gsmtap_counters();
    CHECK(got.size() == c.sent);
    CHECK(c.filtered >= 2);  // addressed MM, CMCE
    CHECK(c.encrypted >= 1);

    std::map<uint8_t, int> by_sub;
    int fragments = 0;
    for (const auto& d : got) {
        REQUIRE(d.size() > tp::kGsmtapHeaderBytes);
        CHECK(d[0] == 2);
        CHECK(d[1] == 4);
        CHECK(d[2] == 0x05);
        CHECK(d[3] <= 3);
        CHECK(((d[4] << 8) | d[5]) == 3600);
        ++by_sub[d[12]];
        const uint32_t fn = be32(d, 8);
        const std::vector<uint8_t> bits = unpack(d);
        if (d[12] == tp::kGsmtapBsch) {
            // The header's time is the time in the MAC-SYNC itself.
            const auto s = dec::parse_sync(bits.data());
            REQUIRE(s.has_value());
            CHECK(s->timeslot == d[3] + 1);
            CHECK(s->frame == (fn - 1) % 18 + 1);
            CHECK(s->frame == 18);
            CHECK(s->multiframe == ((fn - s->frame) / 18 - 1) % 60 + 1);
        }
        if (d[12] == tp::kGsmtapSchF)
            for (const dec::BlockPdu& p : dec::parse_block_pdus(bits.data(), 268))
                fragments += p.mac_pdu_type == 1 || p.fragment == dec::Fragment::Start ? 1 : 0;
        check_in_scope(d);
    }
    CHECK(by_sub[tp::kGsmtapBsch] > 0);
    CHECK(by_sub[tp::kGsmtapBnch] > 0);
    CHECK(by_sub[tp::kGsmtapAach] > 0);
    CHECK(by_sub[tp::kGsmtapSchF] > 0);
    CHECK(fragments >= 3);  // the fragmented D-NWRK-BROADCAST arrived whole
}

TEST_CASE("gsmtap: off by default; a name instead of an address is refused")
{
    tp::EngineConfig cfg;
    CHECK_FALSE(cfg.gsmtap.enabled);
    tp::MeasurementEngine off(cfg);
    CHECK(off.gsmtap_counters().sent == 0);
    cfg.gsmtap.enabled = true;
    cfg.gsmtap.host = "localhost";
    CHECK_THROWS_AS(tp::MeasurementEngine{cfg}, std::runtime_error);
}
