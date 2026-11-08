// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The remote-probes view's HTTP client (API-02), see http_client.h.
#include "http_client.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace yantra::probe {

namespace {

constexpr std::size_t kMaxReply = 4 * 1024 * 1024;

struct Fd {
    int fd = -1;
    ~Fd() {
        if (fd >= 0) ::close(fd);
    }
};

bool wait_fd(int fd, short events, std::chrono::steady_clock::time_point end) {
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now());
        if (left.count() <= 0) return false;
        pollfd p{fd, events, 0};
        const int n = ::poll(&p, 1, static_cast<int>(left.count()));
        if (n > 0) return true;
        if (n < 0 && errno != EINTR) return false;
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
    if (const int e = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res); e != 0) {
        r.error = std::string("address not found: ") + gai_strerror(e);
        return r;
    }
    Fd s;
    std::string why = "no address";
    for (addrinfo* a = res; a != nullptr && s.fd < 0; a = a->ai_next) {
        const int fd = ::socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) continue;
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        int rc = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS && wait_fd(fd, POLLOUT, end)) {
            int err = 0;
            socklen_t len = sizeof err;
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            rc = err == 0 ? 0 : -1;
            errno = err;
        }
        if (rc == 0) {
            s.fd = fd;
        } else {
            why = errno != 0 ? std::strerror(errno) : "timed out";
            ::close(fd);
        }
    }
    ::freeaddrinfo(res);
    if (s.fd < 0) {
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
        const ssize_t n = ::send(s.fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            r.error = std::string("send failed: ") + std::strerror(errno);
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
        const ssize_t n = ::recv(s.fd, buf, sizeof buf, 0);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            r.error = std::string("receive failed: ") + std::strerror(errno);
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
