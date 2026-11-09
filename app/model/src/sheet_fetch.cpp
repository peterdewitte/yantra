// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tour's sheet download and its check (DEMO-12), see sheet_fetch.h.
#include "yantra/app/sheet_fetch.h"
#include "yantra/core/platform/thread_name.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include "yantra/app/i18n.h"
#include "yantra/core/build_info.h"
#include "yantra/core/util/sha256.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

class NoDownloader final : public Downloader {
public:
    explicit NoDownloader(std::string reason) : reason_(std::move(reason)) {}
    std::string fetch(const std::string&, const fs::path&, Progress&) override { return reason_; }

private:
    std::string reason_;
};

std::string file_hash(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    util::Sha256 h;
    char buf[1 << 15];
    while (in) {
        in.read(buf, sizeof buf);
        if (in.gcount() > 0) h.update(buf, static_cast<std::size_t>(in.gcount()));
    }
    return h.finish_hex();
}

void remove_quietly(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

}  // namespace

std::string download_client_name() {
    // Only the program and its version: no user, host or system details.
    return "Yantra/" + std::string(core::build_info().version_string);
}

std::shared_ptr<Downloader> make_no_downloader(std::string reason) {
    return std::make_shared<NoDownloader>(std::move(reason));
}

std::string wrong_sheet_message(const demo::DemoSheet& sheet, const fs::path& file, const std::string& reason) {
    const std::string got = file_hash(file);
    return trf("The file is not the expected plan sheet ({}). Expected SHA-256 {}, the file has {}. Source: {}",
               {reason, sheet.sha256, got.empty() ? std::string(tr("unreadable")) : got, sheet.source_url});
}

SheetStatus cached_sheet(const demo::DemoSheet& sheet, const fs::path& cache) {
    SheetStatus s;
    if (cache.empty()) return s;
    try {
        if (const auto f = demo::check_sheet_file(sheet, cache)) {
            s.state = SheetStatus::State::Ready;
            s.file = *f;
        }
    } catch (const std::exception& e) {
        s.state = SheetStatus::State::Wrong;
        s.message = wrong_sheet_message(sheet, cache / sheet.file, e.what());
    }
    return s;
}

SheetFetch::SheetFetch(std::shared_ptr<Downloader> downloader, demo::DemoSheet sheet, fs::path cache)
    : downloader_(std::move(downloader)), sheet_(std::move(sheet)), cache_(std::move(cache)) {
    worker_ = std::thread([this] {
        platform::set_thread_name("yt-sheet-fetch");
        run();
    });
}

SheetFetch::~SheetFetch() {
    cancel();
    wait();
}

void SheetFetch::wait() {
    if (worker_.joinable()) worker_.join();
}

double SheetFetch::fraction() const noexcept {
    const std::uint64_t total = progress_.total.load();
    if (total == 0) return -1.0;
    return std::min(1.0, static_cast<double>(progress_.got.load()) / static_cast<double>(total));
}

Result SheetFetch::result() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_;
}

fs::path SheetFetch::file() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_;
}

void SheetFetch::run() {
    Result r;
    fs::path out;
    const fs::path partial_dir = cache_ / ".download";
    const fs::path partial = partial_dir / sheet_.file;
    try {
        std::error_code ec;
        fs::create_directories(partial_dir, ec);
        if (ec) throw std::runtime_error(trf("Cannot make the cache folder {}: {}", {partial_dir.string(), ec.message()}));
        remove_quietly(partial);
        if (!downloader_) throw std::runtime_error(tr("Downloads are not set up in this copy of Yantra."));
        progress_.total = sheet_.size_bytes;
        const std::string error = downloader_->fetch(sheet_.source_url, partial, progress_);
        if (!error.empty())
            throw std::runtime_error(trf("The download from {} failed: {}", {sheet_.source_url, error}));
        // DEMO-12: the hash, size and page size of the unaltered file.
        try {
            if (!demo::check_sheet_file(sheet_, partial_dir))
                throw std::runtime_error(tr("The download left no file."));
        } catch (const demo::DemoError& e) {
            const std::string message = wrong_sheet_message(sheet_, partial, e.what());
            remove_quietly(partial);
            throw std::runtime_error(message);
        }
        out = cache_ / sheet_.file;
        fs::rename(partial, out, ec);
        if (ec) throw std::runtime_error(trf("Cannot keep the sheet in {}: {}", {out.string(), ec.message()}));
        r.message = trf("Downloaded and checked {} ({} bytes).", {sheet_.file, std::to_string(progress_.got.load())});
    } catch (const std::exception& e) {
        remove_quietly(partial);
        out.clear();
        r.ok = false;
        r.message = e.what();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = std::move(r);
        file_ = std::move(out);
    }
    done_ = true;
}

}  // namespace yantra::app
