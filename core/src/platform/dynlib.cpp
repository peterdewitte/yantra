// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra/core/platform/dynlib.h: POSIX (Linux) implementation; the
// Windows branch is written but not built yet (DEFERRED (Windows)).
// Copied from yantra-v2 68ea20a: core/src/platform/dynlib_posix.cpp and
// yantra-radios fb194f5: tools/yantra-radio-host/dynlib.cpp (Windows
// branch, RTLD_NODELETE).

#include "yantra/core/platform/dynlib.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <system_error>

namespace yantra::platform {

#if defined(_WIN32)

std::unique_ptr<DynamicLibrary> DynamicLibrary::open(const std::filesystem::path& path,
                                                     std::string& error) {
    // The DLLs a plugin imports are searched in its own folder, then in the
    // program's folder (the shipped runtime DLLs) and System32 (HW-06).
    // These flags need an absolute path.
    const std::filesystem::path full = std::filesystem::absolute(path);
    const UINT old_mode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    HMODULE h = LoadLibraryExW(full.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const DWORD code = GetLastError();
    SetErrorMode(old_mode);
    if (h == nullptr) {
        error = "LoadLibrary failed with error " + std::to_string(code);
        if (code == ERROR_MOD_NOT_FOUND) error += " (the file or a DLL it needs was not found)";
        else if (code == ERROR_BAD_EXE_FORMAT) error += " (not a 64-bit Windows DLL)";
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(reinterpret_cast<void*>(h), path));
}

DynamicLibrary::~DynamicLibrary() {
    if (handle_ != nullptr) FreeLibrary(reinterpret_cast<HMODULE>(handle_));
}

void* DynamicLibrary::symbol(const char* name, std::string& error) const {
    FARPROC p = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
    if (p == nullptr) error = std::string("symbol ") + name + " not found";
    return reinterpret_cast<void*>(p);
}

const char* plugin_file_suffix() noexcept { return ".dll"; }

std::filesystem::path executable_dir() {
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, 32768);
    if (n == 0 || n >= 32768) return {};
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

char path_list_separator() noexcept { return ';'; }

#else

namespace {

std::string last_dl_error(const char* fallback) {
    const char* e = dlerror();
    return e != nullptr ? std::string{e} : std::string{fallback};
}

}  // namespace

std::unique_ptr<DynamicLibrary> DynamicLibrary::open(const std::filesystem::path& path,
                                                     std::string& error) {
    dlerror();  // clear a stale message
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (handle == nullptr) {
        error = last_dl_error("dlopen failed");
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle, path));
}

DynamicLibrary::~DynamicLibrary() {
    if (handle_ != nullptr) dlclose(handle_);
}

void* DynamicLibrary::symbol(const char* name, std::string& error) const {
    dlerror();
    void* p = dlsym(handle_, name);
    if (p == nullptr) error = last_dl_error("symbol not found");
    return p;
}

const char* plugin_file_suffix() noexcept { return ".so"; }

std::filesystem::path executable_dir() {
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
}

char path_list_separator() noexcept { return ':'; }

#endif

DynamicLibrary::DynamicLibrary(void* handle, std::filesystem::path path)
    : handle_(handle), path_(std::move(path)) {}

std::string environment(const char* name) {
    const char* v = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read-only use
    return v != nullptr ? std::string{v} : std::string{};
}

}  // namespace yantra::platform
