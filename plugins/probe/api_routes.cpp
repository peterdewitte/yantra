// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe's remote API routing (API-02), see api_routes.h.
// Copied from yantra-v1 c24e8f5: core/engine/api_routes.cpp (helpers,
// Router), adapted for PLG-24.
#include "api_routes.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <system_error>

namespace fs = std::filesystem;

namespace yantra::probe {

namespace {

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string& s, const std::string& x) {
    return s.size() > x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct FileEntry {
    std::string name;
    std::uint64_t size = 0;
    std::int64_t mtime_s = 0;
};

std::vector<FileEntry> list_recordings(const fs::path& dir) {
    std::vector<FileEntry> out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        if (!recording_file_name(n)) continue;
        const auto st = e.symlink_status(ec);
        if (ec || !fs::is_regular_file(st)) continue;  // no symlinks: they could point anywhere
        FileEntry f;
        f.name = n;
        f.size = e.file_size(ec);
        const auto t = e.last_write_time(ec);
        f.mtime_s = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch())
                        .count();
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) { return a.name < b.name; });
    return out;
}

std::string utc_iso(std::int64_t s) {
    const std::time_t t = static_cast<std::time_t>(s);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

}  // namespace

// ------------------------------------------------------------------ commands

const std::vector<CommandSpec>& commands() {
    static const std::vector<CommandSpec> t = {
        {"record.start", {"name"}, 0, "Start recording (name optional: date/time)."},
        {"record.stop", {}, 0, "Stop recording; monitoring goes on."},
        {"monitor.on", {}, 0, "Open the configured radio and measure."},
        {"monitor.off", {}, 0, "Close the radio (a recording stops)."},
        {"play", {"file", "speed"}, 1, "Replay a recording file or folder (speed 1 = real time, 0 = fast)."},
        {"sound.on", {}, 0, "Audible alerts on."},
        {"sound.off", {}, 0, "Audible alerts off."},
        {"status", {}, 0, "Report the status (health summary)."},
        {"rotate", {}, 0, "Close the recording file and start the next one."},
        {"quit", {}, 0, "Stop recording, close the radio and exit."},
    };
    return t;
}

std::string command_path(const CommandSpec& c) {
    std::string n = c.name;
    std::replace(n.begin(), n.end(), '.', '/');
    return std::string(kApiPrefix) + "/" + n;
}

// ------------------------------------------------------------------ helpers

nlohmann::json api_object(const char* type) {
    return {{"format", kApiFormat}, {"version", kApiVersion}, {"type", type}};
}

Response json_response(int status, const nlohmann::json& j) {
    Response r;
    r.status = status;
    r.body = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return r;
}

Response error_response(int status, const std::string& message) {
    nlohmann::json j = api_object("error");
    j["ok"] = false;
    j["status"] = status;
    j["error"] = message;
    return json_response(status, j);
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '+') {
            out += ' ';
        } else if (s[k] == '%' && k + 2 < s.size() && hex_value(s[k + 1]) >= 0 && hex_value(s[k + 2]) >= 0) {
            out += static_cast<char>(hex_value(s[k + 1]) * 16 + hex_value(s[k + 2]));
            k += 2;
        } else {
            out += s[k];
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(const std::string& query) {
    std::map<std::string, std::string> q;
    std::size_t p = 0;
    while (p <= query.size()) {
        std::size_t amp = query.find('&', p);
        if (amp == std::string::npos) amp = query.size();
        const std::string part = query.substr(p, amp - p);
        if (!part.empty()) {
            const auto eq = part.find('=');
            if (eq == std::string::npos)
                q[url_decode(part)] = "";
            else
                q[url_decode(part.substr(0, eq))] = url_decode(part.substr(eq + 1));
        }
        p = amp + 1;
    }
    return q;
}

bool recording_file_name(const std::string& n) {
    if (n.empty() || n.size() > 200 || n[0] == '.' || n.find("..") != std::string::npos) return false;
    for (unsigned char ch : n)
        if (!(std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return false;
    return ends_with(n, ".yrec") || ends_with(n, ".yrec.zst");
}

// ------------------------------------------------------------------ router

Router::Router(Backend backend, Security security) : b_(std::move(backend)), s_(std::move(security)) {}

double Router::now() const {
    if (s_.clock) return s_.clock();
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::optional<Response> Router::check_origin(const Request& req, Response& cors) const {
    if (req.origin.empty()) return std::nullopt;
    if (!s_.origins.allowed(req.origin)) return error_response(403, "origin not allowed");
    cors.headers.push_back({"Access-Control-Allow-Origin", req.origin});
    cors.headers.push_back({"Vary", "Origin"});
    cors.headers.push_back({"Access-Control-Allow-Headers", "Authorization, Content-Type"});
    cors.headers.push_back({"Access-Control-Allow-Methods", "GET, POST, OPTIONS"});
    cors.headers.push_back({"Access-Control-Max-Age", "600"});
    return std::nullopt;
}

Access Router::authenticate(const Request& req, std::optional<Response>& refusal) {
    const double t = now();
    if (s_.limiter) {
        const double wait = s_.limiter->retry_after(req.client_ip, t);
        if (wait > 0) {
            refusal = error_response(429, "too many failed authentications; try again later");
            refusal->headers.push_back({"Retry-After", std::to_string(static_cast<long>(std::ceil(wait)))});
            return Access::None;
        }
    }
    const auto token = bearer_token(req.authorization);
    Access a = Access::None;
    if (token && s_.tokens) a = s_.tokens->check(*token, req.client_ip);
    if (a == Access::None) {
        if (token && s_.limiter) s_.limiter->failure(req.client_ip, t);  // a wrong token is a guess; a missing one is not
        refusal = error_response(401, token ? "invalid token" : "authentication required (Authorization: Bearer <token>)");
        refusal->headers.push_back({"WWW-Authenticate", "Bearer realm=\"yantra\""});
        return a;
    }
    if (s_.limiter) s_.limiter->success(req.client_ip);
    return a;
}

Response Router::handle(const Request& req) {
    Response cors;
    Response r;
    if (auto refused = check_origin(req, cors)) {
        r = std::move(*refused);
    } else if (req.path != kApiPrefix && !starts_with(req.path, std::string(kApiPrefix) + "/")) {
        r = error_response(404, "not found (the API lives under /api/v1)");
    } else if (req.method == "OPTIONS") {
        r.status = 204;
        r.content_type.clear();
    } else if (req.method == "GET" && req.path == std::string(kApiPrefix) + "/info") {
        nlohmann::json j = api_object("info");
        j["name"] = "yantra";
        j["software"] = b_.software;
        j["api_version"] = kApiVersion;
        j["source_url"] = b_.source_url;
        j["licence"] = b_.licence;
        r = json_response(200, j);
    } else {
        std::optional<Response> refusal;
        const Access a = authenticate(req, refusal);
        r = refusal ? std::move(*refusal) : route(req, a);
        r.access = a;
    }
    for (auto& h : cors.headers) r.headers.push_back(h);
    r.headers.push_back({"Cache-Control", "no-store"});
    return r;
}

Response Router::run_command(const std::string& name, const nlohmann::json& args) {
    if (!b_.control) return error_response(503, "commands are not available");
    nlohmann::json reply = b_.control({{"command", name}, {"args", args}});
    if (!reply.is_object()) return error_response(502, "no reply from Yantra");
    const bool ok = reply.value("ok", false);
    if (ok && name == "status" && reply.contains("result") && reply["result"].is_object()) {
        reply["result"]["source_url"] = b_.source_url;  // NFR-01
        reply["result"]["demo"] = b_.demo && b_.demo();
    }
    reply["format"] = kApiFormat;
    reply["version"] = kApiVersion;
    reply["type"] = "command";
    // 503 when Yantra refused for being busy (a modal state), 422 for a refused command.
    return json_response(ok ? 200 : 422, reply);
}

Response Router::route(const Request& req, Access access) {
    const std::string p = req.path.substr(std::string(kApiPrefix).size());
    const bool get = req.method == "GET", post = req.method == "POST";
    auto method_not_allowed = [](const char* allow) {
        Response r = error_response(405, std::string("method not allowed (use ") + allow + ")");
        r.headers.push_back({"Allow", allow});
        return r;
    };
    auto need_full = [&]() -> std::optional<Response> {
        if (access == Access::Full) return std::nullopt;
        return error_response(403, "the LAN token gives read access only; commands need the owner token");
    };
    const bool demo = b_.demo && b_.demo();

    if (p == "/live") {
        if (!get) return method_not_allowed("GET");
        if (!req.websocket) return error_response(400, "the live view is a WebSocket (Upgrade: websocket)");
        LiveFilter f = default_live_filter();
        std::string err;
        if (!apply_filter_query(f, parse_query(req.query), &err)) return error_response(400, err);
        Response r;
        r.status = 101;
        r.content_type.clear();
        return r;
    }

    if (p == "/command") {
        if (!post) return method_not_allowed("POST");
        if (auto f = need_full()) return *f;
        const auto j = nlohmann::json::parse(req.body, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return error_response(400, "the body is not a JSON object");
        const auto c = j.find("command");
        if (c == j.end() || !c->is_string()) return error_response(400, "missing \"command\"");
        const auto known = std::find_if(commands().begin(), commands().end(),
                                        [&](const CommandSpec& s) { return *c == s.name; });
        if (known == commands().end()) return error_response(400, "unknown command: " + c->get<std::string>().substr(0, 40));
        const nlohmann::json args = j.value("args", nlohmann::json::object());
        if (!args.is_object()) return error_response(400, "\"args\" must be an object");
        return run_command(known->name, args);
    }
    for (const CommandSpec& info : commands()) {
        if (req.path != command_path(info)) continue;
        if (std::string(info.name) == "status" && get) break;  // GET /status is the read view below
        if (!post) return method_not_allowed(std::string(info.name) == "status" ? "GET, POST" : "POST");
        if (auto f = need_full()) return *f;
        nlohmann::json args = nlohmann::json::object();
        if (!req.body.empty()) {
            args = nlohmann::json::parse(req.body, nullptr, false);
            if (!args.is_object()) return error_response(400, "the body must be a JSON object of arguments");
        }
        return run_command(info.name, args);
    }

    if (p == "/auth/rotate" || p == "/auth/lan") {
        if (!post) return method_not_allowed("POST");
        if (auto f = need_full()) return *f;
        if (!s_.tokens) return error_response(503, "no token store");
        std::string err;
        nlohmann::json j = api_object("token");
        if (p == "/auth/rotate") {
            if (!s_.tokens->rotate(&err)) return error_response(500, err);
            j["token"] = s_.tokens->token();
        } else {
            const auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
            if (!body.is_object() || !body.contains("enabled") || !body["enabled"].is_boolean())
                return error_response(400, "body: {\"enabled\": true|false}");
            if (!s_.tokens->set_lan(body["enabled"].get<bool>(), &err)) return error_response(500, err);
            const std::string lan = s_.tokens->lan_token();
            j["lan_token"] = lan.empty() ? nlohmann::json(nullptr) : nlohmann::json(lan);
        }
        return json_response(200, j);
    }

    if (!get)
        return starts_with(p, "/recordings") || p == "/status" || p == "/commands" || p == "/cells" ||
                       p == "/neighbours" || p == "/levels" || p == "/summaries"
                   ? method_not_allowed("GET")
                   : error_response(404, "no such endpoint");
    if (p == "/status") {
        nlohmann::json j = api_object("status");
        j["demo"] = demo;
        j["source_url"] = b_.source_url;
        j["api"] = {{"version", kApiVersion},
                    {"server", b_.software},
                    {"licence", b_.licence},
                    {"source_url", b_.source_url},
                    {"access", access_name(access)}};
        nlohmann::json status = nullptr;
        if (b_.control) {
            const nlohmann::json reply = b_.control({{"command", "status"}, {"args", nlohmann::json::object()}});
            if (reply.value("ok", false)) status = reply.value("result", nlohmann::json(nullptr));
        }
        j["status"] = status;
        return json_response(200, j);
    }
    if (p == "/commands") {
        nlohmann::json j = api_object("commands");
        nlohmann::json a = nlohmann::json::array();
        for (const CommandSpec& c : commands()) {
            nlohmann::json args = nlohmann::json::array();
            for (std::size_t k = 0; k < c.args.size(); ++k) args.push_back({{"name", c.args[k]}, {"required", k < c.required}});
            a.push_back({{"command", c.name}, {"path", command_path(c)}, {"args", args}, {"help", c.help}});
        }
        j["commands"] = a;
        return json_response(200, j);
    }
    if (p == "/cells" || p == "/neighbours" || p == "/levels") {
        const auto snap = b_.snapshot ? b_.snapshot() : nullptr;
        const Snapshot empty;
        const Snapshot& s = snap ? *snap : empty;
        nlohmann::json j = p == "/cells" ? cells_json(s) : p == "/neighbours" ? neighbours_json(s) : levels_json(s);
        j["format"] = kApiFormat;
        j["version"] = kApiVersion;
        j["demo"] = demo;
        return json_response(200, j);
    }
    if (p == "/summaries") {
        nlohmann::json j = api_object("summaries");
        j["demo"] = demo;
        j["health"] = b_.health ? b_.health() : nlohmann::json(nullptr);
        j["coverage"] = b_.coverage ? b_.coverage() : nlohmann::json(nullptr);
        return json_response(200, j);
    }
    if (p == "/recordings") {
        const fs::path dir = b_.recording_dir ? b_.recording_dir() : fs::path();
        nlohmann::json j = api_object("recordings");
        nlohmann::json a = nlohmann::json::array();
        for (const FileEntry& f : list_recordings(dir))
            a.push_back({{"name", f.name}, {"size", f.size}, {"modified", utc_iso(f.mtime_s)},
                         {"url", std::string(kApiPrefix) + "/recordings/" + f.name}});
        j["files"] = a;
        return json_response(200, j);
    }
    if (starts_with(p, "/recordings/")) {
        const std::string name = p.substr(12);
        if (!recording_file_name(name)) return error_response(404, "no such recording");
        const fs::path dir = b_.recording_dir ? b_.recording_dir() : fs::path();
        for (const FileEntry& f : list_recordings(dir)) {
            if (f.name != name) continue;
            Response r;
            r.file = dir / name;
            r.file_size = f.size;
            r.content_type = "application/octet-stream";
            r.headers.push_back({"Content-Disposition", "attachment; filename=\"" + name + "\""});
            return r;
        }
        return error_response(404, "no such recording");
    }
    return error_response(404, "no such endpoint");
}

}  // namespace yantra::probe
