// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The CivetWeb glue of the probe's remote API (API-02), see api_server.h.
// Copied from yantra-v1 c24e8f5: core/engine/api_server.cpp, adapted for
// PLG-24 (log through the plugin host, the probe's live view).
#include "api_server.h"

#include <civetweb.h>

#if defined(__linux__)
#include <pthread.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include <chrono>
#include <mutex>

namespace yantra::probe {

namespace {

void init_library_once()
{
    static std::once_flag once;
    std::call_once(once, [] {
        unsigned features = MG_FEATURES_WEBSOCKET | MG_FEATURES_IPV6;
        if (tls_available())
            features |= MG_FEATURES_TLS;
        mg_init_library(features);
    });
}

int request_cb(mg_connection* conn, void* cb) { return static_cast<Server*>(cb)->on_request(conn); }
int ws_connect_cb(const mg_connection* conn, void* cb) { return static_cast<Server*>(cb)->on_ws_connect(conn); }
void ws_ready_cb(mg_connection* conn, void* cb) { static_cast<Server*>(cb)->on_ws_ready(conn); }
int ws_data_cb(mg_connection* conn, int bits, char* data, size_t len, void* cb)
{
    return static_cast<Server*>(cb)->on_ws_data(conn, bits, data, len);
}
void ws_close_cb(const mg_connection* conn, void* cb) { static_cast<Server*>(cb)->on_ws_close(conn); }

// CivetWeb's own messages go to the debug log only (they never contain
// header values, so no token reaches the log).
int log_cb(const mg_connection* conn, const char* message)
{
    const mg_context* ctx = conn != nullptr ? mg_get_context(conn) : nullptr;
    auto* server = ctx != nullptr ? static_cast<Server*>(mg_get_user_data(ctx)) : nullptr;
    if (server != nullptr)
        server->log(YANTRA_LOG_DEBUG, message ? message : "");
    return 1;
}

double mono_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string header(const mg_connection* conn, const char* name)
{
    const char* v = mg_get_header(conn, name);
    return v ? std::string(v) : std::string();
}

Request make_request(const mg_connection* conn, bool websocket)
{
    const mg_request_info* ri = mg_get_request_info(conn);
    Request r;
    r.method = ri->request_method ? ri->request_method : "";
    r.path = ri->local_uri ? ri->local_uri : "";
    r.query = ri->query_string ? ri->query_string : "";
    r.client_ip = ri->remote_addr;
    r.authorization = header(conn, "Authorization");
    r.origin = header(conn, "Origin");
    r.websocket = websocket;
    return r;
}

void write_response(mg_connection* conn, const Response& r, bool head_only = false)
{
    std::string h = "HTTP/1.1 " + std::to_string(r.status) + " " + mg_get_response_code_text(conn, r.status) + "\r\n";
    const uint64_t length = r.file.empty() ? r.body.size() : r.file_size;
    if (!r.content_type.empty())
        h += "Content-Type: " + r.content_type + "\r\n";
    h += "Content-Length: " + std::to_string(r.status == 204 ? 0 : length) + "\r\n";
    h += "X-Content-Type-Options: nosniff\r\n";
    h += "Connection: close\r\n";
    for (const auto& [k, v] : r.headers)
        h += k + ": " + v + "\r\n";
    h += "\r\n";
    mg_write(conn, h.data(), h.size());
    if (head_only || r.status == 204)
        return;
    if (r.file.empty()) {
        if (!r.body.empty())
            mg_write(conn, r.body.data(), r.body.size());
        return;
    }
    std::ifstream in(r.file, std::ios::binary);
    std::vector<char> buf(64 * 1024);
    uint64_t left = r.file_size;
    while (in && left > 0) {
        in.read(buf.data(), static_cast<std::streamsize>(std::min<uint64_t>(buf.size(), left)));
        const auto n = in.gcount();
        if (n <= 0 || mg_write(conn, buf.data(), static_cast<size_t>(n)) <= 0)
            break;
        left -= static_cast<uint64_t>(n);
    }
}

}  // namespace

bool tls_available()
{
#if defined(YANTRA_API_TLS)
    return true;
#else
    return false;
#endif
}

std::string normalise_listen(const std::string& listen, std::string* error)
{
    auto fail = [&](const std::string& why) {
        if (error)
            *error = "listen address \"" + listen.substr(0, 80) + "\": " + why;
        return std::string();
    };
    std::string host = "127.0.0.1", port = listen;
    if (!listen.empty() && listen[0] == '[') {
        const auto close = listen.find(']');
        if (close == std::string::npos || close + 1 >= listen.size() || listen[close + 1] != ':')
            return fail("use [IPv6]:PORT");
        host = listen.substr(0, close + 1);
        port = listen.substr(close + 2);
    } else if (const auto colon = listen.rfind(':'); colon != std::string::npos) {
        host = listen.substr(0, colon);
        port = listen.substr(colon + 1);
        if (host.empty() || host.find(':') != std::string::npos)
            return fail("use ADDRESS:PORT or [IPv6]:PORT");
        if (host == "localhost")
            host = "127.0.0.1";
    }
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos)
        return fail("the port must be a number");
    const long p = std::stol(port);
    if (p > 65535)
        return fail("the port must lie between 0 and 65535");
    if (host[0] != '[')
        for (char c : host)
            if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.'))
                return fail("give a numeric IPv4 address (or [IPv6])");
    return host + ":" + std::to_string(p);
}

Server::Server(std::shared_ptr<Router> router, LogFn log) : router_(std::move(router)), log_(std::move(log)) {}

void Server::log(std::uint32_t level, const std::string& message) const
{
    if (log_)
        log_(level, message);
}

Server::~Server() { stop(); }

bool Server::start(const ServerConfig& cfg, std::string* error)
{
    if (ctx_)
        return true;
    std::string listen = normalise_listen(cfg.listen, error);
    if (listen.empty())
        return false;
    tls_ = !cfg.tls_certificate.empty();
    if (tls_ && !tls_available()) {
        if (error)
            *error = "this build has no TLS support: run the API behind a reverse proxy for HTTPS (docs/API.md)";
        return false;
    }
    if (tls_)
        listen += "s";
    init_library_once();
    max_clients_ = cfg.max_live_clients;
    const std::string threads = std::to_string(std::max(2, cfg.threads) + static_cast<int>(cfg.max_live_clients));
    const std::string timeout = std::to_string(cfg.request_timeout_ms);
    std::vector<const char*> opt = {"listening_ports",  listen.c_str(),  "num_threads",
                                    threads.c_str(),    "request_timeout_ms", timeout.c_str(),
                                    "enable_keep_alive", "no",            "websocket_timeout_ms",
                                    "30000",            "enable_websocket_ping_pong", "yes"};
    if (tls_) {
        opt.push_back("ssl_certificate");
        opt.push_back(cfg.tls_certificate.c_str());
        opt.push_back("ssl_protocol_version");
        opt.push_back("4");  // TLS 1.2 or later
    }
    opt.push_back(nullptr);
    mg_callbacks cb{};
    cb.log_message = log_cb;
    mg_init_data init{};
    init.callbacks = &cb;
    init.user_data = this;
    init.configuration_options = opt.data();
    mg_error_data err{};
    char errbuf[256] = {0};
    err.text = errbuf;
    err.text_buffer_size = sizeof errbuf;
    ctx_ = mg_start2(&init, &err);
    if (!ctx_) {
        if (error)
            *error = std::string("cannot start the API server on ") + listen + ": " +
                     (errbuf[0] ? errbuf : "unknown error");
        return false;
    }
    mg_server_port ports[4];
    const int n = mg_get_server_ports(ctx_, 4, ports);
    port_ = n > 0 ? ports[0].port : 0;
    mg_set_request_handler(ctx_, "/", request_cb, this);
    mg_set_websocket_handler(ctx_, std::string(std::string(kApiPrefix) + "/live$").c_str(), ws_connect_cb, ws_ready_cb,
                             ws_data_cb, ws_close_cb, this);
    stop_ = false;
    live_thread_ = std::thread([this] {
#if defined(__linux__)
        // OA-03: every thread is named (the plugin does not link the core).
        (void)pthread_setname_np(pthread_self(), "yt-probe-live");
#endif
        live_loop();
    });
    log(YANTRA_LOG_INFO, std::string("remote API on ") + (tls_ ? "https://" : "http://") + listen.substr(0, listen.rfind(':')) +
                        ":" + std::to_string(port_));
    return true;
}

void Server::stop()
{
    if (!ctx_)
        return;
    stop_ = true;
    wake_cv_.notify_all();
    if (live_thread_.joinable())
        live_thread_.join();
    mg_stop(ctx_);  // closes the connections (close callbacks run) and joins the workers
    ctx_ = nullptr;
    std::lock_guard<std::mutex> lk(m_);
    clients_.clear();
}

size_t Server::live_clients() const
{
    std::lock_guard<std::mutex> lk(m_);
    return clients_.size();
}

Server::Client* Server::find_locked(const mg_connection* conn)
{
    for (auto& c : clients_)
        if (c->conn == conn)
            return c.get();
    return nullptr;
}

void Server::send_text(mg_connection* conn, const std::string& text)
{
    mg_websocket_write(conn, MG_WEBSOCKET_OPCODE_TEXT, text.data(), text.size());
}

int Server::on_request(mg_connection* conn)
{
    ++requests_;
    Request req = make_request(conn, false);
    const mg_request_info* ri = mg_get_request_info(conn);
    if (ri->content_length > static_cast<long long>(kMaxBody)) {
        write_response(conn, error_response(413, "the body is too large"));
        return 1;
    }
    char buf[4096];
    while (req.body.size() <= kMaxBody) {
        const int n = mg_read(conn, buf, sizeof buf);
        if (n <= 0)
            break;
        req.body.append(buf, static_cast<size_t>(n));
    }
    if (req.body.size() > kMaxBody) {
        write_response(conn, error_response(413, "the body is too large"));
        return 1;
    }
    const bool head = req.method == "HEAD";
    if (head)
        req.method = "GET";
    const Response r = router_->handle(req);
    if (r.status == 101) {  // an upgrade request that did not reach the WebSocket handler
        write_response(conn, error_response(400, "the live view is a WebSocket (Upgrade: websocket)"));
        return 1;
    }
    write_response(conn, r, head);
    if (r.status >= 400)
        log(YANTRA_LOG_DEBUG, req.method + " " + req.path + " -> " + std::to_string(r.status) + " (" + req.client_ip + ")");
    return 1;
}

int Server::on_ws_connect(const mg_connection* cconn)
{
    ++requests_;
    auto* conn = const_cast<mg_connection*>(cconn);
    const Request req = make_request(cconn, true);
    Response r = router_->handle(req);
    if (r.status == 101) {
        std::lock_guard<std::mutex> lk(m_);
        if (clients_.size() >= max_clients_) {
            r = error_response(503, "too many live clients");
        } else {
            auto c = std::make_unique<Client>();
            c->conn = conn;
            LiveFilter f = default_live_filter();
            std::string err;
            apply_filter_query(f, parse_query(req.query), &err);  // validated by the router
            c->view.set_filter(f);
            clients_.push_back(std::move(c));
            log(YANTRA_LOG_INFO, "live client connected (" + req.client_ip + ", " + access_name(r.access) + ")");
            return 0;
        }
    }
    log(YANTRA_LOG_DEBUG, "live view refused: " + std::to_string(r.status) + " (" + req.client_ip + ")");
    write_response(conn, r);
    return 1;
}

void Server::on_ws_ready(mg_connection* conn)
{
    std::lock_guard<std::mutex> lk(m_);
    Client* c = find_locked(conn);
    if (!c)
        return;
    nlohmann::json hello = live_message("hello", router_->backend().demo && router_->backend().demo());
    hello["filter"] = c->view.filter().to_json();
    hello["types"] = live_types();
    send_text(conn, hello.dump());
    c->ready = true;
    wake_cv_.notify_all();
}

int Server::on_ws_data(mg_connection* conn, int bits, char* data, size_t len)
{
    const int opcode = bits & 0x0f;
    if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE)
        return 0;
    if (opcode != MG_WEBSOCKET_OPCODE_TEXT)
        return 1;  // ping/pong are answered by CivetWeb; binary frames are ignored
    nlohmann::json reply;
    const bool demo = router_->backend().demo && router_->backend().demo();
    if (len > 4096) {
        reply = live_message("error", demo);
        reply["error"] = "message too large";
    } else {
        const auto j = nlohmann::json::parse(std::string(data, len), nullptr, false);
        std::lock_guard<std::mutex> lk(m_);
        Client* c = find_locked(conn);
        if (!c)
            return 0;
        LiveFilter f = c->view.filter();
        std::string err;
        if (j.is_discarded() || !apply_filter_json(f, j, &err)) {
            reply = live_message("error", demo);
            reply["error"] = j.is_discarded() ? "the message is not JSON" : err;
        } else {
            c->view.set_filter(f);
            reply = live_message("filter", demo);
            reply["filter"] = f.to_json();
        }
        c->pending.push_back(reply.dump());
        wake_cv_.notify_all();
        return 1;
    }
    std::lock_guard<std::mutex> lk(m_);
    if (Client* c = find_locked(conn))
        c->pending.push_back(reply.dump());
    return 1;
}

void Server::on_ws_close(const mg_connection* conn)
{
    std::lock_guard<std::mutex> lk(m_);
    const auto it = std::find_if(clients_.begin(), clients_.end(), [&](const auto& c) { return c->conn == conn; });
    if (it != clients_.end()) {
        clients_.erase(it);
        log(YANTRA_LOG_INFO, "live client disconnected");
    }
}

void Server::publish_summary(const nlohmann::json& summary)
{
    const bool demo = router_->backend().demo && router_->backend().demo();
    std::lock_guard<std::mutex> lk(m_);
    for (auto& c : clients_)
        if (auto m = c->view.summary(summary, demo)) {
            if (c->pending.size() < 64)
                c->pending.push_back(m->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
    wake_cv_.notify_all();
}

void Server::live_loop()
{
    const Backend& b = router_->backend();
    while (!stop_.load()) {
        {
            const double now = mono_s();
            std::lock_guard<std::mutex> lk(m_);
            std::shared_ptr<const Snapshot> snap;
            bool fetched = false;
            const bool demo = b.demo && b.demo();
            for (auto& c : clients_) {
                if (!c->ready)
                    continue;
                while (!c->pending.empty()) {
                    send_text(c->conn, c->pending.front());
                    c->pending.pop_front();
                }
                if (!c->view.due(now))
                    continue;
                if (!fetched) {
                    snap = b.snapshot ? b.snapshot() : nullptr;
                    if (!snap)
                        snap = std::make_shared<Snapshot>();
                    fetched = true;
                }
                for (const auto& m : c->view.step(*snap, demo, now))
                    send_text(c->conn, m.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
        }
        std::unique_lock<std::mutex> lk(wake_m_);
        wake_cv_.wait_for(lk, std::chrono::milliseconds(50), [this] { return stop_.load(); });
    }
}

}  // namespace yantra::probe
