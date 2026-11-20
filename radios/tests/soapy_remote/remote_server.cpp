// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "remote_server.hpp"

#include <ClientHandler.hpp>
#include <SoapyInfoUtils.hpp>
#include <SoapyRPCSocket.hpp>
#include <SoapyRemoteDefs.hpp>

#include <SoapySDR/Logger.hpp>

#include <exception>
#include <list>
#include <stdexcept>

namespace yantra::tests {

namespace {

// One client connection: SoapyRemote's ClientHandler on its own thread,
// as SoapyServerListener runs it, but with an atomic stop flag (the
// listener's sig_atomic_t flag is written and read from two threads
// without synchronisation: ThreadSanitizer reports it).
struct Client {
    std::unique_ptr<SoapyRPCSocket> socket;
    std::atomic<bool> done{false};
    std::thread thread;

    void run(const std::string& uuid)
    {
        try {
            SoapyClientHandler handler(*socket, uuid);
            while (!done && handler.handleOnce()) {
            }
        } catch (const std::exception& e) {
            SoapySDR::logf(SOAPY_SDR_ERROR, "SoapyRemote test server: client: %s", e.what());
        }
        done = true;
    }
};

} // namespace

struct RemoteServer::Impl {
    SoapySocketSession session;
    SoapyRPCSocket socket;
    std::string uuid = SoapyInfo::generateUUID1();
    std::list<Client> clients; // only the accept thread touches the list until stop()

    void reap(bool all)
    {
        for (auto it = clients.begin(); it != clients.end();) {
            if (all)
                it->done = true;
            if (it->done) {
                it->thread.join();
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    void accept_once()
    {
        reap(false);
        if (!socket.selectRecv(SOAPY_REMOTE_SOCKET_TIMEOUT_US)) // at most 100 ms
            return;
        std::unique_ptr<SoapyRPCSocket> client(socket.accept());
        if (!client)
            return;
        auto& c = clients.emplace_back();
        c.socket = std::move(client);
        c.thread = std::thread([&c, this] { c.run(uuid); });
    }
};

RemoteServer::RemoteServer() : impl_(std::make_unique<Impl>())
{
    if (impl_->socket.bind("tcp://127.0.0.1:0") != 0)
        throw std::runtime_error(std::string("SoapyRemote test server: bind: ") +
                                 impl_->socket.lastErrorMsg());
    impl_->socket.listen(SOAPY_REMOTE_LISTEN_BACKLOG);
    url_ = impl_->socket.getsockname();
    thread_ = std::thread([this] {
        while (!done_)
            impl_->accept_once();
    });
}

RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::stop()
{
    if (!thread_.joinable())
        return;
    done_ = true;
    thread_.join();
    impl_->reap(true); // stops and joins the client handler threads
    impl_->socket.close();
}

} // namespace yantra::tests

// SoapyRemote 0.5.3's server never deletes a stream's SoapyStreamEndpoint
// (ServerStreamData's destructor frees the sockets only), so every stream
// set up through this test server leaks its endpoint. That is upstream
// server code, used here only to serve the tests (never in a plugin, whose
// client side is not affected); the vendored files stay unchanged. Only
// allocations under the server's ClientHandler are suppressed.
extern "C" const char* __lsan_default_suppressions();
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:SoapyClientHandler::handleOnce\n";
}

// SoapyRemote 0.5.3's server stops a stream's worker threads through a
// sig_atomic_t flag (ServerStreamData::done) written in stopThreads and
// read in the workers without synchronisation: ThreadSanitizer reports
// it. Upstream server code, test-only here, header-defined (no replacement
// file possible without copying the whole server); the client side of the
// plugins has its own fix (tools/deps/soapyremote/LogAcceptor.cpp). Only
// that flag's functions are suppressed.
extern "C" const char* __tsan_default_suppressions();
extern "C" const char* __tsan_default_suppressions()
{
    return "race:ServerStreamData::stopThreads\n"
           "race:ServerStreamData::sendEndpointWork\n"
           "race:ServerStreamData::recvEndpointWork\n"
           "race:ServerStreamData::statEndpointWork\n";
}
