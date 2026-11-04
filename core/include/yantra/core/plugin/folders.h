// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where plugins are found (PLG-09, PLG-25): the application's plugins
// folder and the user's plugins folder, each with the radio plugins in a
// radios/ subfolder. Loading is the job of PluginLoader (loader.h) and
// RadioHost (radio/radio_host.h).
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plugin/
// loader.h and core/src/plugin/manager.cpp (search paths), adapted for the
// radios/ subfolders and the YANTRA_USER_PLUGIN_DIR override.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yantra::plugin {

// Inputs of the folder rule, normally from the environment. Tests fill
// them by hand and never use the real home folder.
struct FolderInputs {
    std::string plugin_path_env;           // YANTRA_PLUGIN_PATH: extra folders (list), first
    std::string user_plugin_dir;           // YANTRA_USER_PLUGIN_DIR: replaces the user folder
    std::string xdg_data_home;             // XDG_DATA_HOME
    std::string home;                      // HOME
    std::filesystem::path executable_dir;  // folder of the running program
};

FolderInputs folder_inputs_from_environment();

// The plugin folders (PLG-09) in search order:
//  1. each absolute folder of YANTRA_PLUGIN_PATH (development and tests),
//  2. the user's plugins folder: YANTRA_USER_PLUGIN_DIR, else
//     $XDG_DATA_HOME/yantra/plugins, else ~/.local/share/yantra/plugins
//     (%APPDATA%/Yantra/plugins on Windows: DEFERRED),
//  3. the application's plugins folder <executable dir>/../lib/yantra/plugins.
// Relative and empty entries are skipped, duplicates removed. When two
// folders hold a plugin with the same id, the first one found wins.
struct PluginFolders {
    std::vector<std::filesystem::path> plugins;  // plugin.h plugins
    std::vector<std::filesystem::path> radios;   // radio plugins: <folder>/radios
    std::filesystem::path user;                  // the user's folder ("" if unknown)
    std::filesystem::path application;           // the application's folder ("" if unknown)
};

PluginFolders plugin_folders(const FolderInputs& inputs);

// Plugin files (*.so) directly in the given folders, in folder order and
// sorted by name inside a folder; missing folders are skipped.
std::vector<std::filesystem::path> discover_plugin_files(
    const std::vector<std::filesystem::path>& folders);

}  // namespace yantra::plugin
