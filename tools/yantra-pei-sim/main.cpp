// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-pei-sim: serves a simulated TETRA terminal (pei_sim.hpp) on a
// pseudo-terminal behind --link, for the terminal plugin's tests (RAD-80).
//
//   yantra-pei-sim --link PATH --scenario FILE [--error CMD]... [--silent CMD]...
//                  [--noise] [--delay-ms N] [-- COMMAND ARGS...]
//
// With a command it runs it with YANTRA_PEI_PORTS=PATH and returns its
// exit code; without, it serves until SIGINT/SIGTERM. While it runs, the
// file PATH.unplugged unplugs the terminal (link removed, pseudo-terminal
// closed) and removing the file plugs it in again (RAD-35 tests).
#include "pei_sim.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_stop = 0;

void on_signal(int)
{
    g_stop = 1;
}

int usage(const char* why)
{
    std::cerr << "yantra-pei-sim: " << why << "\n"
              << "usage: yantra-pei-sim --link PATH --scenario FILE [--error CMD]... [--silent CMD]... [--noise]\n"
                 "                      [--delay-ms N] [-- COMMAND ARGS...]\n";
    return 2;
}

bool exists(const std::string& path)
{
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string link, scenario;
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
    try {
        yantra::pei_sim::Terminal terminal(yantra::gen::load_scenario_file(scenario));
        for (const auto& e : errors)
            terminal.set_error(e, true);
        for (const auto& s : silents)
            terminal.set_silent(s, true);
        terminal.set_noise(noise);
        const std::string control = link + ".unplugged";
        ::unlink(control.c_str());
        yantra::pei_sim::PtyTerminal pty(terminal, link);
        pty.set_reply_delay_ms(delay_ms);
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        pid_t child = -1;
        if (!command.empty()) {
            ::setenv("YANTRA_PEI_PORTS", link.c_str(), 1);
            child = ::fork();
            if (child == 0) {
                std::vector<char*> args;
                for (auto& c : command)
                    args.push_back(c.data());
                args.push_back(nullptr);
                ::execvp(args[0], args.data());
                std::perror("yantra-pei-sim: exec");
                ::_exit(127);
            }
            if (child < 0) {
                std::perror("yantra-pei-sim: fork");
                return 2;
            }
        } else {
            std::cout << "yantra-pei-sim: terminal on " << link << " (" << pty.device() << ")" << std::endl;
        }
        int rc = 0;
        while (!g_stop) {
            if (child > 0) {
                int status = 0;
                const pid_t w = ::waitpid(child, &status, WNOHANG);
                if (w == child) {
                    rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
                    break;
                }
            }
            const bool want_unplugged = exists(control);
            if (want_unplugged && pty.plugged())
                pty.unplug();
            else if (!want_unplugged && !pty.plugged())
                pty.replug();
            ::poll(nullptr, 0, 20);
        }
        if (g_stop && child > 0) {
            ::kill(child, SIGTERM);
            ::waitpid(child, nullptr, 0);
            rc = 1;
        }
        pty.unplug();
        ::unlink(control.c_str());
        return rc;
    } catch (const std::exception& e) {
        std::cerr << "yantra-pei-sim: " << e.what() << "\n";
        return 2;
    }
}
