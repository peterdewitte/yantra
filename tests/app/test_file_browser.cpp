// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app file browser model (TST-50): listing, filters, places,
// navigation, typed paths, Open and Save checks, overwrite confirmation.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "yantra/app/file_browser.h"

namespace fs = std::filesystem;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    // Absolute with its drive on Windows (the build tree names "/home/..."),
    // as the folders the code under test gives back.
    const fs::path d = fs::absolute(fs::path(YANTRA_TEST_SCRATCH_DIR) / "browser" / name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void touch(const fs::path& p, std::size_t bytes = 0) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << std::string(bytes, 'x');
}

std::vector<std::string> names(const FileBrowser& b) {
    std::vector<std::string> out;
    for (const BrowserEntry& e : b.entries()) out.push_back(e.directory ? e.name + "/" : e.name);
    return out;
}

}  // namespace

TEST_CASE("file browser: folders first, filter by extension, hidden entries, sizes") {
    const fs::path d = scratch("list");
    touch(d / "b.yantra", 2048);
    touch(d / "A.YANTRA");
    touch(d / "notes.txt");
    touch(d / ".hidden.yantra");
    fs::create_directories(d / "zeta");
    fs::create_directories(d / "Alpha");
    fs::create_directories(d / ".cache");

    FileBrowser b(BrowseMode::Open, "Open project", project_file_filters(), d, d);
    CHECK(b.folder() == d);
    CHECK(b.folder_error().empty());
    CHECK(names(b) == std::vector<std::string>{"Alpha/", "zeta/", "A.YANTRA", "b.yantra"});
    CHECK(b.entries()[3].size == 2048);
    CHECK(format_size(2048) == "2.0 KB");
    CHECK(format_size(12) == "12 B");
    CHECK(format_size(5U * 1024U * 1024U) == "5.0 MB");

    b.set_filter(1);  // all files
    CHECK(names(b) == std::vector<std::string>{"Alpha/", "zeta/", "A.YANTRA", "b.yantra", "notes.txt"});
    b.set_show_hidden(true);
    CHECK(names(b) == std::vector<std::string>{".cache/", "Alpha/", "zeta/", ".hidden.yantra", "A.YANTRA", "b.yantra",
                                               "notes.txt"});
    b.set_filter(7);  // out of range: ignored
    CHECK(b.active_filter() == 1);

    const auto plans = plan_file_filters();
    REQUIRE(plans.size() == 2);
    for (const char* e : {".png", ".jpg", ".bmp", ".gif", ".tga", ".psd", ".pnm", ".hdr"})
        CHECK(plans[0].matches(fs::path("plan") += e));
    CHECK(plans[0].matches("PLAN.PNG"));
    CHECK_FALSE(plans[0].matches("plan.yantra"));
    CHECK(plans[1].matches("anything"));
}

TEST_CASE("file browser: places, parent, home and typed navigation") {
    const fs::path d = scratch("nav");
    const fs::path home = d / "home";
    fs::create_directories(home / "projects" / "office");
    fs::create_directories(d / "elsewhere");

    FileBrowser b(BrowseMode::Open, "Open", project_file_filters(), home / "projects",
                  home, {d / "elsewhere", d / "missing", home, d / "elsewhere/"});
    const auto places = b.places();
    REQUIRE(places.size() == 2);  // Home, elsewhere (missing and duplicates dropped)
    CHECK(places[0].label == "Home");
    CHECK(places[0].path == home);
    CHECK(places[1].label == "elsewhere");

    CHECK(b.folder() == home / "projects");
    REQUIRE(b.can_go_up());
    b.go_up();
    CHECK(b.folder() == home);
    b.go_home();
    CHECK(b.folder() == home);
    CHECK(b.navigate(home / "projects" / "office"));
    CHECK(b.folder() == home / "projects" / "office");
    CHECK_FALSE(b.navigate(d / "no-such-folder"));  // stays where it is
    CHECK(b.folder() == home / "projects" / "office");
    CHECK(b.folder_error().find("no-such-folder") != std::string::npos);

    // A typed folder opens it (relative, "~/", absolute).
    b.set_file_name("..");
    CHECK(b.confirm().kind == BrowseOutcome::Kind::None);
    CHECK(b.folder() == home / "projects");
    b.set_file_name("~/projects/office");
    CHECK(b.confirm().kind == BrowseOutcome::Kind::None);
    CHECK(b.folder() == home / "projects" / "office");
    b.set_file_name((d / "elsewhere").string());
    CHECK(b.confirm().kind == BrowseOutcome::Kind::None);
    CHECK(b.folder() == d / "elsewhere");
    CHECK(b.file_name().empty());
    CHECK(b.resolve("~") == home);

    // Activating a folder entry opens it.
    b.go_home();
    REQUIRE(names(b) == std::vector<std::string>{"projects/"});
    CHECK(b.activate(0).kind == BrowseOutcome::Kind::None);
    CHECK(b.folder() == home / "projects");

    // The root has no parent; an unreadable start falls back to home.
    FileBrowser root(BrowseMode::Open, "Open", {}, "/", home);
    CHECK_FALSE(root.can_go_up());
    FileBrowser fallback(BrowseMode::Open, "Open", {}, d / "gone", home);
    CHECK(fallback.folder() == home);
}

TEST_CASE("file browser: Open needs an existing file") {
    const fs::path d = scratch("open");
    touch(d / "site.yantra");
    FileBrowser b(BrowseMode::Open, "Open", project_file_filters(), d, d);

    BrowseOutcome o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Error);  // nothing chosen
    b.set_file_name("other.yantra");
    o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Error);
    CHECK(o.message.find("does not exist") != std::string::npos);

    REQUIRE(b.entries().size() == 1);
    b.select(0);
    CHECK(b.selected() == 0U);
    CHECK(b.file_name() == "site.yantra");
    o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Chosen);
    CHECK(o.path == d / "site.yantra");
    o = b.activate(0);  // double click
    CHECK(o.kind == BrowseOutcome::Kind::Chosen);
    CHECK(o.path == d / "site.yantra");
}

TEST_CASE("file browser: Save adds the extension and asks before overwriting") {
    const fs::path d = scratch("save");
    touch(d / "old.yantra", 10);
    FileBrowser b(BrowseMode::Save, "Save as", project_file_filters(), d, d, {}, "new");

    BrowseOutcome o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Chosen);
    CHECK(o.path == d / "new.yantra");  // extension added

    b.set_file_name("old");
    o = b.confirm();
    REQUIRE(o.kind == BrowseOutcome::Kind::ConfirmOverwrite);
    CHECK(o.path == d / "old.yantra");
    CHECK(o.message.find("already exists") != std::string::npos);
    REQUIRE(b.pending_overwrite());
    b.cancel_overwrite();
    CHECK_FALSE(b.pending_overwrite());
    CHECK(b.confirm_overwrite().kind == BrowseOutcome::Kind::None);  // nothing pending
    REQUIRE(b.confirm().kind == BrowseOutcome::Kind::ConfirmOverwrite);
    o = b.confirm_overwrite();
    CHECK(o.kind == BrowseOutcome::Kind::Chosen);
    CHECK(o.path == d / "old.yantra");
    CHECK(fs::file_size(d / "old.yantra") == 10);  // the model never deletes

    // Typing another name drops a pending question.
    REQUIRE(b.confirm().kind == BrowseOutcome::Kind::ConfirmOverwrite);
    b.set_file_name("third");
    CHECK_FALSE(b.pending_overwrite());

    // A missing folder, and a name that is a folder.
    b.set_file_name("nowhere/x");
    o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Error);
    CHECK(o.message.find("does not exist") != std::string::npos);
    fs::create_directories(d / "dir.yantra");
    b.set_file_name("dir.yantra");
    CHECK(b.confirm().kind == BrowseOutcome::Kind::None);  // opens the folder
    CHECK(b.folder() == d / "dir.yantra");

    // "All files" keeps the name as typed.
    b.navigate(d);
    b.set_filter(1);
    b.set_file_name("plain.txt");
    o = b.confirm();
    CHECK(o.kind == BrowseOutcome::Kind::Chosen);
    CHECK(o.path == d / "plain.txt");
    b.set_file_name("");
    CHECK(b.confirm().kind == BrowseOutcome::Kind::Error);
}
