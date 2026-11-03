// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/gsmtap.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>

namespace yantra::receiver::tetra {
namespace {

#ifdef _WIN32
using ssize_type = int;
constexpr int kSendFlags = 0;
void close_socket(intptr_t fd) { ::closesocket(static_cast<SOCKET>(fd)); }
#else
using ssize_type = ssize_t;
constexpr int kSendFlags = MSG_DONTWAIT | MSG_NOSIGNAL;
void close_socket(intptr_t fd) { ::close(static_cast<int>(fd)); }
#endif

/// Held fragments per timeslot at most; a longer chain is dropped (a
/// TM-SDU of the reassembly limit fits in far fewer blocks).
constexpr std::size_t kMaxHeld = 64;

// MLE protocol discriminator and the PDU types sent (EN 300 392-2
// 18.5.21, 18.4.1.4). MM PDUs are never sent (RAD-17: broadcast and system
// information only).
constexpr uint8_t kPdiscMle = 5;
constexpr uint8_t kMleNwrkBroadcast = 2;
constexpr uint8_t kMleNwrkBroadcastExt = 3;

std::optional<bool> parse_bool(const std::string& v)
{
    if (v == "1" || v == "true" || v == "on" || v == "yes")
        return true;
    if (v == "0" || v == "false" || v == "off" || v == "no" || v.empty())
        return false;
    return std::nullopt;
}

/// Resolves a numeric address only (never a name lookup).
struct Address {
    sockaddr_storage addr{};
    socklen_t len = 0;
    int family = 0;
};

std::optional<Address> numeric_address(const std::string& host, uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* res = nullptr;
    const std::string service = std::to_string(port);
    if (host.empty() || getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || res == nullptr)
        return std::nullopt;
    Address a;
    if (res->ai_addrlen <= sizeof a.addr) {
        std::copy_n(reinterpret_cast<const unsigned char*>(res->ai_addr), res->ai_addrlen,
                    reinterpret_cast<unsigned char*>(&a.addr));
        a.len = res->ai_addrlen;
        a.family = res->ai_family;
    }
    freeaddrinfo(res);
    if (a.len == 0)
        return std::nullopt;
    return a;
}

uint8_t sub_type_of(LogicalChannel c)
{
    switch (c) {
    case LogicalChannel::Bsch: return kGsmtapBsch;
    case LogicalChannel::Bnch: return kGsmtapBnch;
    case LogicalChannel::SchF: return kGsmtapSchF;
    case LogicalChannel::SchHd: return kGsmtapSchHd;
    case LogicalChannel::Stch: return kGsmtapStch;
    case LogicalChannel::Aach: return kGsmtapAach;
    default: return 0;
    }
}

}  // namespace

GsmtapOptions gsmtap_options(const GsmtapSettingFn& get, std::string& error)
{
    GsmtapOptions o;
    error.clear();
    if (!get)
        return o;
    if (const auto v = get(kGsmtapEnabledKey)) {
        const auto on = parse_bool(*v);
        if (!on) {
            error = std::string(kGsmtapEnabledKey) + ": expected 1 or 0";
            return o;
        }
        o.enabled = *on;
    }
    if (const auto v = get(kGsmtapHostKey); v && !v->empty()) {
        if (!numeric_address(*v, kGsmtapDefaultPort)) {
            error = std::string(kGsmtapHostKey) + ": not a numeric IPv4 or IPv6 address";
            o.enabled = false;
            return o;
        }
        o.host = *v;
    }
    if (const auto v = get(kGsmtapPortKey); v && !v->empty()) {
        char* end = nullptr;
        errno = 0;
        const long port = std::strtol(v->c_str(), &end, 10);
        if (errno != 0 || end == v->c_str() || *end != '\0' || port < 1 || port > 65535) {
            error = std::string(kGsmtapPortKey) + ": expected a port number 1..65535";
            o.enabled = false;
            return o;
        }
        o.port = static_cast<uint16_t>(port);
    }
    return o;
}

bool is_numeric_address(const std::string& host) { return numeric_address(host, kGsmtapDefaultPort).has_value(); }

bool is_loopback_address(const std::string& host)
{
    const auto a = numeric_address(host, kGsmtapDefaultPort);
    if (!a)
        return false;
    if (a->family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(&a->addr);
        return (ntohl(in->sin_addr.s_addr) >> 24) == 127;
    }
    if (a->family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(&a->addr);
        if (IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr))
            return true;
        if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr))
            return in6->sin6_addr.s6_addr[12] == 127;
    }
    return false;
}

uint32_t gsmtap_frame_number(const TdmaTime& t)
{
    const uint32_t hn = t.hyperframe_known ? t.hyperframe : 0u;
    return ((hn * 60u) + t.multiframe) * 18u + t.frame;
}

std::vector<uint8_t> gsmtap_packet(const TdmaTime& time, uint8_t sub_type, uint16_t arfcn, uint8_t sub_slot,
                                   const uint8_t* bits, int nbits)
{
    const std::size_t n = nbits > 0 ? static_cast<std::size_t>(nbits) : 0u;
    std::vector<uint8_t> d(kGsmtapHeaderBytes + (n + 7) / 8, 0);
    d[0] = kGsmtapVersion;
    d[1] = static_cast<uint8_t>(kGsmtapHeaderBytes / 4);
    d[2] = kGsmtapTypeTetra;
    d[3] = static_cast<uint8_t>(std::clamp<int>(time.timeslot, 1, 4) - 1);  // 0..3 as osmo-tetra
    d[4] = static_cast<uint8_t>((arfcn >> 8) & 0x3F);  // no uplink/PCS flag bits
    d[5] = static_cast<uint8_t>(arfcn & 0xFF);
    d[6] = 0;  // signal dBm: levels are in the records (calibrated by the host)
    d[7] = 0;  // SNR: idem
    const uint32_t fn = gsmtap_frame_number(time);
    d[8] = static_cast<uint8_t>(fn >> 24);
    d[9] = static_cast<uint8_t>(fn >> 16);
    d[10] = static_cast<uint8_t>(fn >> 8);
    d[11] = static_cast<uint8_t>(fn);
    d[12] = sub_type;
    d[13] = 0;  // antenna
    d[14] = sub_slot;
    d[15] = 0;
    for (std::size_t k = 0; k < n; ++k)
        if (bits[k] & 1u)
            d[kGsmtapHeaderBytes + k / 8] |= static_cast<uint8_t>(0x80u >> (k % 8));
    return d;
}

GsmtapSocket::GsmtapSocket(const std::string& host, uint16_t port)
{
    const auto a = numeric_address(host, port);
    if (!a)
        throw std::runtime_error("gsmtap: not a numeric address: " + host);
#ifdef _WIN32
    const SOCKET s = ::socket(a->family, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        throw std::runtime_error("gsmtap: no UDP socket");
    u_long nonblocking = 1;
    ::ioctlsocket(s, FIONBIO, &nonblocking);
    fd_ = static_cast<intptr_t>(s);
#else
    fd_ = ::socket(a->family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0)
        throw std::runtime_error("gsmtap: no UDP socket");
#endif
    // Connected: send() needs no address, and nothing is sent by connect.
    if (::connect(static_cast<decltype(::socket(0, 0, 0))>(fd_), reinterpret_cast<const sockaddr*>(&a->addr),
                  a->len) != 0) {
        close_socket(fd_);
        fd_ = -1;
        throw std::runtime_error("gsmtap: cannot use the address " + host);
    }
}

GsmtapSocket::~GsmtapSocket()
{
    if (fd_ >= 0)
        close_socket(fd_);
}

bool GsmtapSocket::send(const std::vector<uint8_t>& datagram)
{
    const auto n = static_cast<ssize_type>(::send(static_cast<decltype(::socket(0, 0, 0))>(fd_),
                                                  reinterpret_cast<const char*>(datagram.data()),
                                                  static_cast<int>(datagram.size()), kSendFlags));
    if (n == static_cast<ssize_type>(datagram.size())) {
        sent_.fetch_add(1);
        return true;
    }
    failed_.fetch_add(1);
    return false;
}

bool GsmtapFilter::content_in_scope(const BlockPdu& p)
{
    if (p.mac_pdu_type != 0 || p.null_pdu || p.encrypted || !p.broadcast)
        return false;
    if (p.sdu_status != 0 || p.fcs == Fcs::Error || !p.pdisc || !p.pdu_type)
        return false;
    return *p.pdisc == kPdiscMle && (*p.pdu_type == kMleNwrkBroadcast || *p.pdu_type == kMleNwrkBroadcastExt);
}

void GsmtapFilter::send(std::vector<uint8_t> d)
{
    ++counters_.sent;
    if (sink_)
        sink_(d);
}

void GsmtapFilter::drop_held(int ts)
{
    counters_.dropped += held_[ts].size();
    held_[ts].clear();
    chain_[ts] = false;
}

void GsmtapFilter::reset()
{
    for (int ts = 0; ts < 4; ++ts)
        drop_held(ts);
}

GsmtapFilter::Verdict GsmtapFilter::judge(const DecodedBlock& b, int ts)
{
    switch (b.channel) {
    case LogicalChannel::Bsch:
        if (b.sync && b.mac.kind == MacPduKind::MacSync)
            return Verdict::Send;
        ++counters_.filtered;
        return Verdict::Drop;
    case LogicalChannel::Bnch:
        if (b.sysinfo && b.mac.kind == MacPduKind::MacSysinfo)
            return Verdict::Send;
        ++counters_.filtered;
        return Verdict::Drop;
    case LogicalChannel::SchF:
    case LogicalChannel::SchHd:
    case LogicalChannel::Stch: break;
    default: ++counters_.filtered; return Verdict::Drop;
    }

    const BlockPdu* whole = nullptr;  // reassembled TM-SDU ending here
    std::vector<const BlockPdu*> pdus;
    for (const BlockPdu& p : b.pdus) {
        if (p.reassembled)
            whole = &p;
        else
            pdus.push_back(&p);
    }
    bool start = false, frag = false, end = false, encrypted = b.mac.encrypted, scope = !pdus.empty();
    if (scope && !pdus.back()->last)
        scope = false;  // parsing stopped early: the rest is unknown
    for (const BlockPdu* p : pdus) {
        encrypted = encrypted || p->encrypted;
        if (p->mac_pdu_type == 0) {
            if (p->null_pdu)
                continue;
            if (!p->broadcast || p->encrypted)
                scope = false;
            else if (p->fragment == Fragment::Start)
                start = true;
            else if (!content_in_scope(*p))
                scope = false;
        } else if (p->mac_pdu_type == 1) {
            (p->fragment == Fragment::End ? end : frag) = true;
            if (!chain_[ts])
                scope = false;
        } else {
            scope = false;  // MAC-D-BLCK, broadcast/supplementary types: not sent
        }
    }
    if (encrypted || !scope || (end && (start || !whole || !content_in_scope(*whole)))) {
        if (start || frag || end || encrypted)
            drop_held(ts);
        ++(encrypted ? counters_.encrypted : counters_.filtered);
        return Verdict::Drop;
    }
    if (end) {
        for (std::vector<uint8_t>& d : held_[ts])
            send(std::move(d));
        held_[ts].clear();
        chain_[ts] = false;
        return Verdict::Send;
    }
    if (start) {
        drop_held(ts);  // a new start ends the previous TM-SDU
        chain_[ts] = true;
        return Verdict::Hold;
    }
    if (frag) {
        if (held_[ts].size() >= kMaxHeld) {
            drop_held(ts);
            ++counters_.filtered;
            return Verdict::Drop;
        }
        return Verdict::Hold;
    }
    return Verdict::Send;
}

void GsmtapFilter::slot(const SlotResult& r, uint16_t arfcn)
{
    if (!r.time_known)
        return;  // no frame number yet
    const int ts = std::clamp<int>(r.time.timeslot, 1, 4) - 1;
    for (const DecodedBlock& b : r.blocks) {
        if (b.channel == LogicalChannel::Tch)
            continue;  // never decoded, never sent (spec 16.5)
        if (!b.crc_ok) {
            if (b.channel != LogicalChannel::Bsch && b.channel != LogicalChannel::Bnch)
                drop_held(ts);  // a fragment may be lost
            continue;
        }
        const Verdict v = judge(b, ts);
        if (v == Verdict::Drop)
            continue;
        const uint8_t sub_slot = b.block == 2 && b.channel != LogicalChannel::Bnch ? 1 : 0;
        std::vector<uint8_t> d = gsmtap_packet(r.time, sub_type_of(b.channel), arfcn, sub_slot, b.bits.data(),
                                               std::clamp(b.type1_bits, 0, static_cast<int>(b.bits.size())));
        if (v == Verdict::Hold)
            held_[ts].push_back(std::move(d));
        else
            send(std::move(d));
    }
    if (r.aach_present && r.aach.decoded)
        send(gsmtap_packet(r.time, kGsmtapAach, arfcn, 0, r.aach.bits.data(), static_cast<int>(r.aach.bits.size())));
}

}  // namespace yantra::receiver::tetra
