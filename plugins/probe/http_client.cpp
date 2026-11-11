// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The remote-probes view's HTTP client (API-02), see http_client.h.
#include "http_client.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <mutex>

#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace yantra::probe {

namespace {

constexpr std::size_t kMaxReply = 4 * 1024 * 1024;

// Sockets on Linux and Windows (HW-06): Winsock needs WSAStartup before
// getaddrinfo, reports its errors through WSAGetLastError and polls with
// WSAPoll.
#if defined(_WIN32)
using sock_t = SOCKET;
constexpr sock_t kNoSocket = INVALID_SOCKET;
constexpr int kNoSignal = 0;
void close_socket(sock_t s) { ::closesocket(s); }
int socket_error() { return ::WSAGetLastError(); }
bool connect_pending(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
bool retry_later(int e) { return e == WSAEWOULDBLOCK || e == WSAEINTR; }
std::string socket_error_text(int e) { return "socket error " + std::to_string(e); }
void set_nonblocking(sock_t s) {
    u_long on = 1;
    ::ioctlsocket(s, FIONBIO, &on);
}
int poll_one(sock_t s, short events, int ms) {
    WSAPOLLFD p{s, events, 0};
    return ::WSAPoll(&p, 1, ms);
}
void start_sockets() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    });
}
#else
using sock_t = int;
constexpr sock_t kNoSocket = -1;
constexpr int kNoSignal = MSG_NOSIGNAL;
void close_socket(sock_t s) { ::close(s); }
int socket_error() { return errno; }
bool connect_pending(int e) { return e == EINPROGRESS; }
bool retry_later(int e) { return e == EAGAIN || e == EINTR; }
std::string socket_error_text(int e) { return std::strerror(e); }
void set_nonblocking(sock_t s) { ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL) | O_NONBLOCK); }
int poll_one(sock_t s, short events, int ms) {
    pollfd p{s, events, 0};
    return ::poll(&p, 1, ms);
}
void start_sockets() {}
#endif

struct Fd {
    sock_t fd = kNoSocket;
    ~Fd() {
        if (fd != kNoSocket) close_socket(fd);
    }
};

bool wait_fd(sock_t fd, short events, std::chrono::steady_clock::time_point end) {
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
        if (left.count() <= 0) return false;
        const int n = poll_one(fd, events, static_cast<int>(left.count()));
        if (n > 0) return true;
        if (n < 0 && !retry_later(socket_error())) return false;
    }
}

}  // namespace

bool parse_base_url(const std::string& url, std::string& host, std::string& port, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const std::string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return fail(url.compare(0, 8, "https://") == 0 ? "https needs a TLS client: use http:// (or a tunnel)"
                                                       : "the address starts with http://");
    std::string rest = url.substr(scheme.size());
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty() || rest.find('/') != std::string::npos) return fail("give http://host:port");
    if (rest[0] == '[') {
        const auto close = rest.find(']');
        if (close == std::string::npos) return fail("give http://[IPv6]:port");
        host = rest.substr(1, close - 1);
        port = close + 1 < rest.size() && rest[close + 1] == ':' ? rest.substr(close + 2) : "8470";
    } else if (const auto colon = rest.rfind(':'); colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    } else {
        host = rest;
        port = "8470";
    }
    if (host.empty() || port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
        return fail("give http://host:port");
    return true;
}

HttpReply http_get(const std::string& base_url, const std::string& path, const std::string& token, int timeout_ms) {
    HttpReply r;
    std::string host, port;
    if (!parse_base_url(base_url, host, port, &r.error)) return r;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    start_sockets();
    if (const int e = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res); e != 0) {
        r.error = std::string("address not found: ") + gai_strerror(e);
        return r;
    }
    Fd s;
    std::string why = "no address";
    for (addrinfo* a = res; a != nullptr && s.fd == kNoSocket; a = a->ai_next) {
#if defined(_WIN32)
        const sock_t fd = ::WSASocketW(a->ai_family, a->ai_socktype, a->ai_protocol, nullptr, 0,
                                       WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
#else
        const sock_t fd = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
#endif
        if (fd == kNoSocket) continue;
        set_nonblocking(fd);
        int rc = ::connect(fd, a->ai_addr, static_cast<socklen_t>(a->ai_addrlen));
        int err = rc == 0 ? 0 : socket_error();
        if (rc != 0 && connect_pending(err)) {
            err = ETIMEDOUT;
            if (wait_fd(fd, POLLOUT, end)) {
                int so = 0;
                socklen_t len = sizeof so;
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so), &len);
                rc = so == 0 ? 0 : -1;
                err = so;
            }
        }
        if (rc == 0) {
            s.fd = fd;
        } else {
            why = err == ETIMEDOUT ? "timed out" : socket_error_text(err);
            close_socket(fd);
        }
    }
    ::freeaddrinfo(res);
    if (s.fd == kNoSocket) {
        r.error = "cannot connect: " + why;
        return r;
    }
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + port + "\r\nConnection: close\r\n";
    if (!token.empty()) req += "Authorization: Bearer " + token + "\r\n";
    req += "\r\n";
    for (std::size_t sent = 0; sent < req.size();) {
        if (!wait_fd(s.fd, POLLOUT, end)) {
            r.error = "timed out";
            return r;
        }
        const auto n = ::send(s.fd, req.data() + sent, static_cast<int>(req.size() - sent), kNoSignal);
        if (n < 0 && !retry_later(socket_error())) {
            r.error = "send failed: " + socket_error_text(socket_error());
            return r;
        }
        if (n > 0) sent += static_cast<std::size_t>(n);
    }
    std::string in;
    char buf[16384];
    for (;;) {
        if (!wait_fd(s.fd, POLLIN, end)) {
            r.error = "timed out";
            return r;
        }
        const auto n = ::recv(s.fd, buf, static_cast<int>(sizeof buf), 0);
        if (n == 0) break;
        if (n < 0) {
            const int e = socket_error();
            if (retry_later(e)) continue;
            r.error = "receive failed: " + socket_error_text(e);
            return r;
        }
        in.append(buf, static_cast<std::size_t>(n));
        if (in.size() > kMaxReply) {
            r.error = "reply too large";
            return r;
        }
    }
    const auto head_end = in.find("\r\n\r\n");
    if (in.compare(0, 5, "HTTP/") != 0 || head_end == std::string::npos) {
        r.error = "not an HTTP reply";
        return r;
    }
    const auto sp = in.find(' ');
    try {
        r.status = std::stoi(in.substr(sp + 1, 3));
    } catch (const std::exception&) {
        r.error = "not an HTTP reply";
        return r;
    }
    r.body = in.substr(head_end + 4);
    return r;
}

}  // namespace yantra::probe
