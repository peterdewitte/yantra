// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A SoapySDRServer on the loopback for the SoapyRemote tests (RAD-50,
// RAD-92): SoapyRemote's own server side (its ClientHandler, with an own
// accept loop) in this process, serving the SoapySDR modules linked into it (the fake
// module). Test builds only.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace yantra::tests {

class RemoteServer {
public:
    /// Binds tcp://127.0.0.1 on a free port and serves until stop() or
    /// destruction. Throws std::runtime_error when the bind fails.
    RemoteServer();
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    /// "tcp://127.0.0.1:<port>", the value for YANTRA_SOAPY_REMOTE.
    const std::string& url() const { return url_; }
    /// Closes the listening socket and joins the client threads.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string url_;
    std::atomic<bool> done_{false};
    std::thread thread_;
};

} // namespace yantra::tests
