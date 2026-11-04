// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "remote_server.hpp"

#include <SoapyInfoUtils.hpp>
#include <SoapyRPCSocket.hpp>
#include <SoapyRemoteDefs.hpp>
#include <SoapyServer.hpp>

#include <stdexcept>

namespace yantra::tests {

struct RemoteServer::Impl {
    SoapySocketSession session;
    SoapyRPCSocket socket;
    std::unique_ptr<SoapyServerListener> listener;
};

RemoteServer::RemoteServer() : impl_(std::make_unique<Impl>())
{
    if (impl_->socket.bind("tcp://127.0.0.1:0") != 0)
        throw std::runtime_error(std::string("SoapyRemote test server: bind: ") +
                                 impl_->socket.lastErrorMsg());
    impl_->socket.listen(SOAPY_REMOTE_LISTEN_BACKLOG);
    url_ = impl_->socket.getsockname();
    impl_->listener = std::make_unique<SoapyServerListener>(impl_->socket, SoapyInfo::generateUUID1());
    thread_ = std::thread([this] {
        while (!done_)
            impl_->listener->handleOnce(); // waits at most 100 ms for a client
    });
}

RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::stop()
{
    if (!thread_.joinable())
        return;
    done_ = true;
    thread_.join();
    impl_->listener.reset(); // joins the client handler threads
    impl_->socket.close();
}

} // namespace yantra::tests
