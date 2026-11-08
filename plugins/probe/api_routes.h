// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe's network remote API (API-02) without the network: request
// routing, authentication and the read views, so they can be tested
// directly (the CivetWeb glue is api_server.h). All endpoints live under
// /api/v1 (docs/API.md):
//   GET  /api/v1/info               no token: name, version, licence, source link (NFR-01)
//   GET  /api/v1/status             the status (health summary), API block, demo flag (DEMO-10)
//   GET  /api/v1/commands           the command table (API-01)
//   POST /api/v1/command            {"command":"record.start","args":{...}}
//   POST /api/v1/<command path>     record/start, record/stop, monitor/on, monitor/off, play,
//                                   sound/on, sound/off, status, rotate, quit; JSON body = arguments
//   GET  /api/v1/cells              carriers: sync, system information, levels, quality
//   GET  /api/v1/neighbours         the broadcast neighbour lists and the measured neighbours
//   GET  /api/v1/levels             the newest levels and quality per carrier
//   GET  /api/v1/summaries          a health summary now and the last coverage summary (API-03)
//   GET  /api/v1/recordings         the recording files
//   GET  /api/v1/recordings/<file>  download one
//   POST /api/v1/auth/rotate        a new owner token (owner token only)
//   POST /api/v1/auth/lan           {"enabled":true|false}: a new LAN token / none
//   GET  /api/v1/live (WebSocket)   the filtered live view (live.h)
// Reading needs the owner or the LAN token; commands and token changes
// the owner token. Replies are JSON objects of the versioned format
// "yantra-api" (live messages "yantra-api-live"), NFR-05.
// Copied from yantra-v1 c24e8f5: core/engine/api_routes.h/.cpp (Router,
// helpers), adapted for PLG-24: commands go through the plugin API's
// `control` service, the views come from live.h.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api_auth.h"
#include "live.h"

namespace yantra::probe {

inline constexpr const char* kApiFormat = "yantra-api";
inline constexpr int kApiVersion = 1;
inline constexpr const char* kApiPrefix = "/api/v1";
inline constexpr std::size_t kMaxBody = 64 * 1024;

// One command of API-01 (the same set as the core's control commands).
struct CommandSpec {
    const char* name;               // "record.start"
    std::vector<const char*> args;  // accepted argument names
    std::size_t required;           // how many of args are required
    const char* help;
};
const std::vector<CommandSpec>& commands();
std::string command_path(const CommandSpec& c);  // "record.start" -> "/api/v1/record/start"

// What the API reads from and drives (the plugin; fakes in tests).
struct Backend {
    // A control request {"command":..,"args":{..}} -> the reply
    // {"ok":..,"command":..,"result"|"error":..}; empty: not available.
    std::function<nlohmann::json(const nlohmann::json&)> control;
    std::function<std::shared_ptr<const Snapshot>()> snapshot;
    std::function<nlohmann::json()> health;    // a health summary now
    std::function<nlohmann::json()> coverage;  // the last coverage summary or null
    std::function<std::filesystem::path()> recording_dir;
    std::function<bool()> demo;                // simulated data (DEMO-10)
    std::string software;                      // e.g. "yantra 0.4.0, probe 1.0.0"
    std::string source_url;                    // the AGPL source link (NFR-01)
    std::string licence;
};

struct Request {
    std::string method;         // "GET", "POST", "OPTIONS", ...
    std::string path;           // decoded path without the query
    std::string query;          // raw query string (without '?')
    std::string body;
    std::string client_ip;
    std::string authorization;  // header value; empty: none
    std::string origin;         // header value; empty: none
    bool websocket = false;     // an upgrade request to /api/v1/live
};

struct Response {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    std::filesystem::path file;  // non-empty: send this file as the body (download)
    std::uint64_t file_size = 0;
    Access access = Access::None;  // granted access (for the WebSocket upgrade)
};

struct Security {
    TokenStore* tokens = nullptr;
    AuthLimiter* limiter = nullptr;
    OriginPolicy origins;
    std::function<double()> clock;  // monotonic seconds; replaceable in tests
};

class Router {
public:
    Router(Backend backend, Security security);
    // Handles one request (any thread but the main and measurement
    // threads: commands wait for Yantra). WebSocket upgrades only pass the
    // checks here (status 101 = accept); the server runs the stream.
    Response handle(const Request& req);
    const Backend& backend() const { return b_; }

private:
    Backend b_;
    Security s_;
    double now() const;
    std::optional<Response> check_origin(const Request& req, Response& cors) const;
    Access authenticate(const Request& req, std::optional<Response>& refusal);
    Response route(const Request& req, Access access);
    Response run_command(const std::string& name, const nlohmann::json& args);
};

nlohmann::json api_object(const char* type);
Response json_response(int status, const nlohmann::json& j);
Response error_response(int status, const std::string& message);
std::map<std::string, std::string> parse_query(const std::string& query);
std::string url_decode(const std::string& s);
// A recording file name the API may list and serve (no path, no dot file).
bool recording_file_name(const std::string& name);

}  // namespace yantra::probe
