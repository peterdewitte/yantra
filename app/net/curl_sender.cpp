// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The libcurl Sender, see curl_sender.h.
#include "curl_sender.h"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>
#include <string>

#include "yantra/app/sheet_fetch.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::string content_type(const fs::path& f) {
    std::string e = f.extension().string();
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (e == ".pdf") return "application/pdf";
    if (e == ".html" || e == ".htm") return "text/html; charset=utf-8";
    if (e == ".csv") return "text/csv; charset=utf-8";
    if (e == ".png") return "image/png";
    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".dxf") return "image/vnd.dxf";
    return "application/octet-stream";
}

// The error text of a transfer.
std::string failure(CURLcode rc, const char* error) {
    return error[0] != '\0' ? std::string(error) : std::string(curl_easy_strerror(rc));
}

void common(CURL* curl, const CurlOptions& o, char* error, const std::string& agent) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, o.connect_timeout_s);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, o.stall_timeout_s);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
}

std::size_t discard(char*, std::size_t size, std::size_t n, void*) { return size * n; }

class CurlSender final : public Sender {
public:
    explicit CurlSender(CurlOptions o) : options_(o) {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    std::string send(const ShareJob& job) override {
        switch (job.settings.method) {
        case SharingSettings::Method::Http: return upload(job);
        case SharingSettings::Method::Smtp: return mail(job);
        default: return "not a network method";
        }
    }

private:
    std::string upload(const ShareJob& job) {
        std::string base = job.settings.url;
        if (!base.empty() && base.back() != '/') base += '/';
        for (const fs::path& f : job.files) {
            CURL* curl = curl_easy_init();
            if (curl == nullptr) return "libcurl could not start";
            char* name = curl_easy_escape(curl, f.filename().string().c_str(), 0);
            const std::string url = base + (name != nullptr ? name : "");
            curl_free(name);
            std::FILE* in = std::fopen(f.c_str(), "rb");
            if (in == nullptr) {
                curl_easy_cleanup(curl);
                return "cannot read " + f.string();
            }
            std::error_code ec;
            const auto size = fs::file_size(f, ec);
            char error[CURL_ERROR_SIZE] = {0};
            const std::string agent = download_client_name();
            const std::string type = "Content-Type: " + content_type(f);
            curl_slist* headers = curl_slist_append(nullptr, type.c_str());
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READDATA, in);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(ec ? 0 : size));
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard);
            if (!job.settings.user.empty()) {
                curl_easy_setopt(curl, CURLOPT_USERNAME, job.settings.user.c_str());
                curl_easy_setopt(curl, CURLOPT_PASSWORD, job.password.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            }
            common(curl, options_, error, agent);
            const CURLcode rc = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            std::fclose(in);
            if (rc != CURLE_OK) return f.filename().string() + ": " + failure(rc, error);
        }
        return {};
    }

    std::string mail(const ShareJob& job) {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) return "libcurl could not start";
        char error[CURL_ERROR_SIZE] = {0};
        const std::string agent = download_client_name();
        const std::string from = "<" + job.settings.mail_from + ">";
        curl_slist* rcpt = nullptr;
        std::string to_header = "To: ";
        bool first = true;
        for (const std::string& a : mail_recipients(job.settings.mail_to)) {
            rcpt = curl_slist_append(rcpt, ("<" + a + ">").c_str());
            to_header += (first ? "" : ", ") + a;
            first = false;
        }
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("From: " + job.settings.mail_from).c_str());
        headers = curl_slist_append(headers, to_header.c_str());
        headers = curl_slist_append(headers, ("Subject: " + job.subject).c_str());
        headers = curl_slist_append(headers, ("X-Mailer: " + agent).c_str());
        curl_mime* mime = curl_mime_init(curl);
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_data(part, job.text.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/plain; charset=utf-8");
        for (const fs::path& f : job.files) {
            part = curl_mime_addpart(mime);
            curl_mime_filedata(part, f.c_str());
            curl_mime_filename(part, f.filename().string().c_str());
            curl_mime_type(part, content_type(f).c_str());
            curl_mime_encoder(part, "base64");
        }
        curl_easy_setopt(curl, CURLOPT_URL, job.settings.smtp_server.c_str());
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "smtp,smtps");
        curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_TRY));
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        if (!job.settings.user.empty()) {
            curl_easy_setopt(curl, CURLOPT_USERNAME, job.settings.user.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, job.password.c_str());
        }
        common(curl, options_, error, agent);
        const CURLcode rc = curl_easy_perform(curl);
        curl_mime_free(mime);
        curl_slist_free_all(headers);
        curl_slist_free_all(rcpt);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK) return failure(rc, error);
        return {};
    }

    CurlOptions options_;
};

}  // namespace

std::shared_ptr<Sender> make_curl_sender(CurlOptions options) { return std::make_shared<CurlSender>(options); }

}  // namespace yantra::app
