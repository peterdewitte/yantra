// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-pei-sim: serves a simulated TETRA terminal (pei_sim.hpp) where the
// terminal plugin's serial-port code opens it (RAD-80): a pseudo-terminal
// behind the symlink --link (POSIX) or the named pipe --link
// \\.\pipe\NAME (Windows), for the plugin's tests.
//
//   yantra-pei-sim --link PORT --scenario FILE [--control FILE] [--error CMD]...
//                  [--silent CMD]... [--noise] [--delay-ms N] [-- COMMAND ARGS...]
//
// With a command it runs it with YANTRA_PEI_PORTS=PORT and returns its
// exit code; without, it serves until SIGINT/SIGTERM. While it runs, the
// control file (default PORT.unplugged; for a pipe <temp>/NAME.unplugged)
// unplugs the terminal (link removed or pipe closed) and removing the file
// plugs it in again (RAD-35 tests).
#include "pei_sim.hpp"

#include <yantra/gen/portable_math.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int)
{
    g_stop = 1;
}

int usage(const char* why)
{
    std::cerr << "yantra-pei-sim: " << why << "\n"
              << "usage: yantra-pei-sim --link PORT --scenario FILE [--control FILE] [--error CMD]...\n"
                 "                      [--silent CMD]... [--noise] [--delay-ms N] [-- COMMAND ARGS...]\n";
    return 2;
}

bool exists(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void remove_file(const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

#if defined(_WIN32)
std::wstring wide(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(w.size() - 1);
    return w;
}

// One argument quoted for CommandLineToArgvW / the CRT's parser.
std::wstring quote(const std::wstring& a)
{
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < a.size() && a[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == a.size()) {
            q.append(backslashes * 2, L'\\');
            break;
        }
        if (a[i] == L'"')
            q.append(backslashes * 2 + 1, L'\\');
        else
            q.append(backslashes, L'\\');
        q.push_back(a[i]);
    }
    q.push_back(L'"');
    return q;
}

// The child, polled for its end.
struct Child {
    PROCESS_INFORMATION pi{};
    bool start(const std::vector<std::string>& command)
    {
        std::wstring line;
        for (const auto& c : command) {
            if (!line.empty())
                line.push_back(L' ');
            line += quote(wide(c));
        }
        STARTUPINFOW si{};
        si.cb = sizeof si;
        return CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi) != 0;
    }
    bool done(int& rc)
    {
        if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0)
            return false;
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = static_cast<int>(code);
        return true;
    }
    void kill()
    {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    ~Child()
    {
        if (pi.hProcess != nullptr) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
};
#else
struct Child {
    pid_t pid = -1;
    bool start(std::vector<std::string> command)
    {
        pid = ::fork();
        if (pid == 0) {
            std::vector<char*> args;
            for (auto& c : command)
                args.push_back(c.data());
            args.push_back(nullptr);
            ::execvp(args[0], args.data());
            std::perror("yantra-pei-sim: exec");
            ::_exit(127);
        }
        return pid > 0;
    }
    bool done(int& rc)
    {
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) != pid)
            return false;
        rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        pid = -1;
        return true;
    }
    void kill()
    {
        ::kill(pid, SIGTERM);
        ::waitpid(pid, nullptr, 0);
        pid = -1;
    }
};
#endif

int set_ports_env(const std::string& value)
{
#if defined(_WIN32)
    return _putenv_s("YANTRA_PEI_PORTS", value.c_str());
#else
    return ::setenv("YANTRA_PEI_PORTS", value.c_str(), 1);
#endif
}

std::string default_control(const std::string& link)
{
    const std::string pipe_prefix = "\\\\.\\pipe\\";
    if (link.compare(0, pipe_prefix.size(), pipe_prefix) == 0)
        return (std::filesystem::temp_directory_path() / (link.substr(pipe_prefix.size()) + ".unplugged")).string();
    return link + ".unplugged";
}

} // namespace

int main(int argc, char** argv)
{
    yantra::gen::use_portable_math(); // the same levels as the tests' own terminal
    std::string link, scenario, control;
    std::vector<std::string> errors, silents, command;
    bool noise = false;
    int delay_ms = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        const char* v = nullptr;
        if (a == "--") {
            for (++i; i < argc; ++i)
                command.emplace_back(argv[i]);
        } else if (a == "--link" && (v = next()) != nullptr) {
            link = v;
        } else if (a == "--scenario" && (v = next()) != nullptr) {
            scenario = v;
        } else if (a == "--control" && (v = next()) != nullptr) {
            control = v;
        } else if (a == "--error" && (v = next()) != nullptr) {
            errors.emplace_back(v);
        } else if (a == "--silent" && (v = next()) != nullptr) {
            silents.emplace_back(v);
        } else if (a == "--delay-ms" && (v = next()) != nullptr) {
            delay_ms = std::atoi(v);
        } else if (a == "--noise") {
            noise = true;
        } else {
            return usage(("unknown or incomplete option " + a).c_str());
        }
    }
    if (link.empty() || scenario.empty())
        return usage("--link and --scenario are needed");
    if (control.empty())
        control = default_control(link);
    try {
        yantra::pei_sim::Terminal terminal(yantra::gen::load_scenario_file(scenario));
        for (const auto& e : errors)
            terminal.set_error(e, true);
        for (const auto& s : silents)
            terminal.set_silent(s, true);
        terminal.set_noise(noise);
        remove_file(control);
        yantra::pei_sim::PortTerminal port(terminal, link);
        port.set_reply_delay_ms(delay_ms);
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        Child child;
        const bool with_child = !command.empty();
        if (with_child) {
            set_ports_env(link);
            if (!child.start(command)) {
                std::cerr << "yantra-pei-sim: cannot start " << command[0] << "\n";
                return 2;
            }
        } else {
            std::cout << "yantra-pei-sim: terminal on " << link << " (" << port.device() << "), control file "
                      << control << std::endl;
        }
        int rc = 0;
        while (!g_stop) {
            if (with_child && child.done(rc))
                break;
            const bool want_unplugged = exists(control);
            if (want_unplugged && port.plugged())
                port.unplug();
            else if (!want_unplugged && !port.plugged())
                port.replug();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (g_stop && with_child) {
            child.kill();
            rc = 1;
        }
        port.unplug();
        remove_file(control);
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "yantra-pei-sim: " << e.what() << "\n";
        return 2;
    }
}
