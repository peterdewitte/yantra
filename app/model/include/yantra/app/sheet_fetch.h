// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one-time download of a licence-restricted example sheet for the
// guided tour (DEMO-08, DEMO-12; docs/DEMO.md, docs/APP.md "Guided tour"):
//
// - Downloader: a plain GET of the sheet's URL whose body is written to a
//   file unchanged. Nothing is sent but the request with a generic client
//   name (no personal data, no cookies, no referrer). The app's
//   implementation is libcurl (app/net/curl_downloader.h, spec 16.2); the
//   model only sees this interface, so tests give their own.
// - cached_sheet: the sheet in the user's cache, checked against its
//   description (demo::check_sheet_file: SHA-256, size, page size).
// - SheetFetch: the download on a worker thread into
//   <cache>/.download/<file>, then the check; only a file that passes is
//   moved into <cache>/<file>. Anything else is deleted and the message
//   names both SHA-256 values and the URL. The file is never altered.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "yantra/app/result.h"
#include "yantra/core/demo/demo_project.h"

namespace yantra::app {

/// The client name of every download (no personal data, NFR-09).
std::string download_client_name();

class Downloader {
public:
    /// Shared with the worker: bytes so far, the size when known (0
    /// unknown) and the cancel request.
    struct Progress {
        std::atomic<std::uint64_t> got{0};
        std::atomic<std::uint64_t> total{0};
        std::atomic<bool> cancel{false};
    };
    virtual ~Downloader() = default;
    /// Downloads `url` into `file` (created or replaced); blocks. "" on
    /// success, else the reason in one line. Stops early when
    /// progress.cancel is set (then the reason says so).
    virtual std::string fetch(const std::string& url, const std::filesystem::path& file, Progress& progress) = 0;
    /// False for make_no_downloader(): Yantra is offline (IND-47 then
    /// shows cached map tiles only and offers no search).
    virtual bool available() const { return true; }
};

/// A downloader that always fails with `reason` (a copy of Yantra built
/// without downloads; the tests' "offline").
std::shared_ptr<Downloader> make_no_downloader(std::string reason);

/// A licence-restricted sheet in the cache folder.
struct SheetStatus {
    enum class State { Missing, Ready, Wrong };
    State state = State::Missing;
    std::filesystem::path file;  ///< Ready: the checked file
    std::string message;         ///< Wrong: why (both hashes and the URL)
};
SheetStatus cached_sheet(const demo::DemoSheet& sheet, const std::filesystem::path& cache);

/// The message for a file that is not the described sheet: the check's
/// reason, both SHA-256 values and where the sheet comes from.
std::string wrong_sheet_message(const demo::DemoSheet& sheet, const std::filesystem::path& file,
                                const std::string& reason);

class SheetFetch {
public:
    /// Starts the download of `sheet` into `cache` at once.
    SheetFetch(std::shared_ptr<Downloader> downloader, demo::DemoSheet sheet, std::filesystem::path cache);
    /// Cancels a running download and waits for the worker.
    ~SheetFetch();
    SheetFetch(const SheetFetch&) = delete;
    SheetFetch& operator=(const SheetFetch&) = delete;

    bool done() const noexcept { return done_.load(); }
    /// 0..1 when the size is known; < 0 unknown.
    double fraction() const noexcept;
    std::uint64_t bytes() const noexcept { return progress_.got.load(); }
    void cancel() noexcept { progress_.cancel = true; }
    /// Waits for the worker (tests).
    void wait();
    /// After done(): ok with the cached file, or why not.
    Result result() const;
    std::filesystem::path file() const;
    const demo::DemoSheet& sheet() const noexcept { return sheet_; }

private:
    void run();

    std::shared_ptr<Downloader> downloader_;
    demo::DemoSheet sheet_;
    std::filesystem::path cache_;
    Downloader::Progress progress_;
    std::atomic<bool> done_{false};
    mutable std::mutex mutex_;
    Result result_;
    std::filesystem::path file_;
    std::thread worker_;
};

}  // namespace yantra::app
