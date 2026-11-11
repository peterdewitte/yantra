// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Running another program without a shell (HW-06): std::system needs sh
// on Linux and cmd.exe on Windows, each with its own quoting (cmd.exe
// knows no single quotes). Linux: posix_spawn; Windows: CreateProcessW.
// Adapted from tests/test_platform.hpp (run_program, copied from
// yantra-radios 6fac3ee).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yantra::platform {

// Runs argv[0] (a program's path) with these arguments and waits for it:
// its exit code, -1 when it could not run (or ended on a signal). With
// `output`, its stdout and stderr go to that file (created or truncated;
// null_device() discards them); otherwise to the caller's.
int run_program(const std::vector<std::string>& argv, const std::filesystem::path& output = {});

// The file that discards what is written to it ("/dev/null"; "NUL" on
// Windows).
std::filesystem::path null_device();

}  // namespace yantra::platform
