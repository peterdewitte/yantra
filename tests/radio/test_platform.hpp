// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The few operating-system calls the tests need, for Linux and Windows
// (RAD-131): environment variables, running a program, shell commands for
// the tools' --*-cmd options and a loopback UDP receiver (the GSMTAP
// tests, RAD-115). One small interface, so the test files stay
// free of platform conditionals.
#pragma once

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <spawn.h>
#  include <sys/socket.h>
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

/// Runs a program with these arguments (argv[0] is the program's path),
/// without a shell, and waits for it: its exit code, -1 if it could not
/// run. Unlike std::system this works on Windows when the current folder
/// is a network share (cmd.exe refuses one; the Windows tests run in place
/// on \\wsl.localhost, tools/sdk/wsl_run.sh --in-place).
inline int run_program(const std::vector<std::string>& argv)
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
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessA(argv[0].c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                          &pi))
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
    pid_t pid = 0;
    if (::posix_spawn(&pid, argv[0].c_str(), nullptr, nullptr, args.data(), environ) != 0)
        return -1;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/// Shell commands (std::system: sh on Linux, cmd.exe on Windows) for the
/// tools' --step-cmd, --unplug-cmd and --replug-cmd options. The path is
/// made absolute and native on Windows: cmd.exe runs in its own folder
/// when the current one is a network share (\\wsl.localhost), so a rooted
/// path (/home/...) would land on drive C:.
inline std::string shell_path(const std::string& path)
{
#ifdef _WIN32
    return "\"" + std::filesystem::absolute(path).make_preferred().string() + "\"";
#else
    return "'" + path + "'";
#endif
}

/// Writes text (may hold a {placeholder} the tool replaces) into a file.
inline std::string write_command(const std::string& text, const std::string& path)
{
#ifdef _WIN32
    return "> " + shell_path(path) + " echo " + text;
#else
    return "printf %s " + text + " > " + shell_path(path);
#endif
}

/// Creates an empty file.
inline std::string touch_command(const std::string& path)
{
#ifdef _WIN32
    return "type nul > " + shell_path(path);
#else
    return "touch " + shell_path(path);
#endif
}

/// Removes a file.
inline std::string remove_command(const std::string& path)
{
#ifdef _WIN32
    return "del /q " + shell_path(path);
#else
    return "rm " + shell_path(path);
#endif
}

/// A UDP socket bound to an ephemeral port on 127.0.0.1; closed by the
/// destructor. receive() never blocks.
class LoopbackUdpReceiver {
public:
    explicit LoopbackUdpReceiver(int receive_buffer = 0)
    {
#ifdef _WIN32
        WSADATA wsa;
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            throw std::runtime_error("WSAStartup failed");
        started_ = true;
#endif
#ifdef _WIN32
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
#else
        fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
#endif
        if (!valid())
            throw std::runtime_error("no UDP socket");
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0)
            throw std::runtime_error("bind to 127.0.0.1 failed");
        socklen_t alen = sizeof a;
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &alen) != 0)
            throw std::runtime_error("getsockname failed");
        port_ = ntohs(a.sin_port);
        if (receive_buffer > 0)
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receive_buffer),
                         sizeof receive_buffer);
#ifdef _WIN32
        u_long nonblocking = 1;
        ::ioctlsocket(fd_, FIONBIO, &nonblocking);
#endif
    }
    ~LoopbackUdpReceiver()
    {
        if (valid()) {
#ifdef _WIN32
            ::closesocket(fd_);
#else
            ::close(fd_);
#endif
        }
#ifdef _WIN32
        if (started_)
            ::WSACleanup();
#endif
    }
    LoopbackUdpReceiver(const LoopbackUdpReceiver&) = delete;
    LoopbackUdpReceiver& operator=(const LoopbackUdpReceiver&) = delete;

    uint16_t port() const { return port_; }

    /// The next queued datagram (at most max_bytes), or nothing.
    std::optional<std::vector<uint8_t>> receive(std::size_t max_bytes = 2048)
    {
        std::vector<uint8_t> buf(max_bytes);
#ifdef _WIN32
        const int n = ::recv(fd_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
#else
        const ssize_t n = ::recv(fd_, buf.data(), buf.size(), MSG_DONTWAIT);
#endif
        if (n < 0)
            return std::nullopt;
        buf.resize(static_cast<std::size_t>(n));
        return buf;
    }

private:
#ifdef _WIN32
    bool valid() const { return fd_ != INVALID_SOCKET; }
    SOCKET fd_ = INVALID_SOCKET;
    bool started_ = false;
#else
    bool valid() const { return fd_ >= 0; }
    int fd_ = -1;
#endif
    uint16_t port_ = 0;
};

}  // namespace yantra::test
