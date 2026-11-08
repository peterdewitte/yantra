// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Security of the network remote API (API-02, docs/API.md "Threat model"):
//  - bearer tokens: an owner token (full access: read, live view and
//    commands) generated on first start, and an optional LAN token
//    (read and live view only, accepted only from loopback, private and
//    link-local client addresses). Both are stored in a token file that
//    only the owner can read (folder 0700, file 0600) and can be rotated;
//  - a limiter that blocks a client address after repeated failed
//    authentication;
//  - an Origin allow-list for browsers (CORS and WebSocket): a request
//    that carries an Origin header outside the list is refused.
// Tokens are compared in constant time and never written to the log.
// Copied from yantra-v1 c24e8f5: core/engine/api_auth.h, adapted for
// PLG-24 (the probe feature plugin keeps the file in its data folder).
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace yantra::probe {

enum class Access { None, Read, Full };
const char* access_name(Access a);

// 32 random bytes from the kernel (getrandom) as 64 hex digits.
std::string generate_token();
bool constant_time_equal(const std::string& a, const std::string& b);
// The token of an "Authorization: Bearer <token>" header value; nullopt
// for any other scheme or an empty token.
std::optional<std::string> bearer_token(const std::string& authorization);

// Client address classes (textual IPv4/IPv6 as the server reports them;
// IPv4-mapped IPv6 addresses count as IPv4).
bool is_loopback_address(const std::string& ip);
// Loopback, RFC 1918 private, RFC 6598 shared, link-local (IPv4 and IPv6)
// and IPv6 unique local addresses.
bool is_lan_address(const std::string& ip);

// The token file "yantra-api-tokens" v1 (docs/API.md):
// {"format":"yantra-api-tokens","version":1,"token":"...","lan_token":"..."|null}
inline constexpr const char* kTokenFormat = "yantra-api-tokens";
inline constexpr int kTokenVersion = 1;


class TokenStore {
public:
    explicit TokenStore(std::filesystem::path file) : file_(std::move(file)) {}

    // Reads the file; creates it with a new owner token when it is missing
    // (`created` set). Fails on an unreadable or invalid file, or when the
    // file can be read by others than the owner.
    bool load_or_create(std::string* error = nullptr, bool* created = nullptr);
    // A new owner token; the old one stops working at once.
    bool rotate(std::string* error = nullptr);
    // A new LAN token (enable = true) or none (false).
    bool set_lan(bool enable, std::string* error = nullptr);

    // Access for a presented token from a client address. Rereads the file
    // when it changed on disk (a rotation by another process).
    Access check(const std::string& token, const std::string& client_ip);

    std::string token() const;
    std::string lan_token() const;  // empty: no LAN token
    const std::filesystem::path& path() const { return file_; }

private:
    std::filesystem::path file_;
    mutable std::mutex m_;
    std::string token_, lan_;
    std::filesystem::file_time_type mtime_{};
    bool save_locked(std::string* error);
    bool load_locked(std::string* error);
    void reload_if_changed_locked();
};

// Blocks a client address for `block_s` after `max_failures` failed
// authentications within `window_s`. Time is passed in (seconds on a
// monotonic clock) so tests need no sleeps.
class AuthLimiter {
public:
    struct Policy {
        int max_failures = 5;
        double window_s = 60;
        double block_s = 60;
    };
    AuthLimiter() = default;
    explicit AuthLimiter(Policy p) : p_(p) {}
    bool blocked(const std::string& ip, double now_s);
    // Seconds until the block ends (0: not blocked).
    double retry_after(const std::string& ip, double now_s);
    void failure(const std::string& ip, double now_s);
    void success(const std::string& ip);
    size_t tracked() const;

private:
    struct Entry {
        std::vector<double> failures;
        double blocked_until = 0;
    };
    Policy p_;
    mutable std::mutex m_;
    std::map<std::string, Entry> e_;
    void prune_locked(double now_s);
};

// Origins a browser may use ("https://viewer.example:8443"); scheme and
// host compare case-insensitively, a trailing '/' is ignored. "null" and
// "*" are never allowed. Requests without an Origin header (non-browser
// clients) pass; they still need a token.
class OriginPolicy {
public:
    OriginPolicy() = default;
    explicit OriginPolicy(std::vector<std::string> allowed);
    bool allowed(const std::string& origin) const;
    const std::vector<std::string>& list() const { return allowed_; }
    static std::string normalise(const std::string& origin);

private:
    std::vector<std::string> allowed_;
};

}  // namespace yantra::probe
