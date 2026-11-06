// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Data folders and the demo copy, see data_paths.h.
#include "yantra/app/data_paths.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>

#include <nlohmann/json.hpp>

#include "yantra/app/settings.h"
#include "yantra/core/platform/dynlib.h"

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

void add_folder(std::vector<fs::path>& out, const fs::path& p) {
    if (p.empty() || !p.is_absolute()) return;
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return;
    const fs::path n = fs::weakly_canonical(p, ec);
    const fs::path key = ec ? p.lexically_normal() : n;
    if (std::find(out.begin(), out.end(), key) == out.end()) out.push_back(key);
}

struct Stamp {
    std::uintmax_t size = 0;
    long long mtime = 0;
    bool operator==(const Stamp&) const = default;
};

std::optional<Stamp> stamp_of(const fs::path& file) {
    std::error_code ec;
    Stamp s;
    s.size = fs::file_size(file, ec);
    if (ec) return std::nullopt;
    const auto t = fs::last_write_time(file, ec);
    if (ec) return std::nullopt;
    s.mtime = static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count());
    return s;
}

std::optional<Stamp> read_stamp(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return std::nullopt;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("size") || !j.contains("mtime_ns")) return std::nullopt;
    if (!j["size"].is_number_unsigned() || !j["mtime_ns"].is_number_integer()) return std::nullopt;
    return Stamp{j["size"].get<std::uintmax_t>(), j["mtime_ns"].get<long long>()};
}

}  // namespace

DataPathInputs data_path_inputs_from_environment() {
    DataPathInputs in;
    in.data_dir_env = env("YANTRA_DATA_DIR");
    in.executable_dir = platform::executable_dir();
    in.xdg_data_home = env("XDG_DATA_HOME");
    in.xdg_config_home = env("XDG_CONFIG_HOME");
    in.home = env("HOME");
    return in;
}

fs::path build_tree_source_dir(const fs::path& build_dir) {
    std::ifstream in(build_dir / "CMakeCache.txt");
    if (!in) return {};
    constexpr std::string_view key = "Yantra_SOURCE_DIR:STATIC=";
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key, 0) == 0) {
            const fs::path p = line.substr(key.size());
            return p.is_absolute() ? p : fs::path{};
        }
    }
    return {};
}

std::vector<fs::path> data_folders(const DataPathInputs& in) {
    std::vector<fs::path> out;
    add_folder(out, in.data_dir_env);
    if (!in.executable_dir.empty()) {
        add_folder(out, in.executable_dir / ".." / "share" / "yantra");
        const fs::path build = (in.executable_dir / "..").lexically_normal();
        const fs::path source = build_tree_source_dir(build);
        if (!source.empty()) {
            add_folder(out, build);
            add_folder(out, source / "data");
        }
    }
    return out;
}

std::optional<fs::path> find_data_file(const std::vector<fs::path>& folders, const fs::path& relative) {
    for (const fs::path& f : folders) {
        std::error_code ec;
        if (fs::is_regular_file(f / relative, ec)) return f / relative;
    }
    return std::nullopt;
}

fs::path user_data_folder(const DataPathInputs& in) {
    if (!in.xdg_data_home.empty() && fs::path(in.xdg_data_home).is_absolute()) return fs::path(in.xdg_data_home) / "yantra";
    if (!in.home.empty() && fs::path(in.home).is_absolute()) return fs::path(in.home) / ".local" / "share" / "yantra";
    return {};
}

fs::path user_config_folder(const DataPathInputs& in) { return config_folder(in.xdg_config_home, in.home); }

fs::path prepare_demo_copy(const fs::path& installed_project, const fs::path& to, bool& refreshed, std::string& error) {
    refreshed = false;
    const auto stamp = stamp_of(installed_project);
    if (!stamp) {
        error = "The demo project is not installed (" + installed_project.string() + " is missing).";
        return {};
    }
    const fs::path target = to / installed_project.filename();
    const fs::path stamp_file = to / "installed.json";
    std::error_code ec;
    if (fs::is_regular_file(target, ec) && read_stamp(stamp_file) == stamp) return target;

    // A new copy: the old one (with SQLite's side files) goes first.
    for (const char* suffix : {"", "-wal", "-shm"}) fs::remove(fs::path(target.string() + suffix), ec);
    fs::remove_all(to / "recordings", ec);
    fs::create_directories(to, ec);
    if (ec) {
        error = "Cannot create " + to.string() + ": " + ec.message();
        return {};
    }
    fs::copy_file(installed_project, target, fs::copy_options::overwrite_existing, ec);
    const fs::path recordings = installed_project.parent_path() / "recordings";
    if (!ec && fs::is_directory(recordings)) fs::copy(recordings, to / "recordings", fs::copy_options::recursive, ec);
    if (ec) {
        error = "Cannot copy the demo project to " + to.string() + ": " + ec.message();
        return {};
    }
    // The copy is the user's: writable even when the installed file is not.
    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add, ec);
    std::ofstream out(stamp_file, std::ios::trunc);
    out << nlohmann::json{{"size", stamp->size}, {"mtime_ns", stamp->mtime}}.dump() << "\n";
    if (!out) {
        error = "Cannot write " + stamp_file.string();
        return {};
    }
    refreshed = true;
    return target;
}

}  // namespace yantra::app
