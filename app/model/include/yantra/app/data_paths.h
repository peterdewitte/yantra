// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the desktop app finds its installed data and keeps the user's
// files (BRD-06 icons, DEMO-05 demo project, UI-14 settings). Pure rules
// over explicit inputs, so tests never touch the real home folder
// (TST-50); data_path_inputs_from_environment() fills them for the app.
//
// Data folders hold the installed layout of share/yantra:
//   logo/png/yantra_logo_<n>.png          (data/logo of the sources)
//   demo/fraunces/fraunces.yantra + recordings/   (target demo_fraunces)
// Search order: YANTRA_DATA_DIR (absolute), <executable>/../share/yantra,
// then, for a program started from a build tree (yantra-source-dir.txt or
// CMakeCache.txt next to its bin/ folder), the build folder (generated
// demo data) and the source tree's data/ folder. Nothing is compiled in, so a package holds
// no build-machine path.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yantra::app {

struct DataPathInputs {
    std::string data_dir_env;              ///< YANTRA_DATA_DIR
    std::filesystem::path executable_dir;  ///< folder of the running program
    std::string xdg_data_home;             ///< XDG_DATA_HOME
    std::string xdg_config_home;           ///< XDG_CONFIG_HOME
    std::string home;                      ///< HOME
};

DataPathInputs data_path_inputs_from_environment();

/// The data folders in search order (existing folders only, no duplicates).
std::vector<std::filesystem::path> data_folders(const DataPathInputs& in);

/// The source folder of a build tree: `build_dir`/yantra-source-dir.txt
/// (written by CMake, copied with the tree to another machine) or
/// `build_dir`/CMakeCache.txt (Yantra_SOURCE_DIR) names it; empty when
/// `build_dir` is not a Yantra build tree. A source folder named without
/// a drive on Windows (a tree made on Linux) is on the build tree's drive.
std::filesystem::path build_tree_source_dir(const std::filesystem::path& build_dir);

/// The first folder's `relative` file that exists.
std::optional<std::filesystem::path> find_data_file(const std::vector<std::filesystem::path>& folders,
                                                    const std::filesystem::path& relative);

/// The user's data folder: $XDG_DATA_HOME/yantra, else
/// $HOME/.local/share/yantra; empty when neither is absolute.
std::filesystem::path user_data_folder(const DataPathInputs& in);
/// The user's config folder (settings.h config_folder).
std::filesystem::path user_config_folder(const DataPathInputs& in);

/// The installed demo project, relative to a data folder.
inline const std::filesystem::path kDemoProjectRelative = "demo/fraunces/fraunces.yantra";
/// The user's copy of the demo project, relative to the user data folder.
inline const std::filesystem::path kDemoCopyRelative = "demo/fraunces";

/// File > Open demo (DEMO-05): the installed project file is a live
/// SQLite document, so the app opens a copy in the user's folder. The
/// copy (the project file and its recordings/ folder) is made when there
/// is none or when the installed project changed since (size or time,
/// remembered in <to>/installed.json); otherwise the user's copy, with
/// any changes made in it, is opened again. Returns the copy's project
/// file, or empty with `error`. `refreshed` tells whether a new copy was
/// made. The caller closes a copy it has open before calling this.
std::filesystem::path prepare_demo_copy(const std::filesystem::path& installed_project,
                                        const std::filesystem::path& to, bool& refreshed, std::string& error);

}  // namespace yantra::app
