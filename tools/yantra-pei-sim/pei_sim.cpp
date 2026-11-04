// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulated TETRA terminal (see pei_sim.hpp).
#include "pei_sim.hpp"

#include <yantra/radios/tetra_schemas.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace yantra::pei_sim {

namespace {

std::string upper_of(const std::string& s)
{
    std::string u = s;
    for (char& c : u)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
}

std::string base_of(const std::string& upper)
{
    std::string b = upper;
    while (!b.empty() && (b.back() == '?' || b.back() == ' '))
        b.pop_back();
    return b;
}

const char* const kCrlf = "\r\n";

std::string line(const std::string& s)
{
    return s + kCrlf;
}

int rounded(double x)
{
    return static_cast<int>(std::lround(x));
}

} // namespace

Terminal::Terminal(gen::Scenario s)
    : engine_(std::make_unique<gen::ScenarioEngine>(std::move(s))), start_(std::chrono::steady_clock::now())
{
}

Terminal::~Terminal() = default;

double Terminal::now() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (fixed_t_)
        return *fixed_t_;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

void Terminal::set_time(double t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    fixed_t_ = t;
}

void Terminal::set_error(const std::string& command, bool on)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string b = base_of(upper_of(command));
    if (on)
        error_.insert(b);
    else
        error_.erase(b);
}

void Terminal::set_silent(const std::string& command, bool on)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string b = base_of(upper_of(command));
    if (on)
        silent_.insert(b);
    else
        silent_.erase(b);
}

void Terminal::set_noise(bool on)
{
    std::lock_guard<std::mutex> lock(mutex_);
    noise_ = on;
}

bool Terminal::echo() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return echo_;
}

std::vector<std::string> Terminal::commands() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_;
}

Snapshot Terminal::snapshot(double t) const
{
    Snapshot out;
    const gen::Scenario& sc = engine_->scenario();
    const std::vector<gen::CellTruth> cells = engine_->levels_at(engine_->receiver_at(t), t);
    auto report = [&](const gen::CellTruth& c) {
        const gen::ScenarioCell* def = sc.find_cell(c.cell);
        ReportedCell r;
        r.id = c.cell;
        if (def != nullptr) {
            r.carrier = def->main_carrier;
            r.band = def->frequency_band;
            r.offset = def->offset;
        }
        r.mcc = c.mcc;
        r.mnc = c.mnc;
        r.la = c.location_area;
        r.cc = c.colour_code;
        r.frequency_hz = static_cast<uint64_t>(std::llround(c.frequency_hz));
        r.rssi_dbm = rounded(c.level_dbm);
        const double rxlev_min = def != nullptr ? -125.0 + 5.0 * def->rxlev_access_min : -125.0;
        const double txpwr_max = def != nullptr ? 15.0 + 5.0 * (def->ms_txpwr_max_cell - 1) : 30.0;
        r.c1_db = rounded(radios::tetra_c1_db(r.rssi_dbm, rxlev_min, txpwr_max));
        return r;
    };
    const gen::CellTruth* best = nullptr;
    for (const gen::CellTruth& c : cells)
        if (c.on_air && std::isfinite(c.level_dbm) && (best == nullptr || c.level_dbm > best->level_dbm))
            best = &c;
    if (best == nullptr)
        return out;
    out.serving = report(*best);
    out.csq_code = std::clamp(rounded((out.serving->rssi_dbm + 113.0) / 2.0), 0, 31);
    const gen::ScenarioCell* def = sc.find_cell(best->cell);
    if (def == nullptr)
        return out;
    for (const gen::ScenarioNeighbour& n : def->neighbours)
        for (const gen::CellTruth& c : cells)
            if (c.cell == n.cell && c.on_air && std::isfinite(c.level_dbm))
                out.neighbours.push_back(report(c));
    return out;
}

std::string Terminal::handle(const std::string& raw)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::string cmd = raw;
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n' || cmd.back() == ' '))
        cmd.pop_back();
    while (!cmd.empty() && (cmd.front() == '\n' || cmd.front() == ' '))
        cmd.erase(cmd.begin());
    if (cmd.empty())
        return "";
    commands_.push_back(cmd);
    const std::string upper = upper_of(cmd);
    const double t = fixed_t_ ? *fixed_t_ : std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    std::string out;
    if (echo_)
        out += cmd + "\r";
    if (silent_.count(base_of(upper)) != 0)
        return out;
    if (noise_)
        out += std::string("\x01\xfe#!") + kCrlf + line("+YNOTE: 1");
    if (error_.count(base_of(upper)) != 0)
        return out + kCrlf + line("+CME ERROR: 4");
    return out + reply_locked(cmd, upper, t);
}

std::string Terminal::reply_locked(const std::string& cmd, const std::string& upper, double t)
{
    (void)cmd;
    const std::string ok = std::string(kCrlf) + line("OK");
    if (upper == "AT")
        return ok;
    if (upper == "ATE0" || upper == "ATE1") {
        echo_ = upper == "ATE1";
        return ok;
    }
    if (upper == "AT+GMI")
        return std::string(kCrlf) + line(kManufacturer) + ok;
    if (upper == "AT+GMM")
        return std::string(kCrlf) + line(kModel) + ok;
    if (upper == "AT+GMR")
        return std::string(kCrlf) + line(kFirmware) + ok;
    if (upper == "AT+GSN")
        return std::string(kCrlf) + line(kTei) + ok;
    // Readings (snapshot() takes no lock; the engine is only read).
    const auto snap = [&]() { return snapshot(t); };
    char buf[256];
    if (upper == "AT+CSQ") {
        const Snapshot s = snap();
        std::snprintf(buf, sizeof buf, "+CSQ: %d,99", s.csq_code);
        return std::string(kCrlf) + line(buf) + ok;
    }
    if (upper == "AT+CREG?") {
        const Snapshot s = snap();
        if (!s.serving)
            return std::string(kCrlf) + line("+CREG: 2") + ok;
        std::snprintf(buf, sizeof buf, "+CREG: 1,%u,%03u%05u", s.serving->la, s.serving->mcc, s.serving->mnc);
        return std::string(kCrlf) + line(buf) + ok;
    }
    if (upper == "AT+YCELL?") {
        const Snapshot s = snap();
        std::string r = kCrlf;
        if (s.serving) {
            const ReportedCell& c = *s.serving;
            std::snprintf(buf, sizeof buf, "+YCELL: %u,%u,%u,%u,%u,%u,%u,%d,%d,\"camped\"", c.carrier, c.band,
                          c.offset, c.mcc, c.mnc, c.la, c.cc, c.rssi_dbm, c.c1_db);
            r += line(buf);
        }
        return r + ok;
    }
    if (upper == "AT+YNCELL?") {
        const Snapshot s = snap();
        std::string r = kCrlf;
        for (const ReportedCell& c : s.neighbours) {
            std::snprintf(buf, sizeof buf, "+YNCELL: %u,%u,%u,%u,%u,%u,%u,%d,%d", c.carrier, c.band, c.offset, c.mcc,
                          c.mnc, c.la, c.cc, c.rssi_dbm, c.c1_db);
            r += line(buf);
        }
        return r + ok;
    }
    return std::string(kCrlf) + line("ERROR");
}

// ------------------------------------------------------------ PtyTerminal

PtyTerminal::PtyTerminal(Terminal& terminal, std::string link) : terminal_(terminal), link_(std::move(link))
{
    replug();
}

PtyTerminal::~PtyTerminal()
{
    unplug();
}

void PtyTerminal::replug()
{
    if (master_ >= 0)
        return;
    const int m = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        if (m >= 0)
            close(m);
        throw std::runtime_error(std::string("no pseudo-terminal: ") + std::strerror(errno));
    }
    const char* name = ptsname(m);
    if (name == nullptr) {
        close(m);
        throw std::runtime_error("ptsname failed");
    }
    device_ = name;
    // A slave kept open: the master never sees a hang-up when the plugin
    // closes its side, and the slave is raw from the start.
    const int s = open(device_.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (s < 0) {
        close(m);
        throw std::runtime_error("cannot open " + device_ + ": " + std::strerror(errno));
    }
    termios tio{};
    if (tcgetattr(s, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(s, TCSANOW, &tio);
    }
    if (pipe2(wake_, O_CLOEXEC) != 0) {
        close(s);
        close(m);
        throw std::runtime_error("pipe failed");
    }
    ::unlink(link_.c_str());
    if (::symlink(device_.c_str(), link_.c_str()) != 0) {
        const std::string why = std::strerror(errno);
        close(wake_[0]);
        close(wake_[1]);
        close(s);
        close(m);
        throw std::runtime_error("cannot link " + link_ + ": " + why);
    }
    master_ = m;
    slave_ = s;
    thread_ = std::thread([this] { serve(); });
}

void PtyTerminal::unplug()
{
    if (master_ < 0)
        return;
    ::unlink(link_.c_str());
    const char c = 'x';
    (void)!write(wake_[1], &c, 1);
    thread_.join();
    close(wake_[0]);
    close(wake_[1]);
    wake_[0] = wake_[1] = -1;
    close(slave_);
    close(master_);
    slave_ = master_ = -1;
}

void PtyTerminal::serve()
{
    std::string pending;
    char buf[512];
    for (;;) {
        pollfd fds[2] = {{master_, POLLIN, 0}, {wake_[0], POLLIN, 0}};
        const int r = poll(fds, 2, -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (fds[1].revents != 0)
            return;
        if ((fds[0].revents & POLLIN) == 0) {
            if ((fds[0].revents & (POLLHUP | POLLERR)) != 0) {
                // No slave open other than ours cannot happen; wait a bit.
                poll(fds + 1, 1, 20);
            }
            continue;
        }
        const ssize_t n = read(master_, buf, sizeof buf);
        if (n <= 0)
            continue;
        pending.append(buf, static_cast<size_t>(n));
        for (;;) {
            const size_t end = pending.find('\r');
            if (end == std::string::npos)
                break;
            const std::string cmd = pending.substr(0, end);
            pending.erase(0, end + 1);
            const std::string reply = terminal_.handle(cmd);
            if (reply.empty())
                continue;
            if (const int ms = delay_ms_.load(); ms > 0) {
                pollfd w = {wake_[0], POLLIN, 0};
                if (poll(&w, 1, ms) > 0)
                    return;
            }
            size_t off = 0;
            while (off < reply.size()) {
                const ssize_t w = write(master_, reply.data() + off, reply.size() - off);
                if (w < 0) {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    break;
                }
                off += static_cast<size_t>(w);
            }
        }
        if (pending.size() > 4096)
            pending.clear();
    }
}

} // namespace yantra::pei_sim
