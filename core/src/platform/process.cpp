// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra/core/platform/process.h for Linux and Windows.

#include "yantra/core/platform/process.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <shellapi.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <cerrno>
#include <cstring>
#include <thread>

namespace yantra::platform {

#if defined(_WIN32)

int run_program(const std::vector<std::string>& argv, const std::filesystem::path& output) {
    if (argv.empty()) return -1;
    // Command line quoting as the C runtime parses it back into argv.
    std::wstring line;
    for (const std::string& a : argv) {
        if (!line.empty()) line += L' ';
        line += L'"';
        std::size_t slashes = 0;
        for (const wchar_t ch : std::filesystem::path(a).wstring()) {
            if (ch == L'\\') {
                ++slashes;
                continue;
            }
            line.append(ch == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            slashes = 0;
            line += ch;
        }
        line.append(2 * slashes, L'\\');
        line += L'"';
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!output.empty()) {
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};  // inherited by the program
        out = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) return -1;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = out;
        si.hStdError = out;
    }
    PROCESS_INFORMATION pi{};
    const std::wstring program = std::filesystem::path(argv[0]).wstring();
    const BOOL started = CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, out != INVALID_HANDLE_VALUE,
                                        0, nullptr, nullptr, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!started) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    const BOOL got = GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return got ? static_cast<int>(code) : -1;
}

std::filesystem::path null_device() { return "NUL"; }

bool open_document(const std::filesystem::path& file, std::string& error, const std::string& /*opener*/) {
    const auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r > 32) return true;
    error = r == SE_ERR_NOASSOC ? "no program is set up to open this kind of file"
                                : "the shell could not open the file (code " + std::to_string(r) + ")";
    return false;
}

#else

int run_program(const std::vector<std::string>& argv, const std::filesystem::path& output) {
    if (argv.empty()) return -1;
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (!output.empty()) {
        posix_spawn_file_actions_addopen(&actions, 1, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, 1, 2);
    }
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0].c_str(), &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return -1;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::filesystem::path null_device() { return "/dev/null"; }

bool open_document(const std::filesystem::path& file, std::string& error, const std::string& opener) {
    const std::string path = file.string();
    char* args[] = {const_cast<char*>(opener.c_str()), const_cast<char*>(path.c_str()), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, opener.c_str(), &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        error = opener + ": " + std::strerror(spawned);
        return false;
    }
    // The viewer may run for long: reap the opener in the background
    // rather than wait for it.
    std::thread([pid] {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }).detach();
    return true;
}

#endif

}  // namespace yantra::platform
