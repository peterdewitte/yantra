// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A simulated TETRA terminal for the terminal plugin's tests (RAD-80,
// TRM-01, TRM-04). Terminal answers PEI command lines from a scenario: the
// serving cell is the strongest cell on air at the receiver (the
// generator's levels_at, no IQ), the neighbours are its scenario neighbour
// list entries that are on air. It speaks EN 300 392-5 (AT+CSQ, AT+CREG?,
// AT+GMI/GMM/GMR/GSN) and the example vendor cell lists AT+YCELL? and
// AT+YNCELL? of docs/RADIO_PLUGINS.md. PtyTerminal serves it on a
// pseudo-terminal behind a symlink that can be unplugged and replugged.
#pragma once

#include <yantra/gen/scenario.hpp>
#include <yantra/gen/scenario_engine.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace yantra::pei_sim {

/// A cell as the terminal reports it (integer dB, like real terminals).
struct ReportedCell {
    std::string id;
    uint32_t carrier = 0, band = 3, offset = 0;
    uint32_t mcc = 0, mnc = 0, la = 0, cc = 0;
    uint64_t frequency_hz = 0;
    int rssi_dbm = 0;
    int c1_db = 0;
};

/// What the terminal reports at one time.
struct Snapshot {
    std::optional<ReportedCell> serving;
    std::vector<ReportedCell> neighbours;
    int csq_code = 99;  ///< AT+CSQ: 0..31, 99 unknown
};

/// The made-up radio info (TRM-04).
inline constexpr const char* kManufacturer = "Yantra";
inline constexpr const char* kModel = "PEI simulator";
inline constexpr const char* kFirmware = "0.1.0";
inline constexpr const char* kTei = "100000000000001";

class Terminal {
public:
    explicit Terminal(gen::Scenario s);
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    /// The reply bytes to one command line (CR stripped): the echo while
    /// echo is on, the information lines and the final result code, each
    /// ending in CR LF; "" for a silent command.
    std::string handle(const std::string& line);
    /// What the terminal reports at scenario time t.
    Snapshot snapshot(double t) const;
    /// Scenario time now: wall seconds since construction, or the time
    /// fixed by set_time.
    double now() const;
    void set_time(double t);

    /// Faults: commands answered with +CME ERROR (error) or not at all
    /// (silent), matched in upper case without the trailing '?'.
    void set_error(const std::string& command, bool on);
    void set_silent(const std::string& command, bool on);
    /// Garbage bytes and an unsolicited line before every reply.
    void set_noise(bool on);
    /// Echo state (on after power-up, ATE0 turns it off).
    bool echo() const;
    /// Every command line received so far.
    std::vector<std::string> commands() const;

private:
    std::string reply_locked(const std::string& cmd, const std::string& upper, double t);
    mutable std::mutex mutex_;
    std::unique_ptr<gen::ScenarioEngine> engine_;
    std::chrono::steady_clock::time_point start_;
    std::optional<double> fixed_t_;
    std::set<std::string> error_, silent_;
    bool noise_ = false;
    bool echo_ = true;
    std::vector<std::string> commands_;
};

/// The terminal on a pseudo-terminal: `link` is a symlink to the slave
/// side, which the plugin opens as its serial port. unplug() removes the
/// link and closes the pseudo-terminal (the plugin reads EOF/EIO), replug()
/// makes a new one behind the same link.
class PtyTerminal {
public:
    PtyTerminal(Terminal& terminal, std::string link);
    ~PtyTerminal();
    PtyTerminal(const PtyTerminal&) = delete;
    PtyTerminal& operator=(const PtyTerminal&) = delete;

    bool plugged() const { return master_ >= 0; }
    const std::string& link() const { return link_; }
    /// The pseudo-terminal behind the link (/dev/pts/N).
    const std::string& device() const { return device_; }
    void unplug();
    /// std::runtime_error when no pseudo-terminal can be made.
    void replug();
    /// Milliseconds waited before each reply (a slow terminal).
    void set_reply_delay_ms(int ms) { delay_ms_.store(ms); }

private:
    void serve();
    Terminal& terminal_;
    std::string link_;
    std::string device_;
    int master_ = -1;
    int slave_ = -1;
    int wake_[2] = {-1, -1};
    std::atomic<int> delay_ms_{0};
    std::thread thread_;
};

} // namespace yantra::pei_sim
