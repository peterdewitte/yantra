// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v1 c24e8f5: core/engine/api_auth.cpp, adapted for PLG-24.
#include "api_auth.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <winsock2.h>
#include <ws2tcpip.h>
// winsock2.h before windows.h (for bcrypt.h)
#include <windows.h>
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>


namespace fs = std::filesystem;

namespace yantra::probe {

namespace {

// The whole file when it is at most max_bytes long.
bool read_file(const std::filesystem::path& path, std::string& out, std::size_t max_bytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad() || text.size() > max_bytes)
        return false;
    out = std::move(text);
    return true;
}

}  // namespace

const char* access_name(Access a)
{
    switch (a) {
    case Access::Full: return "full";
    case Access::Read: return "read";
    case Access::None: break;
    }
    return "none";
}

std::string generate_token()
{
    uint8_t b[32];
#if defined(_WIN32)
    if (BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("BCryptGenRandom failed");
#else
    size_t got = 0;
    while (got < sizeof b) {
        const ssize_t n = getrandom(b + got, sizeof b - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw std::runtime_error(std::string("getrandom: ") + std::strerror(errno));
        }
        got += static_cast<size_t>(n);
    }
#endif
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t v : b) {
        s += hex[v >> 4];
        s += hex[v & 15];
    }
    return s;
}

bool constant_time_equal(const std::string& a, const std::string& b)
{
    // The length is not secret (fixed 64 digits); the contents are.
    unsigned diff = a.size() == b.size() ? 0u : 1u;
    const size_t n = std::max(a.size(), b.size());
    for (size_t k = 0; k < n; ++k) {
        const unsigned ca = k < a.size() ? static_cast<unsigned char>(a[k]) : 0u;
        const unsigned cb = k < b.size() ? static_cast<unsigned char>(b[k]) : 0u;
        diff |= ca ^ cb;
    }
    return diff == 0;
}

std::optional<std::string> bearer_token(const std::string& authorization)
{
    size_t p = 0;
    while (p < authorization.size() && authorization[p] == ' ')
        ++p;
    static const char kScheme[] = "bearer";
    if (authorization.size() < p + 7)
        return std::nullopt;
    for (size_t k = 0; k < 6; ++k)
        if (std::tolower(static_cast<unsigned char>(authorization[p + k])) != kScheme[k])
            return std::nullopt;
    p += 6;
    if (authorization[p] != ' ')
        return std::nullopt;
    while (p < authorization.size() && authorization[p] == ' ')
        ++p;
    std::string t = authorization.substr(p);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t'))
        t.pop_back();
    if (t.empty() || t.find_first_of(" \t") != std::string::npos)
        return std::nullopt;
    return t;
}

// ---------------------------------------------------------------- addresses

namespace {

// IPv4 as a host-order number; IPv4-mapped IPv6 included.
std::optional<uint32_t> ipv4(const std::string& ip)
{
    in_addr a4{};
    if (inet_pton(AF_INET, ip.c_str(), &a4) == 1)
        return ntohl(a4.s_addr);
    in6_addr a6{};
    if (inet_pton(AF_INET6, ip.c_str(), &a6) == 1) {
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (std::memcmp(a6.s6_addr, mapped, 12) == 0)
            return (uint32_t(a6.s6_addr[12]) << 24) | (uint32_t(a6.s6_addr[13]) << 16) |
                   (uint32_t(a6.s6_addr[14]) << 8) | uint32_t(a6.s6_addr[15]);
    }
    return std::nullopt;
}

std::optional<in6_addr> ipv6(const std::string& ip)
{
    in6_addr a6{};
    if (inet_pton(AF_INET6, ip.c_str(), &a6) == 1)
        return a6;
    return std::nullopt;
}

bool in_net(uint32_t a, uint32_t net, int bits) { return (a >> (32 - bits)) == (net >> (32 - bits)); }

}  // namespace

bool is_loopback_address(const std::string& ip)
{
    if (const auto v4 = ipv4(ip))
        return in_net(*v4, 0x7f000000u, 8);
    if (const auto v6 = ipv6(ip)) {
        static const in6_addr lo = IN6ADDR_LOOPBACK_INIT;
        return std::memcmp(v6->s6_addr, lo.s6_addr, 16) == 0;
    }
    return false;
}

bool is_lan_address(const std::string& ip)
{
    if (is_loopback_address(ip))
        return true;
    if (const auto v4 = ipv4(ip))
        return in_net(*v4, 0x0a000000u, 8) || in_net(*v4, 0xac100000u, 12) || in_net(*v4, 0xc0a80000u, 16) ||
               in_net(*v4, 0xa9fe0000u, 16) || in_net(*v4, 0x64400000u, 10);
    if (const auto v6 = ipv6(ip)) {
        const uint8_t b0 = v6->s6_addr[0], b1 = v6->s6_addr[1];
        return (b0 == 0xfe && (b1 & 0xc0) == 0x80) || (b0 & 0xfe) == 0xfc;  // fe80::/10, fc00::/7
    }
    return false;
}

// ---------------------------------------------------------------- tokens


bool TokenStore::load_locked(std::string* error)
{
    struct stat st{};
    if (::stat(file_.string().c_str(), &st) != 0) {
        if (error)
            *error = "cannot read " + file_.string() + ": " + std::strerror(errno);
        return false;
    }
#if !defined(_WIN32)  // Windows: no mode bits; the profile folder's permissions apply
    if (st.st_mode & 077) {
        if (error)
            *error = file_.string() + " can be read by other users: chmod 600 it (or delete it for new tokens)";
        return false;
    }
#endif
    std::string text;
    if (!read_file(file_, text, 64 * 1024)) {
        if (error)
            *error = "cannot read " + file_.string();
        return false;
    }
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("format", "") != kTokenFormat || !j.contains("token") || !j["token"].is_string() ||
        j["token"].get<std::string>().size() < 32) {
        if (error)
            *error = file_.string() + " is not a valid token file";
        return false;
    }
    if (j.value("version", 0) > kTokenVersion) {
        if (error)
            *error = file_.string() + " was written by a newer version";
        return false;
    }
    token_ = j["token"].get<std::string>();
    lan_ = j.contains("lan_token") && j["lan_token"].is_string() ? j["lan_token"].get<std::string>() : "";
    std::error_code ec;
    mtime_ = fs::last_write_time(file_, ec);
    return true;
}

bool TokenStore::save_locked(std::string* error)
{
    auto fail = [&](const std::string& what) {
        if (error)
            *error = what + ": " + std::strerror(errno);
        return false;
    };
    const fs::path dir = file_.parent_path().empty() ? fs::path(".") : file_.parent_path();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        if (ec) {
            if (error)
                *error = "cannot create " + dir.string() + ": " + ec.message();
            return false;
        }
#if !defined(_WIN32)
        ::chmod(dir.c_str(), 0700);
#endif
    }
    nlohmann::json j = {{"format", kTokenFormat},
                        {"version", kTokenVersion},
                        {"token", token_},
                        {"lan_token", lan_.empty() ? nlohmann::json(nullptr) : nlohmann::json(lan_)}};
    const std::string data = j.dump(2) + "\n";
#if defined(_WIN32)
    // Windows: the file lies in the user's own profile folder
    // (%APPDATA%), whose permissions keep other users out (no mode bits).
    fs::path tmp = file_;
    tmp += ".tmp";
    fs::remove(tmp, ec);
    const int fd = ::_wopen(tmp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                            _S_IREAD | _S_IWRITE);
    if (fd < 0)
        return fail("cannot write " + tmp.string());
    size_t off = 0;
    while (off < data.size()) {
        const int n = ::_write(fd, data.data() + off, static_cast<unsigned>(data.size() - off));
        if (n < 0) {
            ::_close(fd);
            fs::remove(tmp, ec);
            return fail("cannot write " + tmp.string());
        }
        off += static_cast<size_t>(n);
    }
    ::_commit(fd);
    ::_close(fd);
    if (MoveFileExW(tmp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        fs::remove(tmp, ec);
        if (error)
            *error = "cannot replace " + file_.string();
        return false;
    }
#else
    const fs::path tmp = file_.string() + ".tmp";
    ::unlink(tmp.c_str());
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return fail("cannot write " + tmp.string());
    ::fchmod(fd, 0600);  // whatever the umask
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ::close(fd);
            ::unlink(tmp.c_str());
            return fail("cannot write " + tmp.string());
        }
        off += static_cast<size_t>(n);
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(tmp.c_str(), file_.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return fail("cannot replace " + file_.string());
    }
#endif
    mtime_ = fs::last_write_time(file_, ec);
    return true;
}

bool TokenStore::load_or_create(std::string* error, bool* created)
{
    std::lock_guard<std::mutex> lk(m_);
    if (created)
        *created = false;
    std::error_code ec;
    if (fs::exists(file_, ec))
        return load_locked(error);
    token_ = generate_token();
    lan_.clear();
    if (!save_locked(error))
        return false;
    if (created)
        *created = true;
    return true;
}

bool TokenStore::rotate(std::string* error)
{
    std::lock_guard<std::mutex> lk(m_);
    token_ = generate_token();
    return save_locked(error);
}

bool TokenStore::set_lan(bool enable, std::string* error)
{
    std::lock_guard<std::mutex> lk(m_);
    lan_ = enable ? generate_token() : std::string();
    return save_locked(error);
}

void TokenStore::reload_if_changed_locked()
{
    std::error_code ec;
    const auto t = fs::last_write_time(file_, ec);
    if (ec || t == mtime_)
        return;
    const std::string old_token = token_, old_lan = lan_;
    std::string err;
    if (!load_locked(&err)) {  // keep the last good tokens
        token_ = old_token;
        lan_ = old_lan;
        mtime_ = t;
    }
}

Access TokenStore::check(const std::string& token, const std::string& client_ip)
{
    std::lock_guard<std::mutex> lk(m_);
    reload_if_changed_locked();
    if (token.empty())
        return Access::None;
    // Both comparisons always run (no early exit on the owner token).
    const bool owner = !token_.empty() && constant_time_equal(token, token_);
    const bool lan = !lan_.empty() && constant_time_equal(token, lan_);
    if (owner)
        return Access::Full;
    if (lan && is_lan_address(client_ip))
        return Access::Read;
    return Access::None;
}

std::string TokenStore::token() const
{
    std::lock_guard<std::mutex> lk(m_);
    return token_;
}

std::string TokenStore::lan_token() const
{
    std::lock_guard<std::mutex> lk(m_);
    return lan_;
}

// ---------------------------------------------------------------- limiter

void AuthLimiter::prune_locked(double now_s)
{
    for (auto it = e_.begin(); it != e_.end();) {
        auto& f = it->second.failures;
        f.erase(std::remove_if(f.begin(), f.end(), [&](double t) { return now_s - t > p_.window_s; }), f.end());
        if (f.empty() && it->second.blocked_until <= now_s)
            it = e_.erase(it);
        else
            ++it;
    }
}

bool AuthLimiter::blocked(const std::string& ip, double now_s) { return retry_after(ip, now_s) > 0; }

double AuthLimiter::retry_after(const std::string& ip, double now_s)
{
    std::lock_guard<std::mutex> lk(m_);
    const auto it = e_.find(ip);
    if (it == e_.end())
        return 0;
    return std::max(0.0, it->second.blocked_until - now_s);
}

void AuthLimiter::failure(const std::string& ip, double now_s)
{
    std::lock_guard<std::mutex> lk(m_);
    prune_locked(now_s);
    if (e_.size() > 4096)  // a flood from many addresses: forget the oldest state
        e_.clear();
    auto& e = e_[ip];
    e.failures.push_back(now_s);
    if (static_cast<int>(e.failures.size()) >= p_.max_failures) {
        e.blocked_until = now_s + p_.block_s;
        e.failures.clear();
    }
}

void AuthLimiter::success(const std::string& ip)
{
    std::lock_guard<std::mutex> lk(m_);
    const auto it = e_.find(ip);
    if (it != e_.end() && it->second.blocked_until <= 0)
        e_.erase(it);
    else if (it != e_.end())
        it->second.failures.clear();
}

size_t AuthLimiter::tracked() const
{
    std::lock_guard<std::mutex> lk(m_);
    return e_.size();
}

// ---------------------------------------------------------------- origins

std::string OriginPolicy::normalise(const std::string& origin)
{
    std::string o = origin;
    while (!o.empty() && (o.back() == '/' || o.back() == ' '))
        o.pop_back();
    size_t b = 0;
    while (b < o.size() && o[b] == ' ')
        ++b;
    o = o.substr(b);
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

OriginPolicy::OriginPolicy(std::vector<std::string> allowed)
{
    for (auto& a : allowed) {
        std::string n = normalise(a);
        if (!n.empty() && n != "null" && n != "*")
            allowed_.push_back(std::move(n));
    }
}

bool OriginPolicy::allowed(const std::string& origin) const
{
    const std::string n = normalise(origin);
    if (n.empty() || n == "null" || n == "*")
        return false;
    return std::find(allowed_.begin(), allowed_.end(), n) != allowed_.end();
}

}  // namespace yantra::probe
