// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 8c7a3e1: core/src/platform/file_posix.cpp,
// adapted (rename_replace).

#include "yantra/core/platform/file.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace yantra::platform {

namespace {

bool sync_path(const std::filesystem::path& p, int flags, std::string& error) {
    const int fd = ::open(p.c_str(), flags | O_CLOEXEC);
    if (fd < 0) {
        error = p.string() + ": " + std::strerror(errno);
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    if (!ok) {
        error = p.string() + ": " + std::strerror(errno);
    }
    ::close(fd);
    return ok;
}

}  // namespace

bool sync_file(const std::filesystem::path& file, std::string& error) {
    return sync_path(file, O_RDONLY, error);
}

bool sync_folder(const std::filesystem::path& folder, std::string& error) {
    return sync_path(folder.empty() ? std::filesystem::path(".") : folder, O_RDONLY | O_DIRECTORY, error);
}

bool rename_no_replace(const std::filesystem::path& from, const std::filesystem::path& to, bool& exists,
                       std::string& error) {
    exists = false;
    // A hard link fails when the target exists (atomic); then the
    // temporary name goes.
    if (::link(from.c_str(), to.c_str()) == 0) {
        ::unlink(from.c_str());
        return true;
    }
    const int e = errno;
    if (e == EEXIST) {
        exists = true;
        error = to.string() + ": file exists";
        return false;
    }
    // File systems without hard links: check, then rename.
    if (e == EPERM || e == EOPNOTSUPP || e == EXDEV || e == EMLINK) {
        std::error_code ec;
        if (std::filesystem::exists(to, ec)) {
            exists = true;
            error = to.string() + ": file exists";
            return false;
        }
        if (::rename(from.c_str(), to.c_str()) == 0) {
            return true;
        }
    }
    error = to.string() + ": " + std::strerror(errno);
    return false;
}

bool rename_replace(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
    if (::rename(from.c_str(), to.c_str()) == 0) {
        return true;
    }
    error = to.string() + ": " + std::strerror(errno);
    return false;
}

AppendFile AppendFile::create_new(const std::filesystem::path& path, std::string& error) {
    AppendFile f;
    f.fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
    if (f.fd_ < 0) error = path.string() + ": " + std::strerror(errno);
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
        const ssize_t n = ::write(fd_, p, size);
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
    if (::fdatasync(fd_) != 0) {
        error = std::strerror(errno);
        return false;
    }
    return true;
}

void AppendFile::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace yantra::platform
