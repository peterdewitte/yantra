// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/plugin/manager.cpp (search
// paths), adapted for the radios/ subfolders (PLG-09, PLG-25).

#include "yantra/core/plugin/folders.h"

#include <algorithm>
#include <system_error>

#include "yantra/core/platform/dynlib.h"

namespace yantra::plugin {

FolderInputs folder_inputs_from_environment() {
    FolderInputs in;
    in.plugin_path_env = platform::environment("YANTRA_PLUGIN_PATH");
    in.user_plugin_dir = platform::environment("YANTRA_USER_PLUGIN_DIR");
    in.xdg_data_home = platform::environment("XDG_DATA_HOME");
    in.home = platform::environment("HOME");
    in.executable_dir = platform::executable_dir();
    return in;
}

PluginFolders plugin_folders(const FolderInputs& in) {
    PluginFolders out;
    auto add = [&](const std::filesystem::path& p) {
        if (p.empty() || !p.is_absolute()) return std::filesystem::path{};
        const std::filesystem::path n = p.lexically_normal();
        if (std::find(out.plugins.begin(), out.plugins.end(), n) == out.plugins.end())
            out.plugins.push_back(n);
        return n;
    };
    std::string item;
    for (char c : in.plugin_path_env + platform::path_list_separator()) {
        if (c == platform::path_list_separator()) {
            add(item);
            item.clear();
        } else {
            item += c;
        }
    }
    if (!in.user_plugin_dir.empty()) {
        out.user = add(in.user_plugin_dir);
    } else if (!in.xdg_data_home.empty() && std::filesystem::path(in.xdg_data_home).is_absolute()) {
        out.user = add(std::filesystem::path(in.xdg_data_home) / "yantra" / "plugins");
    } else if (!in.home.empty()) {
        out.user = add(std::filesystem::path(in.home) / ".local" / "share" / "yantra" / "plugins");
    }
    if (!in.executable_dir.empty())
        out.application = add(in.executable_dir / ".." / "lib" / "yantra" / "plugins");
    for (const auto& p : out.plugins) out.radios.push_back(p / "radios");
    return out;
}

std::vector<std::filesystem::path> discover_plugin_files(
    const std::vector<std::filesystem::path>& folders) {
    std::vector<std::filesystem::path> out;
    for (const auto& folder : folders) {
        std::error_code ec;
        std::vector<std::filesystem::path> here;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code fec;
            if (it->is_regular_file(fec) && it->path().extension() == platform::plugin_file_suffix())
                here.push_back(it->path());
        }
        std::sort(here.begin(), here.end());
        out.insert(out.end(), here.begin(), here.end());
    }
    return out;
}

}  // namespace yantra::plugin
