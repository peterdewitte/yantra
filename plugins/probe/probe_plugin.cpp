// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe feature plugin (PLG-24): Yantra's only network listener. When
// it is enabled and its setting api.listen is given, it serves the remote
// API (API-02: REST + WebSocket through CivetWeb, owner and LAN tokens,
// an Origin allow-list, the AGPL source link) from the records Yantra
// hands the feature (yantra_feature_api::on_record) and drives Yantra
// through the plugin API's `control` service (API-01). It writes the
// periodic health and coverage summaries of an unattended fixed probe
// (API-03) into its data folder and the log, and offers the "Remote
// probes" window (PLG-06): the state of this Yantra's API and a central
// view of other probes (REST polling). Without this plugin Yantra opens
// no socket. Scope: broadcast/system information, levels and quality
// only (live.h). docs/API.md, docs/PROBE.md.
//
// Settings (yantra_host_api::settings_get; the headless runner sets them
// with --api and --set yantra.probe:KEY=VALUE):
//   api.listen           "[ADDRESS:]PORT", "" = no server (default); port 0 = any free port
//   api.allowed_origins  comma-separated browser origins (CORS, WebSocket)
//   api.token_file       the token file (default <data folder>/api_tokens.json)
//   summary.health_s     health summary period, s (default 60; 0 = off)
//   summary.coverage_s   coverage summary period, s (default 300; 0 = off)
//   viewer.probes        the remote probes of the window (JSON list)
//
// Threads (PLG-07): init, shutdown, start, stop on the main thread;
// on_record on measurement threads (a short lock, no I/O); the HTTP
// workers and the plugin's own thread "probe" (summaries, remote polling)
// call `control`; draw on the UI thread.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "api_auth.h"
#include "api_routes.h"
#include "api_server.h"
#include "http_client.h"
#include "live.h"
#include "yantra/plugin.h"

#ifndef YANTRA_PROBE_PLUGIN_VERSION
#define YANTRA_PROBE_PLUGIN_VERSION "0.0.0"
#endif
#ifndef YANTRA_SOURCE_URL
#define YANTRA_SOURCE_URL ""
#endif

#if defined(__linux__)
#include <pthread.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

namespace yantra::probe {
class Probe;
}

struct yantra_plugin_instance {
    yantra::probe::Probe* probe = nullptr;
};

namespace yantra::probe {

namespace {

constexpr const char* kLicence =
    "GPL-3.0-or-later (the TETRA radio plugin includes osmo-tetra under AGPL-3.0-or-later)";
constexpr std::uintmax_t kSummaryFileLimit = 10u * 1024u * 1024u;

std::string utc_now_iso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        std::size_t c = s.find(',', p);
        if (c == std::string::npos) c = s.size();
        std::string item = s.substr(p, c - p);
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) out.push_back(item);
        p = c + 1;
    }
    return out;
}

}  // namespace

struct RemoteProbe {
    std::string url, token;
    bool ok = false;
    std::string error;
    json status, levels;
    std::string updated;  // UTC of the last reply
};

RemoteProbe remote(std::string url, std::string token) {
    RemoteProbe r;
    r.url = std::move(url);
    r.token = std::move(token);
    return r;
}

class Probe {
public:
    explicit Probe(const yantra_host_api* host) : host_(host) {}

    // ---- host helpers ----------------------------------------------------

    void log(std::uint32_t level, const std::string& m) const { host_->log(host_->host, level, m.c_str()); }

    std::string setting(const char* key, const std::string& fallback = {}) const {
        std::vector<char> buf(512);
        for (int attempt = 0; attempt < 3; ++attempt) {
            std::uint32_t len = 0;
            const yantra_status st = host_->settings_get(host_->host, key, buf.data(),
                                                         static_cast<std::uint32_t>(buf.size()), &len);
            if (st == YANTRA_OK) return std::string(buf.data(), len);
            if (st != YANTRA_ERR_BUFFER_TOO_SMALL) return fallback;
            buf.resize(static_cast<std::size_t>(len) + 1);
        }
        return fallback;
    }

    double number_setting(const char* key, double fallback) const {
        const std::string v = setting(key);
        if (v.empty()) return fallback;
        try {
            return std::stod(v);
        } catch (const std::exception&) {
            log(YANTRA_LOG_WARNING, std::string("setting ") + key + " is not a number: " + v);
            return fallback;
        }
    }

    // A command through the plugin API's control service (API-01).
    json control(const json& request) const {
        const std::string name = request.value("command", std::string());
        auto failure = [&](const std::string& why) { return json{{"ok", false}, {"command", name}, {"error", why}}; };
        if (!YANTRA_HAS_FIELD(host_, yantra_host_api, control))
            return failure("this Yantra is too old for commands (plugin API 1.1)");
        const std::string text = request.dump();
        std::vector<char> buf(64 * 1024);
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::uint32_t len = 0;
            const yantra_status st =
                host_->control(host_->host, text.c_str(), buf.data(), static_cast<std::uint32_t>(buf.size()), &len);
            if (st == YANTRA_OK) {
                json j = json::parse(std::string(buf.data(), len), nullptr, false);
                if (!j.is_object()) return failure("no valid reply");
                // DEMO-10: Yantra's own demo flag (the simulator radio), also before any record.
                if (name == "status" && j.contains("result") && j["result"].is_object())
                    host_demo_ = j["result"].value("demo", false);
                return j;
            }
            if (st == YANTRA_ERR_BUFFER_TOO_SMALL) {
                buf.resize(static_cast<std::size_t>(len) + 1);
                continue;  // the kept reply, the command does not run again
            }
            if (st == YANTRA_ERR_UNSUPPORTED) return failure("this Yantra offers no commands");
            if (st == YANTRA_ERR_BUSY) return failure("Yantra is busy; try again");
            return failure("the command failed (" + std::to_string(st) + ")");
        }
        return failure("the reply did not fit");
    }

    fs::path data_folder() const {
        if (!YANTRA_HAS_FIELD(host_, yantra_host_api, data_folder)) return {};
        std::vector<char> buf(4096);
        std::uint32_t len = 0;
        if (host_->data_folder(host_->host, buf.data(), static_cast<std::uint32_t>(buf.size()), &len) != YANTRA_OK)
            return {};
        return fs::path(std::string(buf.data(), len));
    }

    std::int64_t now_ns() const { return host_->monotonic_ns(host_->host); }

    // ---- feature ---------------------------------------------------------

    yantra_status start() {
        folder_ = data_folder();
        load_remotes();
        const std::string listen = setting("api.listen");
        std::snprintf(listen_buf_, sizeof listen_buf_, "%s", listen.empty() ? "127.0.0.1:8470" : listen.c_str());
        if (!listen.empty()) {
            std::string error;
            if (!start_server(listen, error)) log(YANTRA_LOG_ERROR, "remote API not started: " + error);
        }
        stop_ = false;
        worker_ = std::thread([this] {
#if defined(__linux__)
            (void)pthread_setname_np(pthread_self(), "yt-probe-work");  // OA-03
#endif
            work();
        });
        return YANTRA_OK;
    }

    void stop() {
        {
            const std::lock_guard lock(wake_m_);
            stop_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        stop_server();
        live_.clear();
    }

    void on_record(const yantra_provenance* p, const yantra_schema* s, const yantra_record* r) {
        live_.on_record(p, s, r, now_ns());
    }

    // ---- server ----------------------------------------------------------

    bool start_server(const std::string& listen, std::string& error) {
        const std::lock_guard lock(server_m_);
        if (server_) return true;
        if (folder_.empty()) {
            error = "no data folder for the token file (needs plugin API 1.1)";
            server_error_ = error;
            return false;
        }
        token_file_ = setting("api.token_file");
        if (token_file_.empty()) token_file_ = folder_ / "api_tokens.json";
        tokens_ = std::make_unique<TokenStore>(token_file_);
        bool created = false;
        if (!tokens_->load_or_create(&error, &created)) {
            server_error_ = error;
            tokens_.reset();
            return false;
        }
        if (created) log(YANTRA_LOG_INFO, "a new owner token is in " + token_file_.string());
        Backend b;
        b.control = [this](const json& r) { return control(r); };
        b.snapshot = [this] { return live_.snapshot(now_ns()); };
        b.health = [this] { return health_summary(); };
        b.coverage = [this] {
            const std::lock_guard l(summary_m_);
            return last_coverage_;
        };
        b.recording_dir = [this] {
            const json s = control({{"command", "status"}, {"args", json::object()}});
            const json r = s.value("result", json::object());
            return fs::path(r.value("recording_folder", std::string()));
        };
        b.demo = [this] { return demo(); };
        b.software = std::string("Yantra probe plugin ") + YANTRA_PROBE_PLUGIN_VERSION;
        b.source_url = YANTRA_SOURCE_URL;
        b.licence = kLicence;
        Security sec;
        sec.tokens = tokens_.get();
        sec.limiter = &limiter_;
        sec.origins = OriginPolicy(split_list(setting("api.allowed_origins")));
        router_ = std::make_shared<Router>(std::move(b), std::move(sec));
        server_ = std::make_unique<Server>(router_, [this](std::uint32_t level, const std::string& m) { log(level, m); });
        ServerConfig cfg;
        cfg.listen = listen;
        if (!server_->start(cfg, &error)) {
            server_.reset();
            router_.reset();
            tokens_.reset();
            server_error_ = error;
            return false;
        }
        server_error_.clear();
        log(YANTRA_LOG_INFO, "api http port " + std::to_string(server_->port()) + "; token file " + token_file_.string());
        return true;
    }

    void stop_server() {
        std::unique_ptr<Server> s;
        {
            const std::lock_guard lock(server_m_);
            s = std::move(server_);
        }
        if (s) {
            s->stop();
            log(YANTRA_LOG_INFO, "remote API stopped");
        }
        const std::lock_guard lock(server_m_);
        router_.reset();
        tokens_.reset();
    }

    // ---- summaries (API-03) ---------------------------------------------

    bool demo() const { return host_demo_ || live_.simulated(); }

    json health_summary() const {
        const json reply = control({{"command", "status"}, {"args", json::object()}});
        json j = {{"type", "health"}, {"utc", utc_now_iso()}, {"ok", reply.value("ok", false)}};
        if (reply.value("ok", false))
            j["status"] = reply.value("result", json::object());
        else
            j["error"] = reply.value("error", std::string());
        j["live_carriers"] = live_.snapshot(now_ns())->carriers.size();
        return j;
    }

    json coverage_summary(double period_s) {
        return {{"type", "coverage"}, {"utc", utc_now_iso()}, {"period_s", period_s}, {"carriers", live_.coverage(true)},
                {"demo", demo()}};
    }

    void write_summary(const char* kind, const json& j) {
        if (!folder_.empty()) {
            std::error_code ec;
            const fs::path dir = folder_ / "summaries";
            fs::create_directories(dir, ec);
            const fs::path file = dir / (std::string(kind) + ".jsonl");
            if (fs::exists(file, ec) && fs::file_size(file, ec) > kSummaryFileLimit)
                fs::rename(file, dir / (std::string(kind) + ".1.jsonl"), ec);  // one older file kept
            std::ofstream out(file, std::ios::app);
            out << j.dump(-1, ' ', false, json::error_handler_t::replace) << '\n';
        }
        {
            const std::lock_guard lock(server_m_);
            if (server_) server_->publish_summary(j);
        }
    }

    void work() {
        using clock = std::chrono::steady_clock;
        const double health_s = number_setting("summary.health_s", 60);
        const double coverage_s = number_setting("summary.coverage_s", 300);
        auto next_health = clock::now() + std::chrono::duration<double>(health_s);
        auto next_coverage = clock::now() + std::chrono::duration<double>(coverage_s);
        auto next_poll = clock::now();
        std::unique_lock lock(wake_m_);
        while (!stop_) {
            wake_.wait_for(lock, std::chrono::milliseconds(200));
            if (stop_) break;
            lock.unlock();
            const auto now = clock::now();
            if (health_s > 0 && now >= next_health) {
                next_health = now + std::chrono::duration<double>(health_s);
                const json h = health_summary();
                const json st = h.value("status", json::object());
                log(YANTRA_LOG_INFO, "health: " + st.value("state", std::string("unknown")) + ", " +
                                         std::to_string(h.value("live_carriers", 0)) + " carriers");
                write_summary("health", h);
            }
            if (coverage_s > 0 && now >= next_coverage) {
                next_coverage = now + std::chrono::duration<double>(coverage_s);
                const json c = coverage_summary(coverage_s);
                {
                    const std::lock_guard l(summary_m_);
                    last_coverage_ = c;
                }
                log(YANTRA_LOG_INFO, "coverage: " + std::to_string(c["carriers"].size()) + " carriers");
                write_summary("coverage", c);
            }
            if (now >= next_poll) {
                next_poll = now + std::chrono::seconds(2);
                poll_remotes();
            }
            lock.lock();
        }
    }

    // ---- remote probes (central viewer) ---------------------------------

    void load_remotes() {
        const json j = json::parse(setting("viewer.probes", "[]"), nullptr, false);
        const std::lock_guard lock(remote_m_);
        remotes_.clear();
        if (!j.is_array()) return;
        for (const json& e : j)
            if (e.is_object() && e.value("url", std::string()).size() > 0)
                remotes_.push_back(remote(e.value("url", std::string()), e.value("token", std::string())));
    }

    void save_remotes() {  // UI thread
        json j = json::array();
        {
            const std::lock_guard lock(remote_m_);
            for (const RemoteProbe& r : remotes_) j.push_back({{"url", r.url}, {"token", r.token}});
        }
        host_->settings_set(host_->host, "viewer.probes", j.dump().c_str());
    }

    void poll_remotes() {
        std::vector<std::pair<std::string, std::string>> targets;
        {
            const std::lock_guard lock(remote_m_);
            for (const RemoteProbe& r : remotes_) targets.push_back({r.url, r.token});
        }
        for (const auto& [url, token] : targets) {
            if (stop_) return;
            RemoteProbe got = remote(url, token);
            const HttpReply s = http_get(url, "/api/v1/status", token);
            if (s.status == 200) {
                got.status = json::parse(s.body, nullptr, false);
                const HttpReply l = http_get(url, "/api/v1/levels", token);
                if (l.status == 200) got.levels = json::parse(l.body, nullptr, false);
                got.ok = got.status.is_object();
                got.updated = utc_now_iso();
            } else {
                got.error = s.status == 0 ? s.error : "HTTP " + std::to_string(s.status);
            }
            const std::lock_guard lock(remote_m_);
            for (RemoteProbe& r : remotes_)
                if (r.url == url && r.token == token) {
                    r.ok = got.ok;
                    r.error = got.error;
                    if (got.ok) {
                        r.status = got.status;
                        r.levels = got.levels;
                        r.updated = got.updated;
                    }
                }
        }
    }

    // ---- UI (PLG-06) -----------------------------------------------------

    void draw(const yantra_ui_api* ui);

private:
    const yantra_host_api* host_;
    fs::path folder_;
    LiveState live_;
    mutable std::atomic<bool> host_demo_{false};  // the last status reply's demo flag (DEMO-10)

    mutable std::mutex server_m_;
    std::unique_ptr<TokenStore> tokens_;
    AuthLimiter limiter_;
    std::shared_ptr<Router> router_;
    std::unique_ptr<Server> server_;
    fs::path token_file_;
    std::string server_error_;

    std::mutex summary_m_;
    json last_coverage_ = nullptr;

    std::mutex wake_m_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::thread worker_;

    std::mutex remote_m_;
    std::vector<RemoteProbe> remotes_;

    // UI state (UI thread only)
    char listen_buf_[128] = {};
    char url_buf_[256] = "http://";
    char token_buf_[160] = {};
    double remove_number_ = 1;
};

// ------------------------------------------------------------------ UI

namespace {

yantra_ui_control control_def(const char* id, const char* label, const char* tooltip, std::uint32_t flags = 0,
                              const char* disabled_reason = nullptr) {
    return {sizeof(yantra_ui_control), flags, id, label, tooltip, nullptr, disabled_reason, nullptr};
}

std::string level_text(const json& levels) {
    if (!levels.is_object() || !levels.contains("carriers")) return "-";
    for (const json& c : levels["carriers"])
        if (c.value("role", std::string()) == "serving" && c.contains("level_dbm") && c["level_dbm"].is_number()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f dBm", c["level_dbm"].get<double>());
            return buf;
        }
    return "-";
}

}  // namespace

void Probe::draw(const yantra_ui_api* ui) {
    yantra_ui* u = ui->ui;
    ui->heading(u, "Remote API of this Yantra",
                "Other computers can follow and control this Yantra over the network (REST and WebSocket, "
                "docs/API.md). Off unless started here or with yantra-probe --api.");
    std::string error;
    int port = 0;
    {
        const std::lock_guard lock(server_m_);
        if (server_) port = server_->port();
        error = server_error_;
    }
    if (port > 0) {
        ui->status(u, YANTRA_UI_TONE_PASS, "Listening", "The remote API accepts requests with a token.");
        const yantra_ui_control pc = control_def("port", "Port", "The TCP port the remote API listens on.");
        ui->value(u, &pc, std::to_string(port).c_str(), "");
        const yantra_ui_control tf = control_def("token_file", "Token file",
                                                 "The owner token is in this file (only you can read it). "
                                                 "Clients send it as \"Authorization: Bearer <token>\".",
                                                 YANTRA_UI_VERBATIM);
        ui->value(u, &tf, token_file_.string().c_str(), "");
        const yantra_ui_control stop = control_def("stop", "Stop", "Stop the remote API; clients are disconnected.");
        if (ui->button(u, &stop)) {
            host_->settings_set(host_->host, "api.listen", "");
            stop_server();
        }
    } else {
        ui->status(u, error.empty() ? YANTRA_UI_TONE_NORMAL : YANTRA_UI_TONE_FAIL, error.empty() ? "Off" : "Could not start",
                   error.empty() ? "No network listener is open." : "See the log for the reason.");
        const yantra_ui_control lc = control_def(
            "listen", "Address", "Address and port to listen on, e.g. 127.0.0.1:8470 (this computer only) or "
                                 "0.0.0.0:8470 (the network; use a token and a firewall).",
            YANTRA_UI_VERBATIM);
        ui->text_input(u, &lc, listen_buf_, sizeof listen_buf_);
        const yantra_ui_control start = control_def("start", "Start", "Open the remote API on this address.");
        if (ui->button(u, &start)) {
            std::string why;
            if (start_server(listen_buf_, why))
                host_->settings_set(host_->host, "api.listen", listen_buf_);
            else
                log(YANTRA_LOG_ERROR, "remote API not started: " + why);
        }
    }
    ui->separator(u);
    ui->heading(u, "Remote probes",
                "The state and serving level of other Yantra probes, read every 2 s over their remote API "
                "(http:// only; use a tunnel for other networks).");
    std::vector<std::string> cells;
    std::size_t rows = 0;
    {
        const std::lock_guard lock(remote_m_);
        for (const RemoteProbe& r : remotes_) {
            ++rows;
            const json st = r.status.is_object() ? r.status.value("status", json::object()) : json::object();
            const json stat = st.is_object() ? st : json::object();
            cells.push_back(std::to_string(rows));
            cells.push_back(r.url);
            cells.push_back(r.ok ? stat.value("state", std::string("?")) : (r.error.empty() ? "..." : r.error));
            cells.push_back(r.status.is_object() && r.status.value("demo", false) ? "demo" : "");
            cells.push_back(stat.contains("carriers") ? stat["carriers"].dump() : "-");
            cells.push_back(level_text(r.levels));
            cells.push_back(r.updated.empty() ? "-" : r.updated);
        }
    }
    static const char* const headers[] = {"#", "Address", "State", "Demo", "Carriers", "Serving level", "Updated (UTC)"};
    static const char* const tips[] = {"Number of the probe in this list.",
                                       "The probe's remote API address.",
                                       "Stopped, monitoring, recording or playing; or why it cannot be read.",
                                       "The probe measures the simulator (demo mode).",
                                       "Carriers in the probe's newest measurement cycle.",
                                       "Level of the serving cell, dBm.",
                                       "When the probe last answered."};
    std::vector<const char*> cell_ptrs;
    for (const std::string& c : cells) cell_ptrs.push_back(c.c_str());
    const yantra_ui_table table{sizeof(yantra_ui_table), 7, rows, headers, tips, cell_ptrs.data()};
    const yantra_ui_control tc = control_def("probes", "Remote probes", "The probes this view follows.");
    ui->table(u, &tc, &table);
    const yantra_ui_control uc = control_def("url", "Probe address", "The probe's API address, e.g. http://192.168.1.20:8470.",
                                             YANTRA_UI_VERBATIM);
    ui->text_input(u, &uc, url_buf_, sizeof url_buf_);
    const yantra_ui_control kc = control_def("token", "Token", "The probe's owner or LAN token (from its token file).",
                                             YANTRA_UI_VERBATIM);
    ui->text_input(u, &kc, token_buf_, sizeof token_buf_);
    std::string url_error;
    std::string host, port_text;
    const bool url_ok = parse_base_url(url_buf_, host, port_text, &url_error);
    const yantra_ui_control add =
        control_def("add", "Add", "Follow this probe.", url_ok ? 0u : YANTRA_UI_DISABLED, "Give an address http://host:port first.");
    if (ui->button(u, &add) && url_ok) {
        {
            const std::lock_guard lock(remote_m_);
            remotes_.push_back(remote(url_buf_, token_buf_));
        }
        save_remotes();
        token_buf_[0] = '\0';
        wake_.notify_all();
    }
    const yantra_ui_control nc = control_def("remove_number", "Probe number", "Number of the probe to remove (# column).");
    ui->number(u, &nc, &remove_number_, 1, static_cast<double>(std::max<std::size_t>(rows, 1)), 1, "");
    const yantra_ui_control rc = control_def("remove", "Remove", "Stop following this probe.",
                                             rows == 0 ? YANTRA_UI_DISABLED : 0u, "No probe in the list.");
    if (ui->button(u, &rc) && rows > 0) {
        {
            const std::lock_guard lock(remote_m_);
            const auto k = static_cast<std::size_t>(remove_number_) - 1;
            if (k < remotes_.size()) remotes_.erase(remotes_.begin() + static_cast<std::ptrdiff_t>(k));
        }
        save_remotes();
    }
}

}  // namespace yantra::probe

// ------------------------------------------------------------------ C tables

namespace {

using yantra::probe::Probe;

yantra_status probe_init(const yantra_host_api* host, yantra_plugin_instance** out) noexcept {
    if (host == nullptr || out == nullptr) return YANTRA_ERR_INVALID_ARGUMENT;
    try {
        auto* inst = new yantra_plugin_instance;
        inst->probe = new Probe(host);
        *out = inst;
        return YANTRA_OK;
    } catch (...) {
        return YANTRA_ERR_NO_MEMORY;
    }
}

void probe_shutdown(yantra_plugin_instance* instance) noexcept {
    if (instance == nullptr) return;
    delete instance->probe;
    delete instance;
}

yantra_status probe_start(yantra_plugin_instance* instance) noexcept {
    try {
        return instance->probe->start();
    } catch (...) {
        return YANTRA_ERR_FAILED;
    }
}

void probe_stop(yantra_plugin_instance* instance) noexcept {
    try {
        instance->probe->stop();
    } catch (...) {
    }
}

void probe_on_record(yantra_plugin_instance* instance, const yantra_provenance* p, const yantra_schema* s,
                     const yantra_record* r) noexcept {
    try {
        instance->probe->on_record(p, s, r);
    } catch (...) {
    }
}

void probe_draw(yantra_plugin_instance* instance, const char* window_id, const yantra_ui_api* ui) noexcept {
    if (instance == nullptr || ui == nullptr || window_id == nullptr || std::strcmp(window_id, "remote_probes") != 0)
        return;
    try {
        instance->probe->draw(ui);
    } catch (...) {
    }
}

const yantra_feature_api feature = {
    sizeof(yantra_feature_api), 0, "probe", "Remote probe API", 0, 0, nullptr, nullptr,
    probe_start, probe_stop, probe_on_record,
};

const yantra_ui_window_def window = {
    sizeof(yantra_ui_window_def), YANTRA_UI_WINDOW, 0, 0, "remote_probes", "Remote probes",
    "This Yantra's remote API and the state of other probes (API-02).", "12.3", 640, 420,
};
const yantra_ui_window_def* const windows[] = {&window};
const yantra_ui_menu_item menu = {
    sizeof(yantra_ui_menu_item), YANTRA_MENU_TOOLS, 0, 0, "remote_probes_open", nullptr, "Remote probes",
    "This Yantra's remote API and the state of other probes.", nullptr, "remote_probes",
};
const yantra_ui_menu_item* const menu_items[] = {&menu};
const yantra_plugin_ui_api ui_table = {
    sizeof(yantra_plugin_ui_api), 1, 1, 0, windows, menu_items, probe_draw, nullptr, nullptr, nullptr,
};

const yantra_translation translations[] = {
#include "probe_translations.inc"
};

const yantra_plugin_descriptor descriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_FEATURE,
    YANTRA_CATEGORY_SYSTEM_INFO | YANTRA_CATEGORY_LEVEL | YANTRA_CATEGORY_QUALITY,
    sizeof translations / sizeof translations[0],
    "yantra.probe",
    "Remote probe API",
    YANTRA_PROBE_PLUGIN_VERSION,
    "The Yantra contributors",
    "GPL-3.0-or-later",
    "REST/WebSocket remote API, fixed-probe summaries and the remote-probes view (PLG-24).",
    YANTRA_SOURCE_URL,
    translations,
    probe_init,
    probe_shutdown,
    nullptr,
    nullptr,
    &feature,
    nullptr,
    &ui_table,
};

}  // namespace

extern "C" YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(const yantra_host_api* host) {
    if (host == nullptr || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR) return nullptr;
    return &descriptor;
}
