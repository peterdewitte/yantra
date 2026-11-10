// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small platform interface for loading a radio plugin's shared library
// (dlopen on Linux; LoadLibraryExW on Windows).
// Copied from yantra 68ea20a: core/include/yantra/core/platform/dynlib.h,
// reduced to what the radio test host needs.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace yantra::radio_host {

/** An opened shared library; closed when destroyed. */
class DynamicLibrary {
public:
    /** Opens `path` with all symbols resolved now and kept local to the
     *  library. nullptr with `error` set on failure. */
    static std::unique_ptr<DynamicLibrary> open(const std::filesystem::path& path,
                                                std::string& error);
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    /** Address of an exported symbol, or nullptr with `error` set. */
    void* symbol(const char* name, std::string& error) const;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    DynamicLibrary(void* handle, std::filesystem::path path);
    void* handle_ = nullptr;
    std::filesystem::path path_;
};

} // namespace yantra::radio_host
