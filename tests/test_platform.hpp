// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The few operating-system calls the tests need, for Linux and Windows
// (HW-06): environment variables and running a program without a shell.
// One small interface, so the test files stay free of platform
// conditionals. Copied from yantra-radios 6fac3ee: tests/test_platform.hpp
// (set_env, unset_env, ScopedEnv, run_program).
#pragma once

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace yantra::test {

/// Prefix that makes "/x" an absolute path on this platform: "" on Linux,
/// "C:" on Windows (where "/x" has no drive and is not absolute).
#ifdef _WIN32
inline const std::string kRoot = "C:";
#else
inline const std::string kRoot;
#endif

/// setenv(name, value, 1); 0 on success.
inline int set_env(const char* name, const char* value)
{
#ifdef _WIN32
    return ::_putenv_s(name, value);
#else
    return ::setenv(name, value, 1);
#endif
}

/// unsetenv(name); 0 on success.
inline int unset_env(const char* name)
{
#ifdef _WIN32
    return ::_putenv_s(name, "");  // an empty value removes the variable
#else
    return ::unsetenv(name);
#endif
}

/// Sets a variable for one scope and removes it afterwards.
struct ScopedEnv {
    ScopedEnv(const char* n, const char* value) : name(n) { set_env(n, value); }
    ~ScopedEnv() { unset_env(name.c_str()); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
    std::string name;
};

/// File name suffix of programs ("" on Linux, ".exe" on Windows) and the
/// file that discards what is written to it.
#ifdef _WIN32
inline constexpr const char* kExeSuffix = ".exe";
inline constexpr const char* kNullDevice = "NUL";
#else
inline constexpr const char* kExeSuffix = "";
inline constexpr const char* kNullDevice = "/dev/null";
#endif

/// Runs a program with these arguments (argv[0] is the program's path),
/// without a shell, and waits for it: its exit code, -1 if it could not
/// run. No shell (std::system would need sh or cmd.exe and its quoting).
/// With `output`, the program's stdout and stderr go to that file
/// (created or truncated) instead of the caller's.
inline int run_program(const std::vector<std::string>& argv, const std::filesystem::path& output = {})
{
    if (argv.empty())
        return -1;
#ifdef _WIN32
    // Command line quoting as the C runtime parses it back into argv.
    std::string line;
    for (const std::string& a : argv) {
        if (!line.empty())
            line += ' ';
        line += '"';
        std::size_t slashes = 0;
        for (const char ch : a) {
            if (ch == '\\') {
                ++slashes;
                continue;
            }
            line.append(ch == '"' ? 2 * slashes + 1 : slashes, '\\');
            slashes = 0;
            line += ch;
        }
        line.append(2 * slashes, '\\');
        line += '"';
    }
    STARTUPINFOA si{};
    si.cb = sizeof si;
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!output.empty()) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};  // inherited by the program
        out = ::CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE)
            return -1;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = out;
        si.hStdError = out;
    }
    PROCESS_INFORMATION pi{};
    const BOOL started = ::CreateProcessA(argv[0].c_str(), line.data(), nullptr, nullptr,
                                          out != INVALID_HANDLE_VALUE, 0, nullptr, nullptr, &si, &pi);
    if (out != INVALID_HANDLE_VALUE)
        ::CloseHandle(out);
    if (!started)
        return -1;
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    const BOOL got = ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return got ? static_cast<int>(code) : -1;
#else
    std::vector<char*> args;
    for (const std::string& a : argv)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    if (!output.empty()) {
        ::posix_spawn_file_actions_addopen(&actions, 1, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ::posix_spawn_file_actions_adddup2(&actions, 1, 2);
    }
    pid_t pid = 0;
    const int spawned = ::posix_spawn(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0)
        return -1;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // namespace yantra::test
