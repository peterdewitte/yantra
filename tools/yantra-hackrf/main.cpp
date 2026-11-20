// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-hackrf (RAD-66): see hackrf_app.hpp.
#include "hackrf_app.hpp"

#include <csignal>
#include <filesystem>
#include <iostream>

namespace {
std::atomic<bool> g_interrupt{false};
extern "C" void on_signal(int) { g_interrupt = true; }

std::filesystem::path self_path(const char* argv0)
{
    std::error_code ec;
#ifndef _WIN32
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec)
        return p;
#endif
    return std::filesystem::absolute(argv0, ec);
}
} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    yantra::hackrf_tool::Environment env;
    env.out = &std::cout;
    env.err = &std::cerr;
    env.in = &std::cin;
    env.tool_path = self_path(argv[0]);
    env.interrupt = &g_interrupt;
    return yantra::hackrf_tool::run(std::vector<std::string>(argv + 1, argv + argc), env);
}
