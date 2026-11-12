// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// platform::open_document (UI-14, DOC-02a: Help > User manual (PDF)
// opens the PDF with the system's viewer). No viewer is started here:
// on Linux the opener program is a stand-in ("true", or one that does
// not exist); on Windows the shell is asked for a file that does not
// exist, which it refuses before any program starts.

#include "yantra/core/platform/process.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using yantra::platform::open_document;

TEST_CASE("open document (UI-14): a missing viewer or file is an error with its reason") {
    const fs::path missing = fs::absolute(fs::path("no-such-folder-yantra") / "yantra-manual-0.0.0.pdf");
    std::string error;
#if defined(_WIN32)
    CHECK_FALSE(open_document(missing, error));
    CHECK_FALSE(error.empty());
#else
    CHECK_FALSE(open_document(missing, error, "/no-such-folder-yantra/xdg-open"));
    CHECK(error.find("/no-such-folder-yantra/xdg-open") != std::string::npos);
    // The opener starts and the call returns without waiting for it
    // ("true" stands in for xdg-open, found in PATH like it).
    error.clear();
    CHECK(open_document(missing, error, "true"));
    CHECK(error.empty());
#endif
}
