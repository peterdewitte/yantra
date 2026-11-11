// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-probe: the headless runner (PLG-24, API-01, API-03, DEMO-10;
// docs/PROBE.md). It measures (and records) without a window, controlled
// by its command line and by commands on standard input, one per line
// ("record start", "monitor off", "play file=a_0000.yrec speed=0",
// "sound on", "status", "rotate", "quit", or the JSON form); each reply is
// one JSON line on standard output. The probe feature plugin, when it is
// in a plugin folder, serves the same commands and the live view over its
// REST/WebSocket API; without it there is no network listener.
//
//   yantra-probe [--demo] [--radio ID[:KEY]] [--no-monitor] [--record]
//                [--record-name NAME] [--split-minutes M] [--keep N]
//                [--compress] [--sound] [--recordings DIR]
//                [--calibration FILE] [--data DIR]
//                [--api [ADDR:]PORT] [--set PLUGIN:KEY=VALUE]...
//                [--disable PLUGIN]... [--no-stdin]
//   yantra-probe commands | --help | --version
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <pthread.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <yantra/core/build_info.h>
#include <yantra/core/control/command.h>
#include <yantra/core/platform/thread_name.h>
#include <yantra/core/headless/runner.h>
#include <yantra/core/plugin/folders.h>
#include <yantra/core/plugin/state_store.h>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};
extern "C" void on_signal(int) { g_stop = true; }

std::mutex g_out;

const char* kProbeId = "yantra.probe";

void usage() {
    std::puts(
        "yantra-probe - headless Yantra runner (API-01, API-03, PLG-24)\n"
        "\n"
        "Usage: yantra-probe [options]      measure without a window until quit/SIGTERM\n"
        "       yantra-probe commands       list the commands of standard input and the API\n"
        "\n"
        "  --demo                 the simulator radio plugin only (DEMO-10)\n"
        "  --radio ID[:KEY]       the radio plugin (and device) to measure with\n"
        "  --no-monitor           do not start measuring at start (monitor on later)\n"
        "  --record               record from the start (fixed probe, API-03)\n"
        "  --record-name NAME     the recording's name (default: date and time)\n"
        "  --split-minutes M      a new file every M minutes (1..60, REC-02)\n"
        "  --keep N               keep only the newest N files (needs --split-minutes)\n"
        "  --compress             zstd-compressed recordings\n"
        "  --sound                sound on (kept; the runner has no sound output)\n"
        "  --recordings DIR       where recordings go (default <data>/probe/recordings)\n"
        "  --calibration FILE     calibration table (RF-02); without it levels stay dBFS\n"
        "  --data DIR             data folder (default $XDG_DATA_HOME/yantra or ~/.local/share/yantra)\n"
        "  --api [ADDR:]PORT      the probe plugin's API address (= --set yantra.probe:api.listen=...)\n"
        "  --set ID:KEY=VALUE     a plugin setting for this run (repeatable)\n"
        "  --disable ID           do not enable this plugin (repeatable)\n"
        "  --no-stdin             do not read commands from standard input\n"
        "  --version, --help\n"
        "\n"
        "Plugins come from the plugin folders (YANTRA_PLUGIN_PATH, the user's\n"
        "and the application's); docs/PROBE.md and docs/API.md describe them.");
}

fs::path default_data() {
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA"); a != nullptr && *a != '\0') return fs::path(a) / "yantra";
#endif
    if (const char* x = std::getenv("XDG_DATA_HOME"); x != nullptr && *x == '/') return fs::path(x) / "yantra";
    if (const char* h = std::getenv("HOME"); h != nullptr && *h == '/') return fs::path(h) / ".local/share/yantra";
    return fs::temp_directory_path() / "yantra";
}

void print_line(const std::string& s) {
    const std::lock_guard lock(g_out);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Reads command lines from standard input until EOF or stop.
void read_commands(yantra::headless::Runner& runner, const std::atomic<bool>& stop) {
    std::string pending;
    char buf[4096];
    while (!stop) {
#if defined(_WIN32)
        // A blocking read; the main thread cancels it at the end
        // (CancelSynchronousIo), Windows has no poll on a console or pipe.
        DWORD got = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf, sizeof buf, &got, nullptr) || got == 0) return;
#else
        pollfd p{0, POLLIN, 0};
        const int n = ::poll(&p, 1, 100);
        if (n <= 0) continue;
        const ssize_t got = ::read(0, buf, sizeof buf);
        if (got <= 0) return;  // EOF: keep running until quit or a signal
#endif
        pending.append(buf, static_cast<std::size_t>(got));
        for (std::size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find_first_not_of(" \t") == std::string::npos) continue;
            print_line(runner.execute_line(line).dump());
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    yantra::headless::RunnerConfig cfg;
    fs::path data, recordings;
    bool read_stdin = true;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> settings;
    std::vector<std::string> disabled;
    auto fail = [](const std::string& m) {
        std::fprintf(stderr, "yantra-probe: %s (see --help)\n", m.c_str());
        return 2;
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (a == "--version") {
            std::printf("yantra-probe %s\n", std::string(yantra::core::build_info().version_string).c_str());
            return 0;
        } else if (a == "commands") {
            for (const auto& c : yantra::control::command_table()) {
                std::string line = c.name;
                for (std::size_t k = 0; k < c.args.size(); ++k)
                    line += k < c.required ? std::string(" ") + c.args[k] : std::string(" [") + c.args[k] + "]";
                std::printf("%-28s %s\n", line.c_str(), c.help);
            }
            return 0;
        } else if (a == "--demo") {
            cfg.demo = true;
        } else if (a == "--radio" && value(v)) {
            cfg.radio = v;
        } else if (a == "--no-monitor") {
            cfg.monitor = false;
        } else if (a == "--record") {
            cfg.record = true;
        } else if (a == "--record-name" && value(v)) {
            cfg.record_name = v;
        } else if (a == "--split-minutes" && value(v)) {
            try {
                cfg.split_minutes = std::stod(v);
            } catch (const std::exception&) {
                return fail("--split-minutes needs a number");
            }
        } else if (a == "--keep" && value(v)) {
            try {
                cfg.keep_files = static_cast<std::uint32_t>(std::stoul(v));
            } catch (const std::exception&) {
                return fail("--keep needs a number");
            }
        } else if (a == "--compress") {
            cfg.compress = true;
        } else if (a == "--sound") {
            cfg.sound = true;
        } else if (a == "--recordings" && value(v)) {
            recordings = v;
        } else if (a == "--calibration" && value(v)) {
            cfg.calibration_table = v;
        } else if (a == "--data" && value(v)) {
            data = v;
        } else if (a == "--api" && value(v)) {
            settings.push_back({kProbeId, {"api.listen", v}});
        } else if (a == "--set" && value(v)) {
            const auto colon = v.find(':');
            const auto eq = v.find('=', colon == std::string::npos ? 0 : colon);
            if (colon == std::string::npos || eq == std::string::npos || colon == 0 || eq == colon + 1)
                return fail("--set needs PLUGIN:KEY=VALUE");
            settings.push_back({v.substr(0, colon), {v.substr(colon + 1, eq - colon - 1), v.substr(eq + 1)}});
        } else if (a == "--disable" && value(v)) {
            disabled.push_back(v);
        } else if (a == "--no-stdin") {
            read_stdin = false;
        } else {
            return fail("unknown or incomplete option " + a);
        }
    }
    if (cfg.keep_files > 0 && cfg.split_minutes <= 0) return fail("--keep needs --split-minutes");
    if (data.empty()) data = default_data();
    cfg.recording_folder = recordings.empty() ? data / "probe" / "recordings" : recordings;
    cfg.data_root = data / "plugin-data";

    yantra::plugin::FolderInputs in = yantra::plugin::folder_inputs_from_environment();
    {
        std::error_code ec;
        const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) in.executable_dir = exe.parent_path();
    }
    const yantra::plugin::PluginFolders folders = yantra::plugin::plugin_folders(in);
    cfg.plugin_folders = folders.plugins;
    cfg.radio_folders = folders.radios;
    auto state = std::make_shared<yantra::plugin::MemoryStateStore>();
    for (const auto& [id, kv] : settings) state->set_setting(id, kv.first, kv.second);
    for (const std::string& id : disabled) state->set_enabled(id, false);
    cfg.state = state;
    cfg.log = [](std::uint32_t level, const std::string& source, const std::string& message) {
        if (level == YANTRA_LOG_DEBUG && std::getenv("YANTRA_PROBE_DEBUG") == nullptr) return;
        static const char* names[] = {"debug", "info", "warning", "error"};
        const std::lock_guard lock(g_out);
        std::fprintf(stderr, "yantra-probe: %s%s%s: %s\n", names[level > 3 ? 3 : level],
                     source.empty() ? "" : " ", source.c_str(), message.c_str());
    };

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    yantra::headless::Runner runner(cfg);
    std::string error;
    if (!runner.start(error)) {
        std::fprintf(stderr, "yantra-probe: %s\n", error.c_str());
        return 1;
    }
    std::fprintf(stderr, "yantra-probe: ready (%s)\n", cfg.demo ? "demo" : "radio");
    std::atomic<bool> stop_input{false};
    std::atomic<bool> input_done{false};
    std::thread input;
    if (read_stdin) input = std::thread([&] {
        yantra::platform::set_thread_name("yt-stdin");
        read_commands(runner, stop_input);
        input_done = true;
    });
    while (!g_stop && !runner.quit_requested()) {
        runner.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    stop_input = true;
#if defined(_WIN32)
    // Ends a read that is waiting for input (again, until the thread has
    // left: a cancel before the read starts would be lost).
    while (input.joinable() && !input_done) {
        CancelSynchronousIo(pthread_gethandle(input.native_handle()));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
#endif
    if (input.joinable()) input.join();
    std::fprintf(stderr, "yantra-probe: stopped\n");
    return 0;
}
