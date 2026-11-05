// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Durable file operations for atomic saves (project file, IND-01): flush a
// file or a folder entry to disk, and rename without replacing an
// existing file. Linux implementation: file_posix.cpp. Windows is
// DEFERRED. Copied from yantra-v2 8c7a3e1: core/include/yantra/core/platform/file.h,
// adapted (rename_replace for save_as over an existing file).
#pragma once

#include <filesystem>
#include <string>

namespace yantra::platform {

// Flushes a file's content to disk (fsync). False with `error` on failure.
bool sync_file(const std::filesystem::path& file, std::string& error);
// Flushes a folder's entries (after a rename into it).
bool sync_folder(const std::filesystem::path& folder, std::string& error);
// Renames `from` to `to` atomically, failing when `to` exists (no
// replace). `exists` is set when that was the reason.
bool rename_no_replace(const std::filesystem::path& from, const std::filesystem::path& to,
                       bool& exists, std::string& error);
// Renames `from` to `to` atomically, replacing an existing `to`.
bool rename_replace(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error);

}  // namespace yantra::platform
