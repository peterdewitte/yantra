// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UI lint at run time (DOC-20, DOC-07, UI-12, UI-13): every window of
// the app (ui_visit.h), drawn in every UI language, in the desktop layout
// and in the touch layout: each control has a tooltip, each disabled one
// its reason, controls do not overlap, are not cut off or squashed, touch
// targets are at least 48 px, and every text has its translation
// (tests/app/ui/ui_lint.h). The other window sizes, UI scales, themes and
// the pseudo-language, and every text drawn, are the GUI text-overflow
// test's (TST-52, tests/app/overflow), which replaced step 9.11's TST-50
// layout check.
#include <doctest/doctest.h>

#include <string>

#include "ui_lint.h"
#include "ui_visit.h"

using namespace yantra::app;
using namespace yantra::test::visit;
using yantra::test::UiLint;

namespace {

void run_lint(const char* tag, bool touch) {
    std::string all;
    int frames = 0;
    for (Language l : kLanguages) {
        AppSettings settings;
        settings.language = language_key(l);
        settings.touch = touch;
        Shell s(lint_paths(scratch(std::string(tag) + "_" + language_key(l))), settings);
        s.ui.set_display_size(1280.0F, 800.0F);  // a 10" tablet, a laptop
        REQUIRE(s.ui.frames(3));
        UiLint lint{s.ui, touch};
        visit_all(s, [&](const std::string& where) { lint.check(where); }, std::string(language_key(l)) + " " + tag);
        frames += lint.frames_checked;
        for (const std::string& i : lint.issues) all += i + "\n";
    }
    INFO("frames checked: " << frames);
    CHECK_MESSAGE(all.empty(), all);
    CHECK(frames > 30);
}

}  // namespace

TEST_CASE("DOC-20 UI lint: every window in every language, desktop layout") { run_lint("desktop", false); }

TEST_CASE("DOC-20 UI lint: every window in every language, touch layout (UI-13)") { run_lint("touch", true); }
