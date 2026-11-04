// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small platform interface for shared libraries and the process
// environment, so the plugin loader (PLG-09) and the radio host (PLG-25)
// stay portable. Linux: dlopen (core/src/platform/dynlib.cpp); the
// Windows branch (LoadLibraryW) is written there but DEFERRED (Windows).
// Copied from yantra-v2 68ea20a: core/include/yantra/core/platform/
// dynlib.h, adapted (RTLD_NODELETE as yantra-radios' test host).
#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace yantra::platform {

// An opened shared library; closed when destroyed.
class DynamicLibrary {
public:
    // Opens `path` with all symbols resolved now and kept local to the
    // library (RTLD_NOW | RTLD_LOCAL), and kept mapped after close
    // (RTLD_NODELETE: plugins link libstdc++ statically, RAD-132; unmapping
    // one would hide its frames in sanitizer reports and leak its pools).
    // nullptr with `error` set on failure.
    static std::unique_ptr<DynamicLibrary> open(const std::filesystem::path& path,
                                                std::string& error);
    ~DynamicLibrary();
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    // Address of an exported symbol, or nullptr with `error` set.
    void* symbol(const char* name, std::string& error) const;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    DynamicLibrary(void* handle, std::filesystem::path path);
    void* handle_ = nullptr;
    std::filesystem::path path_;
};

// File name suffix of plugins on this platform (".so"; ".dll" on Windows).
const char* plugin_file_suffix() noexcept;

// Folder of the running executable, empty if unknown.
std::filesystem::path executable_dir();

// Value of an environment variable, "" when unset.
std::string environment(const char* name);

// Separator of path lists in environment variables (':'; ';' on Windows).
char path_list_separator() noexcept;

}  // namespace yantra::platform
