// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TST-52, the reports' half (EXP-05): the coverage and acceptance report
// of the demo project, composed as the app does (its tr(), its DEMO
// label), in every UI language and the pseudo-language. The PDF back end
// notes every text that runs past its box (table cell, key or value
// column, legend column, chart axis, the page's text area), off the page
// or over another text, and every word cut between characters
// (report::PdfOptions::layout_issues): none may be noted. The HTML report
// is laid out by the browser: its style sheet must let every word wrap
// (overflow-wrap:anywhere) and never forbid wrapping (nowrap), so no text
// can run past its box there.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "ui_visit.h"
#include "yantra/core/report/coverage.h"
#include "yantra/core/report/document.h"

using namespace yantra::app;
using namespace yantra::test::visit;
namespace rp = yantra::report;

TEST_CASE("TST-52 the reports: no text past its box, the page or another text, in every language") {
#ifndef YANTRA_TEST_DEMO_DATA_DIR
    return;
#else
    std::string all;
    std::vector<Language> languages(std::begin(kLanguages), std::end(kLanguages));
    languages.push_back(kPseudoLanguage);
    for (const Language l : languages) {
        AppSettings settings;
        settings.language = language_key(l);
        Shell s(lint_paths(scratch(std::string("report_") + language_key(l), "tst52")), settings);
        REQUIRE(s.state.open_demo().ok);
        REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
        rp::CoverageOptions o;
        o.data_folders = s.state.paths().data_folders;
        o.demo_label = s.state.demo_label();
        o.lang = language_key(l);
        o.translate = [](const std::string& english) { return tr(english); };
        o.surveyor = tr("Surveyor");
        o.created_utc_ns = 1793000000LL * 1000000000LL;
        const rp::CoverageReport r = rp::coverage_report(*s.state.project(), o);
        const rp::Assets assets = rp::find_assets(o.data_folders);
        std::vector<std::string> issues;
        rp::PdfOptions po;
        po.layout_issues = &issues;
        const std::vector<std::uint8_t> pdf = rp::render_pdf(r.document, assets, po);
        CHECK(pdf.size() > 1000);
        for (const std::string& i : issues) all += std::string(language_key(l)) + " PDF " + i + "\n";
        const std::string html = rp::render_html(r.document, assets);
        if (html.find("overflow-wrap:anywhere") == std::string::npos)
            all += std::string(language_key(l)) + " HTML: the style sheet does not let words wrap\n";
        if (html.find("nowrap") != std::string::npos)
            all += std::string(language_key(l)) + " HTML: a text may not wrap (nowrap)\n";
        REQUIRE(s.state.close_project().ok);
    }
    CHECK_MESSAGE(all.empty(), all);
#endif
}

TEST_CASE("TST-52 the PDF layout check notes a text past its cell and a cut word") {
    rp::Document d;
    d.title = "Layout";
    rp::TableBlock t;
    t.header = {"A", "B"};
    t.numeric = {false, true};
    t.rows = {{"Averyveryveryveryveryveryveryveryveryveryveryveryveryverylongword", "1"}};
    t.widths = {1, 9};
    rp::Block b;
    b.type = rp::Block::Type::Table;
    b.table = t;
    d.blocks.push_back(b);
    std::vector<std::string> issues;
    rp::PdfOptions po;
    po.layout_issues = &issues;
    (void)rp::render_pdf(d, rp::find_assets({YANTRA_TEST_SOURCE_DATA_DIR}), po);
    std::string all;
    for (const std::string& i : issues) all += i + "\n";
    INFO(all);
    CHECK(all.find("cut between characters") != std::string::npos);
}
