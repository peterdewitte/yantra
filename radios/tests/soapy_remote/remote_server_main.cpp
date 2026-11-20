// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-soapy-remote-test-server URL_FILE: a loopback SoapySDRServer with
// the fake module (YANTRA_FAKE_SOAPY) for the conformance run of the
// generic plugin over SoapyRemote. Writes its URL to URL_FILE and serves
// until SIGTERM or SIGINT. Test builds only, never installed.
#include "remote_server.hpp"

#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>

#include <unistd.h>

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void on_signal(int) { stop_requested = 1; }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: yantra-soapy-remote-test-server URL_FILE\n";
        return 2;
    }
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);
    try {
        yantra::tests::RemoteServer server;
        const std::string tmp = std::string(argv[1]) + ".part";
        std::ofstream(tmp) << server.url() << '\n';
        if (std::rename(tmp.c_str(), argv[1]) != 0)
            return 1;
        while (stop_requested == 0)
            ::usleep(50000);
        server.stop();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
