// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows implementation of yantra/core/platform/file.h (HW-06; the
// Linux one is file_posix.cpp): FlushFileBuffers for durable writes,
// MoveFileExW (write-through) for the atomic renames. NTFS journals its
// folder entries, so a folder needs no flush of its own.

#include "yantra/core/platform/file.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

namespace yantra::platform {

namespace {

std::string last_error_text(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::string out;
    if (n > 0 && buf != nullptr) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        out.resize(static_cast<std::size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), len, nullptr, nullptr);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '.'))
            out.pop_back();
    }
    if (buf != nullptr) LocalFree(buf);
    if (out.empty()) out = "error " + std::to_string(code);
    return out;
}

std::string name_of(const std::filesystem::path& p) { return p.generic_string(); }

}  // namespace

bool sync_file(const std::filesystem::path& file, std::string& error) {
    // FlushFileBuffers needs a handle with write access.
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = name_of(file) + ": " + last_error_text(GetLastError());
        return false;
    }
    const bool ok = FlushFileBuffers(h) != 0;
    if (!ok) error = name_of(file) + ": " + last_error_text(GetLastError());
    CloseHandle(h);
    return ok;
}

bool sync_folder(const std::filesystem::path& folder, std::string& error) {
    std::error_code ec;
    const std::filesystem::path p = folder.empty() ? std::filesystem::path(".") : folder;
    if (!std::filesystem::is_directory(p, ec)) {
        error = name_of(p) + ": not a folder";
        return false;
    }
    return true;
}

bool rename_no_replace(const std::filesystem::path& from, const std::filesystem::path& to, bool& exists,
                       std::string& error) {
    exists = false;
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0) return true;
    const DWORD e = GetLastError();
    if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS) {
        exists = true;
        error = name_of(to) + ": file exists";
        return false;
    }
    error = name_of(to) + ": " + last_error_text(e);
    return false;
}

bool rename_replace(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) return true;
    error = name_of(to) + ": " + last_error_text(GetLastError());
    return false;
}

AppendFile AppendFile::create_new(const std::filesystem::path& path, std::string& error) {
    AppendFile f;
    f.fd_ = ::_wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_APPEND | _O_BINARY | _O_NOINHERIT,
                     _S_IREAD | _S_IWRITE);
    if (f.fd_ < 0) error = name_of(path) + ": " + std::strerror(errno);
    return f;
}

AppendFile::~AppendFile() { close(); }

AppendFile::AppendFile(AppendFile&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

AppendFile& AppendFile::operator=(AppendFile&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool AppendFile::write(const void* data, std::size_t size, std::string& error) {
    const char* p = static_cast<const char*>(data);
    while (size > 0) {
        const unsigned chunk = size > (1u << 30) ? (1u << 30) : static_cast<unsigned>(size);
        const int n = ::_write(fd_, p, chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            error = std::strerror(errno);
            return false;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool AppendFile::sync(std::string& error) {
    if (::_commit(fd_) != 0) {
        error = std::strerror(errno);
        return false;
    }
    return true;
}

void AppendFile::close() noexcept {
    if (fd_ >= 0) {
        ::_close(fd_);
        fd_ = -1;
    }
}

}  // namespace yantra::platform
