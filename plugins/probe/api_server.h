// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe's remote API server (API-02): CivetWeb (MIT, spec 16.2,
// vendored in third_party/civetweb) serving the REST endpoints and the
// WebSocket live view of api_routes.h. Binds to 127.0.0.1 by
// default; HTTPS when a certificate is given and the build has TLS
// (YANTRA_API_TLS, system OpenSSL), else run it behind a reverse proxy
// (docs/API.md). A broadcaster thread ("api-live") sends each WebSocket
// client its filtered rounds at the client's rate and pushes probe
// summaries as they are written.
// Copied from yantra-v1 c24e8f5: core/engine/api_server.h, adapted for
// PLG-24.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api_routes.h"

struct mg_context;
struct mg_connection;

namespace yantra::probe {

struct ServerConfig {
    // "[ADDRESS:]PORT"; no address = 127.0.0.1; "0.0.0.0:8470" or
    // "[::]:8470" for all interfaces; port 0 = any free port (tests).
    std::string listen = "127.0.0.1:8470";
    std::string tls_certificate;   // PEM with certificate and key; empty: plain HTTP
    int threads = 4;               // request worker threads (a WebSocket client keeps one)
    size_t max_live_clients = 8;
    int request_timeout_ms = 10000;
};

// Whether this build can serve HTTPS (CivetWeb with OpenSSL).
bool tls_available();
// "8470" -> "127.0.0.1:8470"; validates the form. Empty on error.
std::string normalise_listen(const std::string& listen, std::string* error = nullptr);

class Server {
public:
    using LogFn = std::function<void(std::uint32_t level, const std::string& message)>;
    Server(std::shared_ptr<Router> router, LogFn log);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(const ServerConfig& cfg, std::string* error = nullptr);
    // Closes the live clients and stops the server; waits for running
    // requests. Idempotent.
    void stop();
    bool running() const { return ctx_ != nullptr; }
    int port() const { return port_; }      // the bound port (after start)
    bool tls() const { return tls_; }

    // Sends a probe summary to the live clients that want summaries.
    void publish_summary(const nlohmann::json& summary);
    size_t live_clients() const;
    uint64_t requests() const { return requests_.load(); }

    void log(std::uint32_t level, const std::string& message) const;

    // CivetWeb callbacks (internal).
    int on_request(mg_connection* conn);
    int on_ws_connect(const mg_connection* conn);
    void on_ws_ready(mg_connection* conn);
    int on_ws_data(mg_connection* conn, int bits, char* data, size_t len);
    void on_ws_close(const mg_connection* conn);

private:
    struct Client {
        mg_connection* conn = nullptr;
        LiveView view;
        bool ready = false;
        std::deque<std::string> pending;  // summaries and replies to send next round
    };
    std::shared_ptr<Router> router_;
    LogFn log_;
    mg_context* ctx_ = nullptr;
    int port_ = 0;
    bool tls_ = false;
    size_t max_clients_ = 8;
    std::atomic<uint64_t> requests_{0};

    mutable std::mutex m_;                 // clients_
    std::vector<std::unique_ptr<Client>> clients_;
    std::atomic<bool> stop_{false};
    std::mutex wake_m_;
    std::condition_variable wake_cv_;
    std::thread live_thread_;

    void live_loop();
    Client* find_locked(const mg_connection* conn);
    static void send_text(mg_connection* conn, const std::string& text);
};

}  // namespace yantra::probe
