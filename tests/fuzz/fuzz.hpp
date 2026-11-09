// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared helpers of the fuzz targets (TST-12). Every target defines
// LLVMFuzzerTestOneInput: with clang's libFuzzer in a fuzz build
// (YANTRA_FUZZ=ON), or with replay_main.cpp in every other build, where
// ctest replays the seed corpus plus seeded mutations of it (label fuzz).
// A target fails by crashing: a sanitizer report, an uncaught exception
// (only each parser's documented error is caught) or fail() when a
// property of the output does not hold.
// Copied from yantra-radios 5394318: tests/fuzz/fuzz.hpp, adapted for
// TST-12 (text helpers only).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

namespace yantra::fuzz {

/// Stops the run on a broken property (libFuzzer saves the input).
[[noreturn]] inline void fail(const char* what) {
    std::fprintf(stderr, "fuzz property failed: %s\n", what);
    std::fflush(stderr);
    std::abort();
}

inline std::string text_of(const uint8_t* d, size_t n) { return std::string(reinterpret_cast<const char*>(d), n); }

/// A scratch folder of this process under the working directory (the
/// build tree), removed when it goes out of scope.
class ScratchDir {
public:
    explicit ScratchDir(const std::string& what)
        : path_(std::filesystem::current_path() / ("fuzz-" + what + "-" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(path_);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace yantra::fuzz
