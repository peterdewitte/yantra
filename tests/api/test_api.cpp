// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe plugin's parts without the network (API-02, PLG-24): tokens,
// the limiter and the Origin policy, the router with a fake backend, and
// the live view built from records (technology-neutral, scope-limited).
// The socket-level test against the real engine is api_client_test.py
// (TST-51).
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include <sys/stat.h>

#include "api_auth.h"
#include "api_routes.h"
#include "live.h"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace yantra::probe;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path p = fs::path(YANTRA_BINARY_DIR) / "tests" / "api" / "scratch" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

// A small technology schema: the radio API's core fields plus one field of
// each category the probe serves and one it keeps (DEVICE).
const yantra_enum_item kRoles[] = {{1, "serving", "Serving"}, {2, "neighbour", "Neighbour"}, {3, "other", "Other"}};
const yantra_field_def kFreq = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, 0,
                                "frequency_hz", "Frequency", "Hz", nullptr, nullptr, 0, 0};
const yantra_field_def kRole = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO, 0,
                                "role", "Role", "", nullptr, kRoles, 3, 0};
const yantra_field_def kLevel = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL, 0,
                                 "level", "Level", "dBm", nullptr, nullptr, 0, 0};
const yantra_field_def kCell = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
                                YANTRA_FIELD_KEY, "cell_id", "Cell", "", nullptr, nullptr, 0, 0};
const yantra_field_def kMer = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, 0,
                               "mer_db", "MER", "dB", nullptr, nullptr, 0, 0};
const yantra_field_def kTemp = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_DEVICE, 0,
                                "temperature_c", "Temperature", "C", nullptr, nullptr, 0, 0};
const yantra_field_def* const kFields[] = {&kFreq, &kRole, &kLevel, &kCell, &kMer, &kTemp};
const yantra_schema kSchema = {sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 6, kFields};

const yantra_field_def kEntry = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, 0,
                                 "entry_index", "Entry", "", nullptr, nullptr, 0, 0};
const yantra_field_def kCount = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO, 0,
                                 "entry_count", "Entries", "", nullptr, nullptr, 0, 0};
const yantra_field_def* const kNbFields[] = {&kFreq, &kEntry, &kCount, &kCell};
const yantra_schema kNbSchema = {sizeof(yantra_schema), YANTRA_SCHEMA_NEIGHBOUR, "x_nb", "Neighbours", 1, 4, kNbFields};

const yantra_provenance kProv = {sizeof(yantra_provenance), 1, "xtech", "x.plugin", "1.0", "x_cell"};

void feed(LiveState& live, std::uint64_t hz, std::int64_t role, double level, std::uint32_t flags, std::int64_t now) {
    const yantra_value v[] = {yantra_value_uint(hz), yantra_value_enum(role), yantra_value_double(level),
                              yantra_value_uint(17), yantra_value_double(21.5), yantra_value_double(40.0)};
    yantra_record r{sizeof(yantra_record), 0, {now, 0, 0, 0}, flags, 6, v};
    live.on_record(&kProv, &kSchema, &r, now);
}

}  // namespace

TEST_CASE("API-02 tokens: created owner-only, LAN token read access from LAN addresses only, rotation") {
    const fs::path dir = scratch("tokens");
    TokenStore t(dir / "sub" / "api_tokens.json");
    bool created = false;
    std::string e;
    REQUIRE_MESSAGE(t.load_or_create(&e, &created), e);
    CHECK(created);
    struct stat st{};
    REQUIRE(::stat(t.path().c_str(), &st) == 0);
    CHECK((st.st_mode & 077) == 0);
    const std::string owner = t.token();
    CHECK(owner.size() == 64);
    CHECK(t.check(owner, "203.0.113.5") == Access::Full);
    CHECK(t.check("x", "127.0.0.1") == Access::None);
    REQUIRE(t.set_lan(true, &e));
    const std::string lan = t.lan_token();
    CHECK(t.check(lan, "192.168.1.9") == Access::Read);
    CHECK(t.check(lan, "127.0.0.1") == Access::Read);
    CHECK(t.check(lan, "203.0.113.5") == Access::None);
    REQUIRE(t.rotate(&e));
    CHECK(t.check(owner, "127.0.0.1") == Access::None);
    TokenStore again(t.path());
    REQUIRE(again.load_or_create(&e, &created));
    CHECK_FALSE(created);
    CHECK(again.token() == t.token());
    ::chmod(t.path().c_str(), 0644);
    TokenStore open_file(t.path());
    CHECK_FALSE(open_file.load_or_create(&e));
    CHECK(e.find("chmod 600") != std::string::npos);
}

TEST_CASE("API-02 the limiter and the Origin policy") {
    AuthLimiter l({3, 60, 30});
    for (int k = 0; k < 3; ++k) l.failure("10.0.0.1", 1.0 + k);
    CHECK(l.blocked("10.0.0.1", 4.0));
    CHECK_FALSE(l.blocked("10.0.0.2", 4.0));
    CHECK_FALSE(l.blocked("10.0.0.1", 40.0));
    OriginPolicy o({"https://Viewer.example:8443/"});
    CHECK(o.allowed("https://viewer.example:8443"));
    CHECK_FALSE(o.allowed("https://evil.example"));
    CHECK_FALSE(o.allowed("null"));
    CHECK_FALSE(OriginPolicy({"*"}).allowed("https://any.example"));
}

TEST_CASE("API-02 the router: info without a token, LAN read-only, commands through control, errors") {
    const fs::path dir = scratch("router");
    TokenStore t(dir / "api_tokens.json");
    REQUIRE(t.load_or_create());
    REQUIRE(t.set_lan(true));
    AuthLimiter limiter;
    std::vector<json> requests;
    Backend b;
    b.control = [&](const json& r) {
        requests.push_back(r);
        if (r["command"] == "rotate") return json{{"ok", false}, {"command", "rotate"}, {"error", "no recording runs"}};
        return json{{"ok", true}, {"command", r["command"]}, {"result", {{"state", "monitoring"}}}};
    };
    b.demo = [] { return true; };
    b.source_url = "https://github.com/peterdewitte/yantra";
    b.software = "test";
    b.licence = "GPL-3.0-or-later";
    Security s;
    s.tokens = &t;
    s.limiter = &limiter;
    s.origins = OriginPolicy({"https://viewer.example"});
    double clock = 0;
    s.clock = [&] { return clock; };
    Router router(b, s);
    auto req = [&](const std::string& method, const std::string& path, const std::string& token = {},
                   const std::string& body = {}, const std::string& origin = {}) {
        Request r;
        r.method = method;
        r.path = path;
        r.body = body;
        r.client_ip = "127.0.0.1";
        if (!token.empty()) r.authorization = "Bearer " + token;
        r.origin = origin;
        return router.handle(r);
    };
    Response r = req("GET", "/api/v1/info");
    CHECK(r.status == 200);
    CHECK(json::parse(r.body)["source_url"] == "https://github.com/peterdewitte/yantra");
    CHECK(req("GET", "/api/v1/status").status == 401);
    r = req("GET", "/api/v1/status", t.token());
    REQUIRE(r.status == 200);
    CHECK(json::parse(r.body)["status"]["state"] == "monitoring");
    CHECK(json::parse(r.body)["demo"] == true);
    r = req("POST", "/api/v1/record/start", t.token(), R"({"name":"a"})");
    CHECK(r.status == 200);
    CHECK(requests.back() == json{{"command", "record.start"}, {"args", {{"name", "a"}}}});
    CHECK(req("POST", "/api/v1/rotate", t.token()).status == 422);
    CHECK(req("POST", "/api/v1/record/start", t.lan_token()).status == 403);
    CHECK(req("GET", "/api/v1/levels", t.lan_token()).status == 200);
    CHECK(req("POST", "/api/v1/command", t.token(), R"({"command":"gps.on"})").status == 400);
    CHECK(req("GET", "/api/v1/status", t.token(), "", "https://evil.example").status == 403);
    r = req("GET", "/api/v1/status", t.token(), "", "https://viewer.example");
    CHECK(r.status == 200);
    CHECK(std::find(r.headers.begin(), r.headers.end(),
                    std::pair<std::string, std::string>{"Access-Control-Allow-Origin", "https://viewer.example"}) !=
          r.headers.end());
    CHECK(req("GET", "/api/v1/nothing", t.token()).status == 404);
    CHECK(req("DELETE", "/api/v1/cells", t.token()).status == 405);
    CHECK(req("GET", "/api/v1/recordings/..%2Fx.yrec", t.token()).status == 404);
    // DEMO-10: Yantra's own demo flag counts before the first simulated record.
    Backend b2 = b;
    b2.demo = [] { return false; };
    b2.control = [](const json& in) {
        return json{{"ok", true}, {"command", in["command"]}, {"result", {{"state", "monitoring"}, {"demo", true}}}};
    };
    Router router2(b2, s);
    Request q;
    q.method = "GET";
    q.path = "/api/v1/status";
    q.client_ip = "127.0.0.1";
    q.authorization = "Bearer " + t.token();
    CHECK(json::parse(router2.handle(q).body)["demo"] == true);
    q.method = "POST";
    CHECK(json::parse(router2.handle(q).body)["result"]["demo"] == true);
    CHECK_FALSE(recording_file_name("../a.yrec"));
    CHECK_FALSE(recording_file_name("api_tokens.json"));
    CHECK(recording_file_name("a_0000.yrec.zst"));
}

TEST_CASE("API-02 the live view keeps the newest state per carrier, serves SYSTEM_INFO, LEVEL and QUALITY only") {
    LiveState live;
    const std::int64_t t0 = 1'000'000'000;
    feed(live, 390000000, 1, -70.5, YANTRA_RECORD_SIMULATED, t0);
    feed(live, 390100000, 2, -85.0, YANTRA_RECORD_SIMULATED | YANTRA_RECORD_NO_SYNC, t0);
    const yantra_value nb[] = {yantra_value_uint(390000000), yantra_value_uint(0), yantra_value_uint(1),
                               yantra_value_uint(18)};
    yantra_record nr{sizeof(yantra_record), 1, {t0, 0, 0, 0}, 0, 4, nb};
    live.on_record(&kProv, &kNbSchema, &nr, t0);
    auto s = live.snapshot(t0 + 1'000'000'000);
    REQUIRE(s->carriers.size() == 2);
    CHECK(s->simulated);
    const json cells = cells_json(*s);
    const json& serving = cells["carriers"][0];
    CHECK(serving["role"] == "serving");
    CHECK(serving["synced"] == true);
    CHECK(serving["level_dbm"] == -70.5);
    CHECK(serving["sysinfo"]["cell_id"] == 17);
    CHECK(serving["quality"]["mer_db"]["value"] == 21.5);
    CHECK(serving.dump().find("temperature") == std::string::npos);  // DEVICE fields stay in the probe
    CHECK(cells["carriers"][1]["synced"] == false);
    const json n = neighbours_json(*s);
    REQUIRE(n["broadcast"].size() == 1);
    CHECK(n["broadcast"][0]["neighbours"][0]["cell_id"] == 18);
    CHECK(n["measured"][0]["frequency_hz"] == 390100000);
    CHECK(sysinfo_json(*s)["carriers"].size() == 1);  // the unsynchronised one has no identity
    LiveFilter f = default_live_filter();
    std::string e;
    REQUIRE(apply_filter_query(f, {{"carrier", "390100000"}, {"types", "levels"}}, &e));
    CHECK(levels_json(*s, &f)["carriers"].size() == 1);
    CHECK_FALSE(apply_filter_query(f, {{"rate", "50"}}, &e));
    CHECK_FALSE(apply_filter_json(f, json{{"types", {"voice"}}}, &e));
    // Stream rounds: sync/levels only when new records came.
    LiveView v(f);
    CHECK(v.step(*s, true, 0).size() == 1);
    CHECK(v.step(*s, true, 1).empty());
    // Carriers silent for too long drop out; coverage counts and resets.
    CHECK(live.snapshot(t0 + 60'000'000'000)->carriers.empty());
    const json cov = live.coverage(true);
    REQUIRE(cov.size() == 2);
    CHECK(cov[0]["records"] == 1);
    CHECK(cov[0]["level_mean_dbm"] == -70.5);
    CHECK(live.coverage(false).empty());
}
