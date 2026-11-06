// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app file browser model, see file_browser.h.
// Copied from yantra-v2 68ea20a: app/model/src/file_browser.cpp.
#include "yantra/app/file_browser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>

#include "yantra/core/plan/source.h"
#include "yantra/core/project/project.h"

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

/// Absolute and without "." / ".." parts (no symlinks resolved).
fs::path clean(const fs::path& p) {
    std::error_code ec;
    fs::path a = p.is_absolute() ? p : fs::absolute(p, ec);
    a = a.lexically_normal();
    // "/a/b/" -> "/a/b" (but keep "/").
    if (a.has_relative_path() && a.filename().empty()) a = a.parent_path();
    return a;
}

}  // namespace

bool FileFilter::matches(const fs::path& file) const {
    if (extensions.empty()) return true;
    const std::string e = lower(file.extension().string());
    return std::find(extensions.begin(), extensions.end(), e) != extensions.end();
}

std::vector<FileFilter> project_file_filters() {
    return {FileFilter{"Yantra projects (*.yantra)", {project::kExtension}}, FileFilter{"All files", {}}};
}

std::vector<FileFilter> plan_file_filters() {
    const std::vector<std::string> ext = plan::supported_extensions();
    std::string label = plan::pdf_supported() ? "Plans: PDF and images (" : "Plans: images (";
    for (std::size_t i = 0; i < ext.size(); ++i) label += (i == 0 ? "*" : " *") + ext[i];
    label += ")";
    return {FileFilter{label, ext}, FileFilter{"All files", {}}};
}

std::string format_size(std::uintmax_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof buf, "%ju B", bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

FileBrowser::FileBrowser(BrowseMode mode, std::string title, std::vector<FileFilter> filters, fs::path start,
                         fs::path home, std::vector<fs::path> recent, std::string file_name)
    : mode_(mode),
      title_(std::move(title)),
      filters_(std::move(filters)),
      home_(home.empty() ? fs::path("/") : clean(home)),
      recent_(std::move(recent)),
      file_name_(std::move(file_name)) {
    if (filters_.empty()) filters_.push_back(FileFilter{"All files", {}});
    if (!navigate(start.empty() ? home_ : start) && !navigate(home_)) navigate("/");
}

std::vector<BrowserPlace> FileBrowser::places() const {
    std::vector<BrowserPlace> out;
    out.push_back(BrowserPlace{"Home", home_});
    for (const fs::path& r : recent_) {
        const fs::path c = clean(r);
        if (!is_dir(c)) continue;
        const bool seen = std::any_of(out.begin(), out.end(), [&](const BrowserPlace& p) { return p.path == c; });
        if (seen) continue;
        const std::string name = c.filename().empty() ? c.string() : c.filename().string();
        out.push_back(BrowserPlace{name, c});
    }
    return out;
}

bool FileBrowser::can_go_up() const { return folder_.has_relative_path(); }

void FileBrowser::go_up() {
    if (can_go_up()) navigate(folder_.parent_path());
}

void FileBrowser::go_home() { navigate(home_); }

bool FileBrowser::navigate(const fs::path& folder) {
    const fs::path c = clean(folder);
    std::error_code ec;
    fs::directory_iterator probe(c, ec);
    if (ec || !is_dir(c)) {
        folder_error_ = "Cannot open the folder " + c.string() + ": " +
                        (ec ? ec.message() : std::string("not a folder"));
        if (folder_.empty()) {
            folder_ = c;
            entries_.clear();
        }
        return false;
    }
    folder_ = c;
    selected_.reset();
    pending_overwrite_.reset();
    list();
    return true;
}

void FileBrowser::refresh() { list(); }

void FileBrowser::list() {
    entries_.clear();
    folder_error_.clear();
    std::error_code ec;
    fs::directory_iterator it(folder_, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        folder_error_ = "Cannot list " + folder_.string() + ": " + ec.message();
        return;
    }
    std::vector<BrowserEntry> dirs, files;
    const FileFilter& filter = filters_[active_filter_];
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::path p = it->path();
        const std::string name = p.filename().string();
        if (!show_hidden_ && !name.empty() && name[0] == '.') continue;
        std::error_code e2;
        if (it->is_directory(e2)) {
            dirs.push_back(BrowserEntry{name, p, true, 0});
        } else if (filter.matches(p)) {
            const std::uintmax_t size = it->is_regular_file(e2) ? it->file_size(e2) : 0;
            files.push_back(BrowserEntry{name, p, false, e2 ? 0 : size});
        }
    }
    auto by_name = [](const BrowserEntry& a, const BrowserEntry& b) {
        const std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    entries_ = std::move(dirs);
    entries_.insert(entries_.end(), files.begin(), files.end());
    selected_.reset();
}

void FileBrowser::set_filter(std::size_t index) {
    if (index >= filters_.size() || index == active_filter_) return;
    active_filter_ = index;
    list();
}

void FileBrowser::set_show_hidden(bool on) {
    if (on == show_hidden_) return;
    show_hidden_ = on;
    list();
}

void FileBrowser::set_file_name(std::string name) {
    file_name_ = std::move(name);
    pending_overwrite_.reset();
}

void FileBrowser::select(std::size_t index) {
    if (index >= entries_.size()) return;
    selected_ = index;
    if (!entries_[index].directory) set_file_name(entries_[index].name);
}

BrowseOutcome FileBrowser::activate(std::size_t index) {
    if (index >= entries_.size()) return {};
    const BrowserEntry e = entries_[index];
    if (e.directory) {
        navigate(e.path);
        return {};
    }
    set_file_name(e.name);
    return confirm();
}

fs::path FileBrowser::resolve(const std::string& typed) const {
    if (typed == "~") return home_;
    if (typed.rfind("~/", 0) == 0) return clean(home_ / typed.substr(2));
    const fs::path t(typed);
    return clean(t.is_absolute() ? t : folder_ / t);
}

BrowseOutcome FileBrowser::confirm() {
    BrowseOutcome out;
    pending_overwrite_.reset();
    if (file_name_.empty()) {
        out.kind = BrowseOutcome::Kind::Error;
        out.message = mode_ == BrowseMode::Open ? "Choose a file." : "Type a file name.";
        return out;
    }
    fs::path p = resolve(file_name_);
    if (is_dir(p)) {  // a typed folder opens it
        if (navigate(p)) {
            file_name_.clear();
            return out;
        }
        out.kind = BrowseOutcome::Kind::Error;
        out.message = folder_error_;
        return out;
    }
    std::error_code ec;
    if (mode_ == BrowseMode::Open) {
        if (!fs::is_regular_file(p, ec)) {
            out.kind = BrowseOutcome::Kind::Error;
            out.message = "The file " + p.string() + " does not exist.";
            return out;
        }
        out.kind = BrowseOutcome::Kind::Chosen;
        out.path = p;
        return out;
    }
    const FileFilter& filter = filters_[active_filter_];
    if (!filter.extensions.empty() && !filter.matches(p)) p += filter.extensions.front();
    if (!is_dir(p.parent_path())) {
        out.kind = BrowseOutcome::Kind::Error;
        out.message = "The folder " + p.parent_path().string() + " does not exist.";
        return out;
    }
    if (fs::exists(p, ec)) {
        if (is_dir(p)) {
            out.kind = BrowseOutcome::Kind::Error;
            out.message = p.string() + " is a folder.";
            return out;
        }
        pending_overwrite_ = p;
        out.kind = BrowseOutcome::Kind::ConfirmOverwrite;
        out.path = p;
        out.message = "The file " + p.filename().string() + " already exists. Replace it?";
        return out;
    }
    out.kind = BrowseOutcome::Kind::Chosen;
    out.path = p;
    return out;
}

BrowseOutcome FileBrowser::confirm_overwrite() {
    BrowseOutcome out;
    if (!pending_overwrite_) return out;
    out.kind = BrowseOutcome::Kind::Chosen;
    out.path = *pending_overwrite_;
    pending_overwrite_.reset();
    return out;
}

void FileBrowser::cancel_overwrite() { pending_overwrite_.reset(); }

}  // namespace yantra::app
