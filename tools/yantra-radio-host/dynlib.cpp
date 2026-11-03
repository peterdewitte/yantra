// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: core/src/platform/dynlib_posix.cpp, with the
// Windows branch added (not built yet: Windows is DEFERRED) and RTLD_NODELETE.
#include "dynlib.hpp"

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace yantra::radio_host {

#if defined(_WIN32)

std::unique_ptr<DynamicLibrary> DynamicLibrary::open(const std::filesystem::path& path,
                                                     std::string& error)
{
    HMODULE h = LoadLibraryW(path.c_str());
    if (h == nullptr) {
        error = "LoadLibrary failed with error " + std::to_string(GetLastError());
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(reinterpret_cast<void*>(h), path));
}

DynamicLibrary::~DynamicLibrary()
{
    if (handle_ != nullptr)
        FreeLibrary(reinterpret_cast<HMODULE>(handle_));
}

void* DynamicLibrary::symbol(const char* name, std::string& error) const
{
    FARPROC p = GetProcAddress(reinterpret_cast<HMODULE>(handle_), name);
    if (p == nullptr)
        error = std::string("symbol ") + name + " not found";
    return reinterpret_cast<void*>(p);
}

#else

namespace {
std::string last_dl_error(const char* fallback)
{
    const char* e = dlerror();
    return e != nullptr ? std::string{e} : std::string{fallback};
}
} // namespace

std::unique_ptr<DynamicLibrary> DynamicLibrary::open(const std::filesystem::path& path,
                                                     std::string& error)
{
    dlerror(); // clear a stale message
    // RTLD_NODELETE: the plugin stays mapped after dlclose. A plugin links
    // libstdc++ statically (RAD-132); its exception emergency pool is only
    // released at process exit, and unmapping the module would turn it into
    // a leak report under LeakSanitizer and hide plugin frames in reports.
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (handle == nullptr) {
        error = last_dl_error("dlopen failed");
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle, path));
}

DynamicLibrary::~DynamicLibrary()
{
    if (handle_ != nullptr)
        dlclose(handle_);
}

void* DynamicLibrary::symbol(const char* name, std::string& error) const
{
    dlerror();
    void* p = dlsym(handle_, name);
    if (p == nullptr)
        error = last_dl_error("symbol not found");
    return p;
}

#endif

DynamicLibrary::DynamicLibrary(void* handle, std::filesystem::path path)
    : handle_(handle), path_(std::move(path))
{
}

} // namespace yantra::radio_host
