// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of the cross SDK's Windows part (RAD-139, RAD-131/132): a
// C++20 program with threads, iostream, std::filesystem and exceptions
// plus a Rust static library, cross-compiled with clang and lld against
// the MSYS2 UCRT64 sysroot and the Windows rust-std. tests/sdk/win_smoke.sh
// runs it on Windows through WSL interop; it prints "smoke ok" last and
// exits 0 when every part works.
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
std::int64_t yantra_sdk_smoke_sum(const std::int32_t* values, std::size_t n);
std::size_t yantra_sdk_smoke_text(std::int64_t sum, char* buf, std::size_t len);
}

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    std::cout << (ok ? "ok   " : "FAIL ") << what << '\n';
    if (!ok) {
        ++failures;
    }
}

// Four threads add their part under a mutex; the main thread waits on a
// condition variable until all are done (winpthreads under libstdc++).
long long threaded_sum() {
    std::mutex m;
    std::condition_variable cv;
    long long total = 0;
    int done = 0;
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t] {
            long long part = 0;
            for (int i = t * 250; i < (t + 1) * 250; ++i) {
                part += i;
            }
            std::lock_guard lock(m);
            total += part;
            ++done;
            cv.notify_one();
        });
    }
    {
        std::unique_lock lock(m);
        cv.wait(lock, [&] { return done == 4; });
    }
    for (auto& w : workers) {
        w.join();
    }
    return total;
}

bool filesystem_round_trip() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("yantra-sdk-smoke-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir / "sub");
    {
        std::ofstream(dir / "sub" / "a.txt") << "yantra";
    }
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) {
            ++files;
            bytes += e.file_size();
        }
    }
    const auto removed = fs::remove_all(dir);
    return files == 1 && bytes == 6 && removed == 3 && !fs::exists(dir);
}

bool exception_round_trip() {
    try {
        throw std::runtime_error("expected");
    } catch (const std::exception& e) {
        return std::string(e.what()) == "expected";
    }
}

}  // namespace

int main() {
    expect(threaded_sum() == 499500, "std::thread, std::mutex, std::condition_variable");
    expect(filesystem_round_trip(), "std::filesystem");
    expect(exception_round_trip(), "C++ exceptions");
    std::vector<std::int32_t> v(100);
    std::iota(v.begin(), v.end(), 1);
    const std::int64_t sum = yantra_sdk_smoke_sum(v.data(), v.size());
    char text[64] = {};
    const std::size_t n = yantra_sdk_smoke_text(sum, text, sizeof text);
    expect(sum == 5050 && std::string(text, n) == "rust says 5050", std::string("Rust static library: ") + text);
    if (failures != 0) {
        std::cout << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "smoke ok\n";
    return 0;
}
