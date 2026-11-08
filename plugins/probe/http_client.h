// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A minimal HTTP/1.1 client for the remote-probes view (API-02 central
// viewer): one GET per connection ("Connection: close", the probe sends
// Content-Length), plain HTTP only (HTTPS through a reverse proxy needs a
// TLS client: not in this version), a bounded wait for connect and
// reply. POSIX sockets (MSYS2 UCRT64: Winsock, DEFERRED (Windows)).
#pragma once

#include <string>

namespace yantra::probe {

struct HttpReply {
    int status = 0;     // 0: no reply (error says why)
    std::string body;
    std::string error;
};

// "http://host[:port]" (default port 8470) -> host, port; false on any
// other form.
bool parse_base_url(const std::string& url, std::string& host, std::string& port, std::string* error);

// GET <base_url><path> with "Authorization: Bearer <token>" (when given).
HttpReply http_get(const std::string& base_url, const std::string& path, const std::string& token,
                   int timeout_ms = 3000);

}  // namespace yantra::probe
