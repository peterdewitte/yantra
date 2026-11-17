// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.10: the guided tour's model (DEMO-11, DEMO-12, DEMO-08) without
// a window. No network: the MAK sheet is a synthetic stand-in (a JPEG
// test pattern, no MAK data) with its own description copy and hash,
// fetched through libcurl from a file:// URL or a loopback HTTP server
// in this process. The failure paths (refused consent, offline, wrong
// hash, size or pixel size) fall back to the bundled Fraunces Tavern
// with the message.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "curl_downloader.h"
#include "plan_fixtures.h"
#include "yantra/app/app_state.h"
#include "yantra/app/settings.h"
#include "yantra/app/sheet_fetch.h"
#include "yantra/app/tour.h"
#include "yantra/core/build_info.h"
#include "yantra/core/project/project.h"
#include "yantra/core/util/sha256.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace yantra;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "tour" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::string file_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string sha256_of(const fs::path& p) {
    util::Sha256 h;
    h.update(file_bytes(p));
    return h.finish_hex();
}

nlohmann::json read_json(const fs::path& p) {
    std::ifstream in(p);
    return nlohmann::json::parse(in);
}

const fs::path kData = YANTRA_TEST_SOURCE_DATA_DIR;
const char* kMak = "mak_beer_house";

// A test setup: a data folder holding a copy of one MAK description with a
// synthetic stand-in sheet (its own hash) in front of the source data
// (Fraunces and the other descriptions), a user folder and a sheet cache.
struct Setup {
    fs::path dir, data, served, cache;
    fs::path image;  // the stand-in as served
    nlohmann::json desc;
    AppPaths paths;

    explicit Setup(const std::string& name) : dir(scratch(name)) {
        data = dir / "data";
        served = dir / "served";
        cache = dir / "cache";
        fs::create_directories(data / "demo" / "mak");
        fs::create_directories(served);
        desc = read_json(kData / "demo" / "mak" / (std::string(kMak) + ".json"));
        auto& s = desc["sheets"][0];
        const int w = s["width"].get<int>(), h = s["height"].get<int>();
        const auto bytes = plan::fixtures::jpeg(plan::fixtures::pattern(w, h), 80);
        image = served / s["file"].get<std::string>();
        std::ofstream(image, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        s["sha256"] = sha256_of(image);
        s["size_bytes"] = fs::file_size(image);
        // The description keeps an https address (it is checked); the
        // test's downloader maps it to the local fixture.
        s["source_url"] = "https://example.invalid/sheets/" + s["file"].get<std::string>();
        fs::copy_file(kData / "demo" / "mak" / "cells.csv", data / "demo" / "mak" / "cells.csv");
        write();
        paths.settings_file = dir / "config" / "app.json";
        paths.user_data = dir / "user";
        paths.home = dir / "home";
        paths.data_folders = {data, kData};
        paths.sheet_cache = cache;
        fs::create_directories(paths.home);
    }
    ~Setup() {
        std::error_code ec;
        fs::remove_all(dir, ec);  // no stand-in named like a MAK sheet stays behind
    }
    void write() const {
        std::ofstream(data / "demo" / "mak" / (std::string(kMak) + ".json")) << desc.dump(1) << "\n";
    }
    std::string sheet_file() const { return desc["sheets"][0]["file"].get<std::string>(); }
    std::string url() const { return desc["sheets"][0]["source_url"].get<std::string>(); }
};

// The libcurl downloader with the description's address mapped to a local
// fixture (file:// or loopback http): the real transfer, no network.
class MappedDownloader final : public Downloader {
public:
    MappedDownloader(std::string from, std::string to, CurlOptions o = {})
        : inner_(make_curl_downloader(o)), from_(std::move(from)), to_(std::move(to)) {}
    std::string fetch(const std::string& url, const fs::path& file, Progress& progress) override {
        if (url != from_) return "unexpected address " + url;
        return inner_->fetch(to_, file, progress);
    }

private:
    std::shared_ptr<Downloader> inner_;
    std::string from_, to_;
};

std::shared_ptr<Downloader> local(const Setup& s, CurlOptions o = {}) {
    return std::make_shared<MappedDownloader>(s.url(), "file://" + s.image.string(), o);
}

#ifndef _WIN32
// A loopback HTTP server for one file: answers every GET with it (or with
// 404), and keeps the request heads it got.
class LoopbackServer {
public:
    explicit LoopbackServer(std::string body) : body_(std::move(body)) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        REQUIRE(::listen(fd_, 4) == 0);
        socklen_t n = sizeof a;
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &n) == 0);
        port_ = ntohs(a.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~LoopbackServer() {
        stop_ = true;
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        thread_.join();
    }
    std::string url(const std::string& name) const { return "http://127.0.0.1:" + std::to_string(port_) + "/" + name; }
    std::vector<std::string> requests() const {
        std::lock_guard<std::mutex> l(mutex_);
        return requests_;
    }

private:
    void serve() {
        while (!stop_) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) return;
            std::string head;
            char buf[1024];
            while (head.find("\r\n\r\n") == std::string::npos) {
                const auto r = ::recv(c, buf, sizeof buf, 0);
                if (r <= 0) break;
                head.append(buf, static_cast<std::size_t>(r));
            }
            {
                std::lock_guard<std::mutex> l(mutex_);
                requests_.push_back(head);
            }
            const std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                                      std::to_string(body_.size()) + "\r\nConnection: close\r\n\r\n" + body_;
            std::size_t sent = 0;
            while (sent < reply.size()) {
                const auto r = ::send(c, reply.data() + sent, reply.size() - sent, MSG_NOSIGNAL);
                if (r <= 0) break;
                sent += static_cast<std::size_t>(r);
            }
            ::close(c);
        }
    }

    std::string body_;
    int fd_ = -1;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::vector<std::string> requests_;
};

// A loopback port with nothing listening: "offline".
int closed_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    (void)::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    socklen_t n = sizeof a;
    (void)::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n);
    ::close(fd);
    return ntohs(a.sin_port);
}
#endif

bool cache_clean(const Setup& s) {
    // Nothing half-downloaded or refused stays in the cache.
    std::error_code ec;
    if (!fs::exists(s.cache, ec)) return true;
    for (const auto& e : fs::recursive_directory_iterator(s.cache))
        if (e.is_regular_file()) return false;
    return true;
}

// Runs every step's "Show me" from the current step up to `last`,
// checking the step is done afterwards.
void show_through(AppState& st, TourStep last) {
    Tour& t = st.tour();
    while (t.view().step != last) {
        const TourView v = t.view();
        CAPTURE(v.key);
        if (v.can_show) {
            const Result r = t.show_me();
            CAPTURE(r.message);
            REQUIRE(r.ok);
            st.tick();
            CHECK(t.view().done);
        }
        REQUIRE(t.next().ok);
    }
}

}  // namespace

TEST_CASE("DEMO-11 the tour's steps, keys and settings round trip") {
    for (int i = 0; i < kTourStepCount; ++i) {
        const auto s = static_cast<TourStep>(i);
        CHECK(parse_tour_step(tour_step_key(s)) == s);
    }
    CHECK_FALSE(parse_tour_step("no_such_step"));
    AppSettings a;
    a.tour.offered = true;
    a.tour.building = "fraunces";
    a.tour.step = "scale";
    a.tour.done = {"choose", "new_project"};
    std::string error;
    const AppSettings b = settings_from_json(settings_to_json(a), error);
    CHECK(error.empty());
    CHECK(b.tour == a.tour);
    // A damaged tour object keeps the defaults.
    const AppSettings c = settings_from_json(R"({"tour": {"offered": 3, "done": "x"}})", error);
    CHECK(c.tour == TourProgress{});
    CHECK(download_client_name().rfind("Yantra/", 0) == 0);
    CHECK(download_client_name() == "Yantra/" + std::string(core::build_info().version_string));
}

TEST_CASE("DEMO-11 the first-start offer: once, Not now hides it, Help > Tour stays") {
    Setup s("offer");
    AppState st({}, s.paths);
    CHECK(st.tour_offer());
    CHECK(st.answer_tour_offer(false).ok);
    CHECK_FALSE(st.tour_offer());
    CHECK_FALSE(st.tour().active());
    CHECK(st.settings().tour.offered);
    CHECK(st.settings_changed());
    // Yes starts the tour on its first step, in demo mode (DEMO-07).
    AppState st2({}, s.paths);
    REQUIRE(st2.answer_tour_offer(true).ok);
    CHECK(st2.tour().active());
    const TourView v = st2.tour().view();
    CHECK(v.step == TourStep::Choose);
    CHECK(v.demo_label.find("DEMO") != std::string::npos);
    REQUIRE(v.buildings.size() == 4);
    CHECK(v.buildings.back().id == "fraunces");
    CHECK_FALSE(v.buildings.back().restricted);
    CHECK(v.buildings.front().restricted);
    CHECK_FALSE(v.buildings.front().cached);
    CHECK_FALSE(v.can_show);
    CHECK_FALSE(v.show_reason.empty());
    CHECK_FALSE(st2.tour().next().ok);  // a building first
}

TEST_CASE("DEMO-11 DEMO-08 the tour with the Fraunces Tavern: every step by Show me, progress kept, Leave and back") {
    Setup s("fraunces");
    AppState st({}, s.paths);
    REQUIRE(st.trigger(Action::Tour).ok);
    Tour& t = st.tour();
    REQUIRE(t.choose("fraunces").ok);
    CHECK(st.dialog() == Dialog::None);  // bundled: nothing to ask
    // DEMO-11: the plans step says that nothing was downloaded (no folder).
    REQUIRE(t.view().step == TourStep::Downloaded);
    CHECK(t.view().done);
    CHECK(t.view().folder.empty());
    CHECK_FALSE(t.view().can_show);
    CHECK(t.view().text.find("nothing is downloaded") != std::string::npos);
    CHECK_FALSE(t.open_folder().ok);
    REQUIRE(t.next().ok);
    REQUIRE(t.view().step == TourStep::NewProject);
    CHECK(t.view().building == "Fraunces Tavern");
    CHECK(t.view().targets.front() == "file.new_project");

    REQUIRE(t.show_me().ok);
    REQUIRE(st.project() != nullptr);
    CHECK(st.project()->path() == t.project_file());
    CHECK(t.view().done);
    // DEMO-07: the tour's project carries the demo label from the start.
    REQUIRE(st.project_view());
    CHECK(st.project_view()->demo_label.find("DEMO") != std::string::npos);
    REQUIRE(t.next().ok);

    // Import, trim, second source, scale.
    CHECK_FALSE(t.view().done);
    show_through(st, TourStep::MouseSurvey);
    const auto floors = st.project()->floors();
    REQUIRE(floors.size() == 2);  // one from each of the two sheets
    CHECK(st.project()->sources().size() == 2);
    for (const auto& f : floors) {
        CHECK_FALSE(f.example_id.empty());
        CHECK(st.project()->building().has_scale(f));
    }
    CHECK(st.project()->check().empty());
    // The Fraunces plans are bundled: no MAK credit line.
    CHECK_FALSE(st.plan_credit(floors[0].id));

    // Leave keeps the step; the next start goes on there.
    t.leave();
    CHECK_FALSE(t.active());
    CHECK(st.settings().tour.step == "mouse_survey");
    CHECK(st.settings().tour.building == "fraunces");
    const auto done = st.settings().tour.done;
    CHECK(std::find(done.begin(), done.end(), "scale") != done.end());
    REQUIRE(st.trigger(Action::Tour).ok);
    CHECK(t.view().step == TourStep::MouseSurvey);
    // Back repeats a step.
    REQUIRE(t.back().ok);
    CHECK(t.view().step == TourStep::Scale);
    CHECK(t.view().done);
    // Skip to the report (step 10.4): available, not done before a report.
    REQUIRE(t.go_to(TourStep::Report).ok);
    CHECK(t.view().available);
    CHECK(t.view().can_show);
    CHECK(t.view().unavailable.empty());
    CHECK_FALSE(t.view().done);
    REQUIRE(t.next().ok);
    CHECK(t.view().last);
    REQUIRE(t.next().ok);  // Finish
    CHECK_FALSE(t.active());
    CHECK(st.settings().tour.finished);

    // The progress is saved with the settings and read again.
    std::string error;
    REQUIRE(st.save_settings(error));
    const AppSettings again = load_settings(s.paths.settings_file, error);
    CHECK(again.tour == st.settings().tour);

    // UI-16: Reset all settings starts the tour over.
    REQUIRE(st.trigger(Action::ResetSettings).ok);
    REQUIRE(st.confirm_reset_settings().ok);
    CHECK(st.settings().tour == TourProgress{});
    CHECK_FALSE(st.tour().active());
    CHECK(st.tour().description() == nullptr);
    CHECK(st.tour_offer());
}

TEST_CASE("DEMO-12 a MAK example: consent first, then the stand-in sheet fetched (file://), checked, referenced, credited") {
    Setup s("mak-file");
    AppState st({}, s.paths);
    st.set_downloader(local(s));
    REQUIRE(st.trigger(Action::Tour).ok);
    Tour& t = st.tour();
    const Result chosen = t.choose(kMak);
    CAPTURE(chosen.message);
    REQUIRE(chosen.ok);
    // Nothing is fetched before the answer.
    CHECK(st.dialog() == Dialog::TourConsent);
    REQUIRE(t.consent());
    CHECK(t.consent()->url == s.url());
    CHECK(t.consent()->licence == "CC BY-NC-ND 4.0");
    CHECK_FALSE(t.consent()->credit.empty());
    CHECK(t.consent()->privacy.find("Yantra") != std::string::npos);
    CHECK(t.consent()->cache_file == (s.cache / s.sheet_file()).string());
    CHECK_FALSE(t.downloading());
    CHECK(cache_clean(s));

    REQUIRE(t.answer_consent(true).ok);
    CHECK(st.dialog() == Dialog::None);
    t.wait_download();
    CHECK_FALSE(t.downloading());
    const TourView v = t.view();
    CAPTURE(v.message);
    CHECK_FALSE(v.message_error);
    REQUIRE(t.description() != nullptr);
    CHECK(t.description()->id == kMak);
    // DEMO-11: the step after the download shows the cache folder and opens it.
    CHECK(v.step == TourStep::Downloaded);
    CHECK(v.folder == s.cache.string());
    CHECK(v.can_open_folder);
    CHECK(v.targets.front() == "tour.open_folder");
    std::vector<fs::path> opened;
    st.set_document_opener([&](const fs::path& f, std::string&) {
        opened.push_back(f);
        return true;
    });
    REQUIRE(t.open_folder().ok);
    CHECK(opened == std::vector<fs::path>{s.cache});
    const fs::path kept = s.cache / s.sheet_file();
    REQUIRE(fs::exists(kept));
    CHECK(sha256_of(kept) == s.desc["sheets"][0]["sha256"].get<std::string>());  // unchanged
    REQUIRE(v.credit);
    CHECK(v.credit->licence == "CC BY-NC-ND 4.0");
    CHECK(v.credit->scale_note.find("approximate") != std::string::npos);

    // Several floors from the one sheet, an approximate scale.
    show_through(st, TourStep::SecondSource);
    const auto floors = st.project()->floors();
    CHECK(floors.size() == s.desc["floors"].size());
    REQUIRE(st.project()->sources().size() == 1);
    // IND-03: referenced by hash, the image is not in the project.
    CHECK_FALSE(st.project()->sources()[0].plan.embedded);
    CHECK(st.project()->sources()[0].plan.sha256 == s.desc["sheets"][0]["sha256"].get<std::string>());
    // One sheet: the second source has nothing to do, said so.
    CHECK(t.view().done);
    CHECK_FALSE(t.view().can_show);
    CHECK(t.view().text.find("one sheet") != std::string::npos);
    REQUIRE(t.next().ok);
    REQUIRE(t.view().step == TourStep::Scale);
    CHECK(t.view().text.find("approximate") != std::string::npos);
    show_through(st, TourStep::MouseSurvey);
    for (const auto& f : st.project()->floors()) CHECK(st.project()->building().has_scale(f));
    // DEMO-12: credit, licence and the approximate scale with the plan.
    const auto c = st.plan_credit(floors[0].id);
    REQUIRE(c);
    CHECK(c->licence == "CC BY-NC-ND 4.0");
    CHECK_FALSE(c->credit.empty());
    CHECK_FALSE(c->scale_note.empty());
    // The plan draws from the referenced file (trims at display time only).
    ProjectWindow* w = st.project_window();
    REQUIRE(w != nullptr);
    CHECK(w->plan_tiles(floors[0].id, 64u << 20).source != nullptr);

    // A second choice finds the checked sheet in the cache: no question.
    t.leave();
    AppState st2(st.settings(), s.paths);  // the saved settings, as at the next start
    REQUIRE(st2.trigger(Action::Tour).ok);
    CHECK(st2.tour().view().step == TourStep::MouseSurvey);  // the remembered step, sheet still there
    REQUIRE(st2.tour().go_to(TourStep::Choose).ok);
    const auto bv = st2.tour().view().buildings;
    CHECK(bv.front().cached);
    REQUIRE(st2.tour().choose(kMak).ok);
    CHECK(st2.dialog() == Dialog::None);
    CHECK(st2.tour().description()->id == kMak);
}

#ifndef _WIN32
TEST_CASE("DEMO-12 the download over loopback HTTP sends only the plain request with the generic client name") {
    Setup s("mak-http");
    LoopbackServer server(file_bytes(s.image));
    AppState st({}, s.paths);
    st.set_downloader(std::make_shared<MappedDownloader>(s.url(), server.url(s.sheet_file())));
    REQUIRE(st.trigger(Action::Tour).ok);
    REQUIRE(st.tour().choose(kMak).ok);
    REQUIRE(st.tour().answer_consent(true).ok);
    st.tour().wait_download();
    CAPTURE(st.tour().view().message);
    REQUIRE(st.tour().description() != nullptr);
    CHECK(st.tour().description()->id == kMak);
    const auto req = server.requests();
    REQUIRE(req.size() == 1);
    CHECK(req[0].rfind("GET /" + s.sheet_file() + " HTTP/1.1\r\n", 0) == 0);
    CHECK(req[0].find("User-Agent: " + download_client_name() + "\r\n") != std::string::npos);
    // Nothing else: no cookie, referrer or user data (NFR-09).
    for (const char* h : {"Cookie:", "Referer:", "From:", "Authorization:"}) CHECK(req[0].find(h) == std::string::npos);
    std::size_t lines = 0;
    for (std::size_t p = req[0].find("\r\n"); p != std::string::npos; p = req[0].find("\r\n", p + 2)) ++lines;
    CHECK(lines <= 5);  // request line, Host, User-Agent, Accept, the end
}

TEST_CASE("DEMO-12 offline: the tour says why and continues with the Fraunces Tavern") {
    Setup s("mak-offline");
    AppState st({}, s.paths);
    st.set_downloader(std::make_shared<MappedDownloader>(
        s.url(), "http://127.0.0.1:" + std::to_string(closed_port()) + "/" + s.sheet_file(), CurlOptions{5, 5}));
    REQUIRE(st.trigger(Action::Tour).ok);
    REQUIRE(st.tour().choose(kMak).ok);
    REQUIRE(st.tour().answer_consent(true).ok);
    st.tour().wait_download();
    const TourView v = st.tour().view();
    CHECK(v.message_error);
    CHECK(v.message.find("failed") != std::string::npos);
    CHECK(v.message.find("Fraunces Tavern") != std::string::npos);
    REQUIRE(st.tour().description() != nullptr);
    CHECK(st.tour().description()->id == "fraunces");
    // DEMO-11: the plans step says the bundled Fraunces Tavern is used.
    CHECK(v.step == TourStep::Downloaded);
    CHECK(v.folder.empty());
    CHECK(v.text.find("Fraunces Tavern") != std::string::npos);
    CHECK(cache_clean(s));
}
#endif

TEST_CASE("DEMO-12 refused consent, no downloader, cancel: Fraunces Tavern with the message, nothing fetched") {
    Setup s("mak-refused");
    {
        AppState st({}, s.paths);
        st.set_downloader(local(s));
        REQUIRE(st.trigger(Action::Tour).ok);
        REQUIRE(st.tour().choose(kMak).ok);
        REQUIRE(st.tour().answer_consent(false).ok == false);  // a fallback is reported as an error
        CHECK(st.tour().description()->id == "fraunces");
        CHECK(st.tour().view().message.find("chose not to download") != std::string::npos);
        CHECK(cache_clean(s));
    }
    {
        // Closing the question (Esc) is no.
        AppState st({}, s.paths);
        st.set_downloader(local(s));
        REQUIRE(st.trigger(Action::Tour).ok);
        REQUIRE(st.tour().choose(kMak).ok);
        REQUIRE(st.dialog() == Dialog::TourConsent);
        st.cancel_dialog();
        CHECK(st.dialog() == Dialog::None);
        CHECK_FALSE(st.tour().consent());
        CHECK(st.tour().description()->id == "fraunces");
        CHECK(cache_clean(s));
    }
    {
        // A copy without downloads (the default AppState).
        AppState st({}, s.paths);
        REQUIRE(st.trigger(Action::Tour).ok);
        REQUIRE(st.tour().choose(kMak).ok);
        REQUIRE(st.tour().answer_consent(true).ok);
        st.tour().wait_download();
        CHECK(st.tour().description()->id == "fraunces");
        CHECK(st.tour().view().message.find("not set up") != std::string::npos);
    }
}

TEST_CASE("DEMO-12 a different file (hash, size or pixel size) is refused with both hashes and the URL; Fraunces instead") {
    struct Case {
        const char* name;
        std::function<void(nlohmann::json&, const fs::path&)> edit;
        const char* reason;
    };
    const std::vector<Case> cases{
        {"hash", [](nlohmann::json& j, const fs::path&) { j["sha256"] = std::string(64, 'a'); }, "SHA-256"},
        {"size", [](nlohmann::json& j, const fs::path&) { j["size_bytes"] = j["size_bytes"].get<std::uint64_t>() + 1; }, "size"},
        {"pixels", [](nlohmann::json& j, const fs::path&) { j["width"] = j["width"].get<int>() + 1; }, "pixel"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.name);
        Setup s(std::string("mak-wrong-") + c.name);
        const std::string actual = sha256_of(s.image);
        c.edit(s.desc["sheets"][0], s.image);
        s.write();
        AppState st({}, s.paths);
        st.set_downloader(local(s));
        REQUIRE(st.trigger(Action::Tour).ok);
        REQUIRE(st.tour().choose(kMak).ok);
        REQUIRE(st.tour().answer_consent(true).ok);
        st.tour().wait_download();
        const TourView v = st.tour().view();
        CAPTURE(v.message);
        CHECK(v.message_error);
        CHECK(v.message.find(s.desc["sheets"][0]["sha256"].get<std::string>()) != std::string::npos);
        CHECK(v.message.find(actual) != std::string::npos);
        CHECK(v.message.find(s.desc["sheets"][0]["source_url"].get<std::string>()) != std::string::npos);
        CHECK(v.message.find("Fraunces Tavern") != std::string::npos);
        CHECK(st.tour().description()->id == "fraunces");
        CHECK(cache_clean(s));  // the refused file is not kept
    }
}

TEST_CASE("DEMO-12 a wrong file already in the cache is not used: the question names it") {
    Setup s("mak-cached-wrong");
    fs::create_directories(s.cache);
    std::ofstream(s.cache / s.sheet_file()) << "not a plan";
    AppState st({}, s.paths);
    REQUIRE(st.trigger(Action::Tour).ok);
    const auto bv = st.tour().view().buildings;
    CHECK_FALSE(bv.front().cached);
    REQUIRE(st.tour().choose(kMak).ok);
    REQUIRE(st.tour().consent());
    CHECK(st.tour().consent()->privacy.find(s.desc["sheets"][0]["sha256"].get<std::string>()) != std::string::npos);
    st.cancel_dialog();
    fs::remove(s.cache / s.sheet_file());
}
