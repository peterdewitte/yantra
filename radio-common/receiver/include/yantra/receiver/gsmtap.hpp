// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// GSMTAP output of the TETRA receiver (RAD-115, DEC-12): decoded downlink
// broadcast blocks as GSMTAP version 2 datagrams over UDP, as osmo-tetra
// sends them (type TETRA air interface, sub-type per logical channel,
// timeslot 0..3, frame number ((hyperframe x 60) + multiframe) x 18 +
// frame, the type-1 bits packed MSB first), so that Wireshark's
// GSMTAP/TETRA dissector reads them. For debugging only: nothing of it
// reaches Yantra (RAD-26), and it is off by default.
//
// Scope (RAD-17), applied before anything leaves the receiver: only
// blocks whose every MAC PDU is broadcast or system information are sent
// - BSCH (MAC-SYNC), BNCH (MAC-SYSINFO), the AACH (ACCESS-ASSIGN) and
// signalling blocks made of Null PDUs and MAC-RESOURCE PDUs to the
// broadcast address that carry an MLE D-NWRK-BROADCAST (or its
// extension). A block with an addressed PDU, an encrypted PDU, MM, CMCE
// or SNDCP content, an undecodable PDU or any other MAC PDU is never
// sent; encrypted blocks are only counted. A fragmented TM-SDU is held
// back until its MAC-END and sent only when the whole, reassembled TM-SDU
// is in scope. Destinations are numeric IPv4/IPv6 addresses only (no name
// lookups); default 127.0.0.1:4729.
// Copied from yantra 68ea20a: plugins/tetra/plugin/src/gsmtap.h, adapted
// (namespace, engine setting, no MM PDUs).
#pragma once

#include "yantra/receiver/lower_mac.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yantra::receiver::tetra {

/// Setting keys of the GSMTAP output (the plugins' settings, RAD-115).
inline constexpr const char* kGsmtapEnabledKey = "gsmtap.enabled";
inline constexpr const char* kGsmtapHostKey = "gsmtap.host";
inline constexpr const char* kGsmtapPortKey = "gsmtap.port";

/// The GSMTAP UDP port Wireshark listens for (IANA "gsmtap").
inline constexpr uint16_t kGsmtapDefaultPort = 4729;

// GSMTAP constants (libosmocore gsmtap.h, the values Wireshark knows).
inline constexpr uint8_t kGsmtapVersion = 2;
inline constexpr uint8_t kGsmtapTypeTetra = 0x05;  ///< GSMTAP_TYPE_TETRA_I1
inline constexpr std::size_t kGsmtapHeaderBytes = 16;
enum GsmtapTetraSubType : uint8_t {
    kGsmtapBsch = 0x01,
    kGsmtapAach = 0x02,
    kGsmtapSchHd = 0x04,
    kGsmtapSchF = 0x05,
    kGsmtapBnch = 0x06,
    kGsmtapStch = 0x07,
};

struct GsmtapOptions {
    bool enabled = false;
    std::string host = "127.0.0.1";
    uint16_t port = kGsmtapDefaultPort;
};

/// Reads a setting as text; empty when unset.
using GsmtapSettingFn = std::function<std::optional<std::string>(const char* key)>;

/// The options from settings. `error` tells why a set value was refused
/// (the output then stays off).
GsmtapOptions gsmtap_options(const GsmtapSettingFn& get, std::string& error);

/// True for a numeric IPv4/IPv6 address (no name lookup).
bool is_numeric_address(const std::string& host);
/// True for a numeric loopback address (127.0.0.0/8, ::1).
bool is_loopback_address(const std::string& host);

/// GSMTAP frame number of a TDMA time (osmo-tetra's tetra_tdma_time2fn;
/// hyperframe 0 while unknown).
uint32_t gsmtap_frame_number(const TdmaTime& time);

/// One GSMTAP datagram: header + `nbits` type-1 bits (one per byte) packed
/// MSB first. `sub_slot` 1 for the second half-slot block, else 0.
std::vector<uint8_t> gsmtap_packet(const TdmaTime& time, uint8_t sub_type, uint16_t arfcn, uint8_t sub_slot,
                                   const uint8_t* bits, int nbits);

/// UDP socket to one destination (sends never block; a full socket buffer
/// drops the datagram). Thread-safe: several carriers may share it.
class GsmtapSocket {
public:
    /// std::runtime_error when the host is not a numeric address or the
    /// socket cannot be made.
    GsmtapSocket(const std::string& host, uint16_t port);
    ~GsmtapSocket();
    GsmtapSocket(const GsmtapSocket&) = delete;
    GsmtapSocket& operator=(const GsmtapSocket&) = delete;

    bool send(const std::vector<uint8_t>& datagram);
    uint64_t sent() const { return sent_.load(); }
    uint64_t failed() const { return failed_.load(); }

private:
    intptr_t fd_ = -1;
    std::atomic<uint64_t> sent_{0};
    std::atomic<uint64_t> failed_{0};
};

struct GsmtapCounters {
    uint64_t sent = 0;       ///< datagrams handed to the sink
    uint64_t filtered = 0;   ///< blocks not sent: addressed or out of scope
    uint64_t encrypted = 0;  ///< blocks with an encrypted PDU: counted, never sent
    uint64_t dropped = 0;    ///< held fragments dropped (reassembly lost or out of scope)
    GsmtapCounters& operator+=(const GsmtapCounters& o)
    {
        sent += o.sent;
        filtered += o.filtered;
        encrypted += o.encrypted;
        dropped += o.dropped;
        return *this;
    }
};

/// Applies the scope rules to the decoded timeslots of one carrier and
/// hands the datagrams that may leave to a sink. One per carrier.
class GsmtapFilter {
public:
    using Sink = std::function<void(const std::vector<uint8_t>& datagram)>;
    explicit GsmtapFilter(Sink sink) : sink_(std::move(sink)) {}

    /// One decoded timeslot; `arfcn` is the carrier number (0 unknown).
    void slot(const SlotResult& r, uint16_t arfcn);
    /// Sync lost or decoder restarted: held fragments are dropped.
    void reset();
    const GsmtapCounters& counters() const { return counters_; }

    /// The in-scope content rule for one MAC-RESOURCE PDU (complete or
    /// reassembled TM-SDU).
    static bool content_in_scope(const BlockPdu& p);

private:
    enum class Verdict { Send, Hold, Drop };
    Verdict judge(const DecodedBlock& b, int ts);
    void send(std::vector<uint8_t> d);
    void drop_held(int ts);

    Sink sink_;
    std::array<std::vector<std::vector<uint8_t>>, 4> held_{};  ///< per timeslot: fragments waiting for their MAC-END
    std::array<bool, 4> chain_{};                              ///< a fragmented TM-SDU in scope so far
    GsmtapCounters counters_;
};

}  // namespace yantra::receiver::tetra
