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

}  // namespace yantra::platform
