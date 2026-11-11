// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 10.4 models: File > Export (EXP-01, EXP-03, EXP-04, EXP-08), File >
// Report (EXP-05, EXP-06) with the acceptance profile editor, and EXP-07
// sharing to a local folder and to local fake HTTP and SMTP servers on the
// loopback interface (no external service).
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "curl_sender.h"
#include "yantra/app/app_state.h"
#include "yantra/app/report_export.h"
#include "yantra/app/share.h"
#include "yantra/core/project/project.h"
#include "yantra/core/report/document.h"
#include "yantra/core/survey/acceptance.h"

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
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "report_export" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::string read(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

#ifndef _WIN32
// A loopback server speaking just enough HTTP (PUT) or SMTP for the sender;
// it keeps what it received.
class FakeServer {
public:
    enum class Kind { Http, Smtp };
    explicit FakeServer(Kind kind, int http_status = 201) : kind_(kind), status_(http_status) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd_ >= 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        REQUIRE(::listen(fd_, 8) == 0);
        socklen_t n = sizeof a;
        REQUIRE(::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &n) == 0);
        port_ = ntohs(a.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeServer() {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        thread_.join();
    }
    std::string base() const {
        return std::string(kind_ == Kind::Http ? "http" : "smtp") + "://127.0.0.1:" + std::to_string(port_);
    }
    std::vector<std::string> received() const {
        std::lock_guard<std::mutex> l(mutex_);
        return received_;
    }

private:
    static void put(int c, const std::string& s) {
        std::size_t sent = 0;
        while (sent < s.size()) {
            const auto r = ::send(c, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
            if (r <= 0) return;
            sent += static_cast<std::size_t>(r);
        }
    }
    // Reads until `end` is in `buf` (true) or the peer closes (false).
    static bool until(int c, std::string& buf, const std::string& end) {
        char b[4096];
        while (buf.find(end) == std::string::npos) {
            const auto r = ::recv(c, b, sizeof b, 0);
            if (r <= 0) return false;
            buf.append(b, static_cast<std::size_t>(r));
        }
        return true;
    }
    void keep(std::string s) {
        std::lock_guard<std::mutex> l(mutex_);
        received_.push_back(std::move(s));
    }
    void http(int c) {
        std::string buf;
        if (!until(c, buf, "\r\n\r\n")) return;
        const std::size_t head_end = buf.find("\r\n\r\n") + 4;
        std::size_t length = 0;
        const auto cl = buf.find("Content-Length: ");
        if (cl != std::string::npos && cl < head_end) length = std::stoul(buf.substr(cl + 16));
        if (buf.find("Expect: 100-continue") != std::string::npos) put(c, "HTTP/1.1 100 Continue\r\n\r\n");
        char b[4096];
        while (buf.size() < head_end + length) {
            const auto r = ::recv(c, b, sizeof b, 0);
            if (r <= 0) break;
            buf.append(b, static_cast<std::size_t>(r));
        }
        keep(buf);
        put(c, "HTTP/1.1 " + std::to_string(status_) + " X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    }
    void smtp(int c) {
        put(c, "220 fake ESMTP\r\n");
        std::string all;
        std::string buf;
        while (true) {
            if (!until(c, buf, "\r\n")) break;
            const std::size_t e = buf.find("\r\n");
            const std::string line = buf.substr(0, e);
            buf.erase(0, e + 2);
            all += line + "\n";
            if (line.rfind("EHLO", 0) == 0) {
                put(c, "250-fake\r\n250 OK\r\n");
            } else if (line.rfind("DATA", 0) == 0) {
                put(c, "354 go on\r\n");
                if (!until(c, buf, "\r\n.\r\n")) break;
                const std::size_t d = buf.find("\r\n.\r\n");
                all += buf.substr(0, d + 5);
                buf.erase(0, d + 5);
                put(c, "250 queued\r\n");
            } else if (line.rfind("QUIT", 0) == 0) {
                put(c, "221 bye\r\n");
                break;
            } else {
                put(c, "250 OK\r\n");
            }
        }
        keep(all);
    }
    void serve() {
        while (true) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) return;
            if (kind_ == Kind::Http)
                http(c);
            else
                smtp(c);
            ::close(c);
        }
    }

    Kind kind_;
    int status_;
    int fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::vector<std::string> received_;
};
#endif

// A Sender that keeps its jobs (the model's view of the network).
class KeepSender final : public Sender {
public:
    std::string send(const ShareJob& job) override {
        std::lock_guard<std::mutex> l(mutex);
        jobs.push_back(job);
        return {};
    }
    std::mutex mutex;
    std::vector<ShareJob> jobs;
};

AppPaths paths_with_tetra(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.user_data = dir / "data";
    p.home = dir / "home";
    p.data_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR)};
    fs::create_directories(p.home);
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra" YANTRA_PLUGIN_SUFFIX, folder / "tetra" YANTRA_PLUGIN_SUFFIX,
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

// Runs the open dialog's job to its end, as the frames do.
void finish(AppState& st) {
    REQUIRE(st.output() != nullptr);
    st.output()->wait();
    st.tick();
}

}  // namespace

TEST_CASE("EXP-07 sharing: the settings are checked, a folder copy, refusals; nothing without a method") {
    const fs::path dir = scratch("share");
    SharingSettings s;
    CHECK_FALSE(check_sharing(s).empty());  // off
    s.method = SharingSettings::Method::Folder;
    CHECK_FALSE(check_sharing(s).empty());
    s.folder = dir / "shared";
    CHECK(check_sharing(s).empty());
    s.method = SharingSettings::Method::Http;
    s.url = "ftp://x";
    CHECK_FALSE(check_sharing(s).empty());
    s.url = "https://upload.example.org/surveys";
    CHECK(check_sharing(s).empty());
    s.method = SharingSettings::Method::Smtp;
    s.smtp_server = "mail.example.org";
    CHECK_FALSE(check_sharing(s).empty());
    s.smtp_server = "smtp://mail.example.org:587";
    s.mail_from = "survey@example.org";
    s.mail_to = "a@example.org; b@example.org ,";
    CHECK(check_sharing(s).empty());
    CHECK(mail_recipients(s.mail_to) == std::vector<std::string>{"a@example.org", "b@example.org"});
    s.mail_to = "a@example.org, not an address";
    CHECK_FALSE(check_sharing(s).empty());

    std::ofstream(dir / "a.csv", std::ios::binary) << "x\n1\n";  // the bytes compared below
    ShareJob job;
    job.settings.method = SharingSettings::Method::Folder;
    job.settings.folder = dir / "shared";
    job.files = {dir / "a.csv"};
    CHECK_FALSE(share_files(job, nullptr).empty());  // the folder is not there
    fs::create_directories(dir / "shared");
    CHECK(share_files(job, nullptr).empty());
    CHECK(read(dir / "shared" / "a.csv") == "x\n1\n");
    CHECK_FALSE(fs::exists(dir / "shared" / "a.csv.part"));
    job.files = {dir / "missing.csv"};
    CHECK_FALSE(share_files(job, nullptr).empty());
    job.files = {dir / "a.csv"};
    job.settings.method = SharingSettings::Method::Http;
    job.settings.url = "http://127.0.0.1:9";
    CHECK_FALSE(share_files(job, nullptr).empty());  // no network sender
    const auto none = make_no_sender("not set up");
    CHECK(share_files(job, none.get()) == "not set up");

    // The settings round-trip through the settings file.
    AppSettings a;
    a.sharing.method = SharingSettings::Method::Smtp;
    a.sharing.smtp_server = "smtps://mail.example.org";
    a.sharing.mail_from = "survey@example.org";
    a.sharing.mail_to = "a@example.org";
    a.sharing.user = "survey";
    a.sharing.on_walk_end = true;
    const fs::path f = dir / "app.json";
    std::string error;
    REQUIRE(save_settings(f, a, error));
    const AppSettings b = load_settings(f, error);
    CHECK(b.sharing == a.sharing);
    CHECK(read(f).find("password") == std::string::npos);  // never stored
}

#ifndef _WIN32
TEST_CASE("EXP-07 sharing through libcurl to local fake servers: HTTP PUT, an HTTP refusal, SMTP with attachments") {
    const fs::path dir = scratch("curl");
    std::ofstream(dir / "points.csv") << "time_utc,rssi\n2026-11-08T10:00:00Z,-71.5\n";
    std::ofstream(dir / "plan floor.png", std::ios::binary) << std::string("\x89PNG\r\n\x1a\n", 8) << "data";
    const auto sender = make_curl_sender();
    ShareJob job;
    job.files = {dir / "points.csv", dir / "plan floor.png"};
    job.subject = "Yantra export: Test";
    job.text = "Sent by Yantra.";
    {
        FakeServer http(FakeServer::Kind::Http);
        job.settings.method = SharingSettings::Method::Http;
        job.settings.url = http.base() + "/up/";
        job.settings.user = "survey";
        job.password = "secret";
        CHECK(share_files(job, sender.get()) == "");
        const auto got = http.received();
        REQUIRE(got.size() == 2);
        CHECK(got[0].rfind("PUT /up/points.csv HTTP/1.1\r\n", 0) == 0);
        CHECK(got[0].find("Content-Type: text/csv") != std::string::npos);
        CHECK(got[0].find("Authorization: Basic ") != std::string::npos);
        CHECK(got[0].find("2026-11-08T10:00:00Z,-71.5") != std::string::npos);
        CHECK(got[1].rfind("PUT /up/plan%20floor.png HTTP/1.1\r\n", 0) == 0);
        CHECK(got[1].find("Content-Type: image/png") != std::string::npos);
    }
    {
        FakeServer refuses(FakeServer::Kind::Http, 403);
        job.settings.url = refuses.base();
        job.settings.user.clear();
        const std::string why = share_files(job, sender.get());
        CHECK_FALSE(why.empty());
        CHECK(why.find("points.csv") != std::string::npos);
        REQUIRE(refuses.received().size() == 1);  // stops at the first refusal
        CHECK(refuses.received()[0].find("Authorization") == std::string::npos);
    }
    {
        FakeServer smtp(FakeServer::Kind::Smtp);
        job.settings.method = SharingSettings::Method::Smtp;
        job.settings.smtp_server = smtp.base();
        job.settings.mail_from = "survey@example.org";
        job.settings.mail_to = "a@example.org, b@example.org";
        job.settings.user.clear();
        CHECK(share_files(job, sender.get()) == "");
        const auto got = smtp.received();
        REQUIRE(got.size() == 1);
        const std::string& m = got[0];
        CHECK(m.find("MAIL FROM:<survey@example.org>") != std::string::npos);
        CHECK(m.find("RCPT TO:<a@example.org>") != std::string::npos);
        CHECK(m.find("RCPT TO:<b@example.org>") != std::string::npos);
        CHECK(m.find("Subject: Yantra export: Test") != std::string::npos);
        CHECK(m.find("Sent by Yantra.") != std::string::npos);
        CHECK(m.find("filename=\"points.csv\"") != std::string::npos);
        CHECK(m.find("filename=\"plan floor.png\"") != std::string::npos);
        CHECK(m.find("Content-Transfer-Encoding: base64") != std::string::npos);
    }
}
#endif

#ifdef YANTRA_TEST_DEMO_PROJECT
TEST_CASE("EXP-01/03/08 File > Export on the demo: CSV columns, separator and time, DXF, floor picture, refusals") {
    const fs::path dir = scratch("export");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    AppState st({}, paths_with_tetra(dir));
    CHECK_FALSE(st.action(Action::Export).enabled);  // no project
    CHECK_FALSE(st.action(Action::Report).enabled);
    REQUIRE(st.open_project(dir / "fraunces.yantra").ok);
    REQUIRE(st.action(Action::Export).enabled);
    REQUIRE(st.trigger(Action::Export).ok);
    REQUIRE(st.dialog() == Dialog::Export);
    ExportForm& f = st.output()->export_form();
    CHECK(f.file == dir / "exports" / "fraunces.csv");
    CHECK(f.batch_folder == dir / "recordings");

    // CSV: three columns, semicolons, local time.
    const auto all = exports::all_columns();
    f.columns.assign(all.size(), false);
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i] == exports::Column::Time || all[i] == exports::Column::Floor || all[i] == exports::Column::Level)
            f.columns[i] = true;
    f.separator = 1;
    f.local_time = true;
    REQUIRE(st.confirm_export().ok);
    CHECK(st.dialog() == Dialog::None);
    const std::string csv = read(f.file);
    const std::string head = csv.substr(0, csv.find('\n'));
    CHECK(head == "time_local;floor;rssi;rssi_unit");
    CHECK(std::count(csv.begin(), csv.end(), '\n') > 10);

    // Every floor in one file has more rows than the shown floor.
    REQUIRE(st.trigger(Action::Export).ok);
    ExportForm& g = st.output()->export_form();
    g.all_floors = true;
    g.file = dir / "exports" / "all.csv";
    REQUIRE(st.confirm_export().ok);
    CHECK(read(g.file).size() > csv.size());

    // No columns, a wrong extension: refused, the dialog stays.
    REQUIRE(st.trigger(Action::Export).ok);
    ExportForm& h = st.output()->export_form();
    h.columns.assign(all.size(), false);
    CHECK_FALSE(st.confirm_export().ok);
    CHECK(st.dialog() == Dialog::Export);
    h.kind = ExportForm::Kind::Dxf;
    h.file = dir / "exports" / "floor.csv";
    CHECK_FALSE(st.confirm_export().ok);
    h.file = dir / "exports" / "floor.dxf";
    REQUIRE(st.confirm_export().ok);
    const std::string dxf = read(h.file);
    CHECK(dxf.find("SECTION") != std::string::npos);
    CHECK(dxf.find("EOF") != std::string::npos);

    // The floor picture, with and without the map, one per floor.
    REQUIRE(st.trigger(Action::Export).ok);
    ExportForm& im = st.output()->export_form();
    im.kind = ExportForm::Kind::Image;
    im.file = dir / "exports" / "floor.png";
    im.image_px = 400;
    im.all_floors = true;
    REQUIRE(st.confirm_export().ok);
    std::size_t pictures = 0;
    for (const auto& e : fs::directory_iterator(dir / "exports"))
        if (e.path().extension() == ".png") {
            ++pictures;
            CHECK(read(e.path()).rfind("\x89PNG", 0) == 0);
        }
    CHECK(pictures == st.output()->floor_names().size());
    CHECK(pictures >= 2);

    // EXP-07 with the export: copied to the shared folder after writing.
    fs::create_directories(dir / "shared");
    REQUIRE(st.trigger(Action::Export).ok);
    REQUIRE(st.open_sharing().ok);
    CHECK(st.dialog() == Dialog::Sharing);
    st.sharing_form().method = SharingSettings::Method::Folder;
    CHECK_FALSE(st.confirm_sharing().ok);  // no folder yet
    CHECK(st.dialog() == Dialog::Sharing);
    st.sharing_form().folder = dir / "shared";
    REQUIRE(st.confirm_sharing().ok);
    CHECK(st.dialog() == Dialog::Export);
    CHECK(st.settings().sharing.method == SharingSettings::Method::Folder);
    CHECK(st.output()->export_form().kind == ExportForm::Kind::Image);  // the last kind is kept
    st.output()->export_form().kind = ExportForm::Kind::Csv;
    st.output()->export_form().share = true;
    st.output()->export_form().file = dir / "exports" / "sent.csv";
    REQUIRE(st.confirm_export().ok);
    finish(st);
    CHECK(read(dir / "shared" / "sent.csv") == read(dir / "exports" / "sent.csv"));
}

TEST_CASE("EXP-04/PLG-02 batch export of the demo's recordings: merged, separate, cancel, in the background") {
    const fs::path dir = scratch("batch");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    fs::copy(fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path() / "recordings", dir / "recordings",
             fs::copy_options::recursive);
    AppState st({}, paths_with_tetra(dir));
    REQUIRE(st.open_project(dir / "fraunces.yantra").ok);
    REQUIRE(st.trigger(Action::Export).ok);
    ExportForm& f = st.output()->export_form();
    f.kind = ExportForm::Kind::Batch;
    CHECK(st.output()->batch_formats().empty());  // no exporter plugin loaded
    REQUIRE(st.confirm_export().ok);
    CHECK(st.dialog() == Dialog::None);
    finish(st);
    CHECK_FALSE(st.status_is_error());
    CHECK(st.status().find("Batch export") != std::string::npos);
    const std::string merged = read(dir / "exports" / "fraunces_batch.csv");
    CHECK(std::count(merged.begin(), merged.end(), '\n') > 10);
    CHECK_FALSE(st.output()->busy());

    REQUIRE(st.trigger(Action::Export).ok);
    ExportForm& g = st.output()->export_form();
    g.kind = ExportForm::Kind::Batch;
    g.batch_separate = true;
    g.batch_output = dir / "separate";
    REQUIRE(st.confirm_export().ok);
    finish(st);
    std::size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir / "separate"))
        if (e.path().extension() == ".csv") ++files;
    CHECK(files >= 1);
    CHECK(st.output()->job().files.size() == files);

    REQUIRE(st.trigger(Action::Export).ok);
    st.output()->export_form().kind = ExportForm::Kind::Batch;
    st.output()->export_form().batch_folder = dir / "nothing";
    REQUIRE(st.confirm_export().ok);
    finish(st);
    CHECK(st.status_is_error());
}

TEST_CASE("EXP-05/EXP-06 File > Report: surveyor, floors, profiles, PDF and HTML; the profile editor") {
    const fs::path dir = scratch("report");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    AppState st({}, paths_with_tetra(dir));
    REQUIRE(st.open_project(dir / "fraunces.yantra").ok);

    // The profile editor: add two, a bad one is refused, save, back.
    REQUIRE(st.trigger(Action::Report).ok);
    REQUIRE(st.dialog() == Dialog::Report);
    REQUIRE(st.open_profiles().ok);
    REQUIRE(st.dialog() == Dialog::AcceptanceProfiles);
    ProfileForm& pf = st.output()->profile_form();
    const std::size_t stored = pf.profiles.size();
    st.output()->add_profile();
    st.output()->add_profile();
    REQUIRE(pf.profiles.size() == stored + 2);
    CHECK(pf.profiles[stored].name != pf.profiles[stored + 1].name);
    pf.profiles[stored + 1].name = pf.profiles[stored].name;
    CHECK_FALSE(st.output()->check_profiles().empty());
    CHECK_FALSE(st.confirm_profiles().ok);
    CHECK(st.dialog() == Dialog::AcceptanceProfiles);
    pf.profiles[stored + 1].name = "Strict";
    pf.profiles[stored + 1].criteria = {survey::AcceptanceCriterion{-80.0, 99.0}};
    pf.profiles[stored + 1].criteria.push_back({-70.0, 150.0});
    CHECK_FALSE(st.confirm_profiles().ok);
    pf.profiles[stored + 1].criteria.pop_back();
    REQUIRE(st.confirm_profiles().ok);
    CHECK(st.dialog() == Dialog::Report);
    CHECK(survey::acceptance_profiles(*st.project()).size() == stored + 2);
    const auto names = st.output()->profile_names();
    REQUIRE(names.size() == stored + 2);
    CHECK(names.back() == "Strict");

    // The report with one floor, one chosen profile and the surveyor.
    ReportForm& r = st.output()->report_form();
    CHECK(r.file == dir / "reports" / "fraunces_report.pdf");
    r.surveyor = "Surveyor Name";
    r.floors.assign(st.output()->floor_names().size(), false);
    r.floors[0] = true;
    r.choose_profiles = true;
    r.profiles.assign(names.size(), false);
    CHECK_FALSE(st.confirm_report().ok);  // no profile chosen
    r.profiles.back() = true;
    r.more_thresholds = "-85, x";
    CHECK_FALSE(st.confirm_report().ok);
    r.more_thresholds = "-85; -105";
    CHECK(st.output()->report_written() == false);
    REQUIRE(st.confirm_report().ok);
    CHECK(st.dialog() == Dialog::None);
    CHECK(st.output()->busy() == st.output()->job().running);
    finish(st);
    CHECK_FALSE(st.status_is_error());
    CHECK(st.output()->report_written());
    const fs::path pdf = dir / "reports" / "fraunces_report.pdf";
    const fs::path html = dir / "reports" / "fraunces_report.html";
    REQUIRE(fs::exists(pdf));
    REQUIRE(fs::exists(html));
    const std::string bytes = read(pdf);
    CHECK(report::check_pdf(std::vector<std::uint8_t>(bytes.begin(), bytes.end())).ok);
    const std::string page = read(html);
    CHECK(page.find("Surveyor Name") != std::string::npos);
    CHECK(page.find("Strict") != std::string::npos);
    CHECK(page.find(st.output()->floor_names()[1].second) == std::string::npos);
    // The surveyor is not stored anywhere.
    CHECK(read(dir / "fraunces.yantra").find("Surveyor Name") == std::string::npos);
    CHECK(read(dir / "config" / "app.json").find("Surveyor Name") == std::string::npos);

    // HTML only, and neither is refused.
    REQUIRE(st.trigger(Action::Report).ok);
    CHECK(st.output()->report_form().surveyor == "Surveyor Name");  // kept for this run
    st.output()->report_form().pdf = false;
    st.output()->report_form().html = false;
    CHECK_FALSE(st.confirm_report().ok);
    st.cancel_dialog();
    CHECK(st.dialog() == Dialog::None);
}

TEST_CASE("EXP-07 a walk test that ends sends its floor's CSV when asked; the report is sent after it is written") {
    const fs::path dir = scratch("walk_end");
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "fraunces.yantra");
    std::shared_ptr<project::Project> p = project::Project::open(dir / "fraunces.yantra");
    auto keep = std::make_shared<KeepSender>();
    SharingSettings sharing;
    sharing.method = SharingSettings::Method::Http;
    sharing.url = "https://upload.example.org/surveys";
    ReportExport::Context c;
    c.sharing = [&sharing] { return sharing; };
    c.sender = [keep] { return keep; };
    c.data_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR)};
    c.now_utc_ns = [] { return std::int64_t{1794096000} * 1000000000; };
    ReportExport out(p, c);
    const std::string floor = p->floors().front().id;
    CHECK(out.walk_finished(floor).ok);  // not asked: nothing
    out.wait();
    CHECK_FALSE(out.tick());
    CHECK(keep->jobs.empty());
    sharing.on_walk_end = true;
    out.set_share_password("pw");
    REQUIRE(out.walk_finished(floor).ok);
    out.wait();
    const auto r = out.tick();
    REQUIRE(r);
    CHECK(r->ok);
    REQUIRE(keep->jobs.size() == 1);
    CHECK(keep->jobs[0].password == "pw");
    REQUIRE(keep->jobs[0].files.size() == 1);
    CHECK(keep->jobs[0].files[0].parent_path() == dir / "exports");
    CHECK(fs::exists(keep->jobs[0].files[0]));
    CHECK(keep->jobs[0].subject.find("walk test") != std::string::npos);

    // The report, then its sending.
    out.open_report();
    out.report_form().floors.assign(p->floors().size(), false);
    out.report_form().floors[0] = true;
    out.report_form().share = true;
    REQUIRE(out.run_report().ok);
    CHECK_FALSE(out.run_report().ok);  // busy
    out.wait();
    const auto made = out.tick();
    REQUIRE(made);
    CHECK(made->ok);
    out.wait();
    REQUIRE(out.tick());
    REQUIRE(keep->jobs.size() == 2);
    CHECK(keep->jobs[1].files.size() == 2);
    CHECK(keep->jobs[1].subject.find("report") != std::string::npos);
}
#endif
