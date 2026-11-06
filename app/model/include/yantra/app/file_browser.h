// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app file browser (UI-14 Open/Save as, IND-01 new project file,
// IND-10 plan source), as a model without drawing: spec 16.2 has no
// file-dialog library, so the app draws its own small dialog (app/src)
// over this state, and the browsing logic is tested without a window
// (TST-50).
//
// - A folder listing: folders first, then the files that match the active
//   filter (by extension, case-insensitive), each group sorted by name;
//   hidden entries (leading '.') only on request.
// - Places: the home folder and recent folders (those that still exist).
// - Parent, home and typed navigation; a typed name or path is the
//   fallback for everything (relative to the current folder, "~/..." from
//   home; a typed folder opens it).
// - Open: the file must exist. Save: the first extension of the active
//   filter is added when the name has none of the filter's extensions, the
//   folder must exist, and an existing file needs an explicit overwrite
//   confirmation.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/file_browser.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yantra::app {

enum class BrowseMode { Open, Save };

/// A filename filter: lower-case extensions with the dot; none = all files.
struct FileFilter {
    std::string label;  ///< "Yantra projects (*.yantra)"
    std::vector<std::string> extensions;
    bool matches(const std::filesystem::path& file) const;
    bool operator==(const FileFilter&) const = default;
};

struct BrowserEntry {
    std::string name;
    std::filesystem::path path;
    bool directory = false;
    std::uintmax_t size = 0;  ///< bytes (files)
    bool operator==(const BrowserEntry&) const = default;
};

struct BrowserPlace {
    std::string label;
    std::filesystem::path path;
};

struct BrowseOutcome {
    enum class Kind {
        None,              ///< nothing chosen yet (e.g. a folder was opened)
        Chosen,            ///< `path` is the answer; close the browser
        ConfirmOverwrite,  ///< Save: `path` exists; ask, then confirm_overwrite()
        Error,             ///< `message` says why; the browser stays open
    };
    Kind kind = Kind::None;
    std::filesystem::path path;
    std::string message;
};

/// Filters for project files: *.yantra, then all files.
std::vector<FileFilter> project_file_filters();
/// Filters for plan sources (IND-02, IND-10): PDF (when supported) and the
/// stb_image formats, then all files.
std::vector<FileFilter> plan_file_filters();

class FileBrowser {
public:
    /// `start` is the folder shown first (falls back to `home`, then "/"
    /// when it cannot be listed); `recent` are folders offered as places.
    FileBrowser(BrowseMode mode, std::string title, std::vector<FileFilter> filters, std::filesystem::path start,
                std::filesystem::path home, std::vector<std::filesystem::path> recent = {},
                std::string file_name = {});

    BrowseMode mode() const noexcept { return mode_; }
    const std::string& title() const noexcept { return title_; }

    // -- folder ---------------------------------------------------------
    const std::filesystem::path& folder() const noexcept { return folder_; }
    const std::vector<BrowserEntry>& entries() const noexcept { return entries_; }
    /// Why the folder could not be listed ("" when it could).
    const std::string& folder_error() const noexcept { return folder_error_; }
    std::vector<BrowserPlace> places() const;
    bool can_go_up() const;
    void go_up();
    void go_home();
    /// Opens a folder; false (and the current folder stays) when it is not
    /// a readable folder.
    bool navigate(const std::filesystem::path& folder);
    void refresh();

    // -- filter ---------------------------------------------------------
    const std::vector<FileFilter>& filters() const noexcept { return filters_; }
    std::size_t active_filter() const noexcept { return active_filter_; }
    void set_filter(std::size_t index);
    bool show_hidden() const noexcept { return show_hidden_; }
    void set_show_hidden(bool on);

    // -- choice ---------------------------------------------------------
    /// The typed name or path (also set by selecting a file).
    const std::string& file_name() const noexcept { return file_name_; }
    void set_file_name(std::string name);
    /// Selected entry index, or nullopt.
    std::optional<std::size_t> selected() const noexcept { return selected_; }
    /// Selects an entry; a file's name goes into file_name().
    void select(std::size_t index);
    /// Double click / Enter on an entry: a folder opens, a file is chosen
    /// as by confirm().
    BrowseOutcome activate(std::size_t index);
    /// The OK button: resolves file_name() (see the file comment).
    BrowseOutcome confirm();
    /// The file waiting for an overwrite answer (Save), if any.
    const std::optional<std::filesystem::path>& pending_overwrite() const noexcept { return pending_overwrite_; }
    BrowseOutcome confirm_overwrite();
    void cancel_overwrite();

    /// The path file_name() stands for (no checks): typed absolute paths,
    /// "~/..." and names relative to the current folder.
    std::filesystem::path resolve(const std::string& typed) const;

private:
    void list();

    BrowseMode mode_;
    std::string title_;
    std::vector<FileFilter> filters_;
    std::size_t active_filter_ = 0;
    std::filesystem::path folder_;
    std::filesystem::path home_;
    std::vector<std::filesystem::path> recent_;
    std::vector<BrowserEntry> entries_;
    std::string folder_error_;
    bool show_hidden_ = false;
    std::string file_name_;
    std::optional<std::size_t> selected_;
    std::optional<std::filesystem::path> pending_overwrite_;
};

/// A human size: "512 B", "3.4 KB", "12.0 MB".
std::string format_size(std::uintmax_t bytes);

}  // namespace yantra::app
