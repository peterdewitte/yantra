// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The libcurl Downloader, see curl_downloader.h.
#include "curl_downloader.h"

#include <curl/curl.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

struct Sink {
    std::FILE* file = nullptr;
    Downloader::Progress* progress = nullptr;
    bool write_failed = false;
};

std::size_t on_data(char* data, std::size_t size, std::size_t n, void* user) {
    auto* s = static_cast<Sink*>(user);
    const std::size_t bytes = size * n;
    if (std::fwrite(data, 1, bytes, s->file) != bytes) {
        s->write_failed = true;
        return 0;  // stops the transfer
    }
    s->progress->got += bytes;
    return bytes;
}

int on_progress(void* user, curl_off_t total, curl_off_t, curl_off_t, curl_off_t) {
    auto* s = static_cast<Sink*>(user);
    if (total > 0) s->progress->total = static_cast<std::uint64_t>(total);
    return s->progress->cancel.load() ? 1 : 0;  // non-zero aborts
}

class CurlDownloader final : public Downloader {
public:
    explicit CurlDownloader(CurlOptions o) : options_(o) {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    std::string fetch(const std::string& url, const fs::path& file, Progress& progress) override {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) return "libcurl could not start";
        std::FILE* out = std::fopen(file.string().c_str(), "wb");
        if (out == nullptr) {
            curl_easy_cleanup(curl);
            return "cannot write " + file.string();
        }
        Sink sink{out, &progress, false};
        char error[CURL_ERROR_SIZE] = {0};
        const std::string agent = download_client_name();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http,file");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
        // DEMO-12: only the plain request; a generic client name (NFR-09).
        curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
        curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 0L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, options_.connect_timeout_s);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, options_.stall_timeout_s);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
        const CURLcode rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        const bool closed = std::fclose(out) == 0;
        if (rc == CURLE_ABORTED_BY_CALLBACK) return "cancelled";
        if (sink.write_failed || !closed) return "cannot write " + file.string();
        if (rc != CURLE_OK) return error[0] != '\0' ? std::string(error) : std::string(curl_easy_strerror(rc));
        return {};
    }

private:
    CurlOptions options_;
};

}  // namespace

std::shared_ptr<Downloader> make_curl_downloader(CurlOptions options) {
    return std::make_shared<CurlDownloader>(options);
}

}  // namespace yantra::app
