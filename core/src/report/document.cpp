// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The report document's builders, assets, file output and the PDF
// structure check (docs/REPORT.md). check_pdf copied from yantra-v1
// c24e8f5: core/report/pdf_check.cpp.

#include "yantra/core/report/document.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <regex>

#include "yantra/core/export/table.h"

namespace yantra::report {

namespace fs = std::filesystem;

void Document::heading(int level, std::string text, std::string anchor) {
    Block b;
    b.type = Block::Type::Heading;
    b.heading = {level, std::move(text), std::move(anchor)};
    blocks.push_back(std::move(b));
}

void Document::paragraph(std::string text, Tone tone) {
    Block b;
    b.type = Block::Type::Paragraph;
    b.paragraph = {std::move(text), tone};
    blocks.push_back(std::move(b));
}

void Document::key_values(KeyValues kv) {
    Block b;
    b.type = Block::Type::KeyValues;
    b.kv = std::move(kv);
    blocks.push_back(std::move(b));
}

void Document::table(TableBlock t) {
    Block b;
    b.type = Block::Type::Table;
    b.table = std::move(t);
    blocks.push_back(std::move(b));
}

void Document::image(ImageBlock i) {
    Block b;
    b.type = Block::Type::Image;
    b.image = std::move(i);
    blocks.push_back(std::move(b));
}

void Document::legend(LegendBlock l) {
    Block b;
    b.type = Block::Type::Legend;
    b.legend = std::move(l);
    blocks.push_back(std::move(b));
}

void Document::chart(ChartBlock c) {
    Block b;
    b.type = Block::Type::Chart;
    b.chart = std::move(c);
    blocks.push_back(std::move(b));
}

void Document::page_break() {
    Block b;
    b.type = Block::Type::PageBreak;
    blocks.push_back(std::move(b));
}

std::vector<PlanCredit> Document::plan_credits() const {
    std::vector<PlanCredit> out;
    for (const Block& b : blocks)
        if (b.type == Block::Type::Image && b.image.credit &&
            std::find(out.begin(), out.end(), *b.image.credit) == out.end())
            out.push_back(*b.image.credit);
    return out;
}

std::string credit_line(const PlanCredit& c, const Words& w) {
    std::string who = c.title;
    if (!c.credit.empty()) who += (who.empty() ? "" : ", ") + c.credit;
    std::string s = w.plan + ": " + who + ".";
    if (!c.licence.empty()) {
        s += " " + w.licence + " " + c.licence;
        if (!c.licence_url.empty()) s += " (" + c.licence_url + ")";
        s += ".";
    }
    return s;
}

std::int64_t creation_time(std::int64_t now_utc_ns) {
    const char* e = std::getenv("SOURCE_DATE_EPOCH");
    if (e == nullptr || *e == '\0') return now_utc_ns;
    char* end = nullptr;
    const long long s = std::strtoll(e, &end, 10);
    if (end == e || *end != '\0' || s < 0) return now_utc_ns;
    return static_cast<std::int64_t>(s) * 1000000000LL;
}

Assets find_assets(const std::vector<fs::path>& data_folders) {
    Assets a;
    std::vector<std::string> missing;
    auto find = [&](fs::path& out, const fs::path& rel) {
        for (const fs::path& f : data_folders) {
            std::error_code ec;
            if (fs::is_regular_file(f / rel, ec)) {
                out = f / rel;
                return;
            }
        }
        missing.push_back(rel.generic_string());
    };
    find(a.font_regular, "fonts/Inter-Regular.ttf");
    find(a.font_bold, "fonts/Inter-SemiBold.ttf");
    find(a.font_mono, "fonts/JetBrainsMono-Regular.ttf");
    find(a.curtat_logo, "branding/curtat_logo_light_256.png");
    find(a.curtat_logo_small, "branding/curtat_logo_light_64.png");
    find(a.curtat_logo_svg, "branding/curtat_logo.svg");
    find(a.favicon, "logo/png/yantra_logo_32.png");
    if (!missing.empty()) {
        std::string m;
        for (const auto& s : missing) m += (m.empty() ? "" : ", ") + s;
        throw ReportError("report files not found in the data folders: " + m);
    }
    return a;
}

Format format_of(const fs::path& file) {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".pdf") return Format::Pdf;
    if (ext == ".html" || ext == ".htm") return Format::Html;
    throw ReportError("a report is a .pdf or .html file: " + file.filename().string());
}

void write_report(const fs::path& file, const Document& d, const Assets& assets) {
    std::string bytes;
    if (format_of(file) == Format::Pdf) {
        const std::vector<std::uint8_t> pdf = render_pdf(d, assets);
        bytes.assign(pdf.begin(), pdf.end());
    } else {
        bytes = render_html(d, assets);
    }
    try {
        exports::write_file(file, bytes);
    } catch (const exports::ExportError& e) {
        throw ReportError(e.what());
    }
}

PdfCheck check_pdf(const std::vector<std::uint8_t>& bytes) {
    PdfCheck c;
    const std::string s(bytes.begin(), bytes.end());
    if (s.compare(0, 7, "%PDF-1.") != 0) {
        c.error = "no %PDF-1.x header";
        return c;
    }
    c.version = s.substr(5, 3);
    const auto eof = s.rfind("%%EOF");
    if (eof == std::string::npos || s.find_first_not_of("\r\n", eof + 5) != std::string::npos) {
        c.error = "no %%EOF at the end";
        return c;
    }
    const auto sx = s.rfind("startxref");
    if (sx == std::string::npos || sx > eof) {
        c.error = "no startxref";
        return c;
    }
    const long off = std::strtol(s.c_str() + sx + 9, nullptr, 10);
    if (off <= 0 || static_cast<std::size_t>(off) >= s.size() || s.compare(static_cast<std::size_t>(off), 4, "xref") != 0) {
        c.error = "startxref does not point at the cross-reference table";
        return c;
    }
    const auto tr = s.find("trailer", static_cast<std::size_t>(off));
    if (tr == std::string::npos || s.find("/Root", tr) == std::string::npos) {
        c.error = "no trailer with /Root";
        return c;
    }
    // Page objects: "/Type /Page" not followed by "s" (that is the tree).
    static const std::regex page(R"(/Type\s*/Page(?![s\w]))");
    c.pages = static_cast<int>(std::distance(std::sregex_iterator(s.begin(), s.end(), page), std::sregex_iterator()));
    static const std::regex tree(R"(/Type\s*/Pages[\s\S]{0,200}?/Count\s+(\d+))");
    static const std::regex tree2(R"(/Count\s+(\d+)[\s\S]{0,200}?/Type\s*/Pages)");
    std::smatch m;
    int count = -1;
    if (std::regex_search(s, m, tree) || std::regex_search(s, m, tree2)) count = std::atoi(m[1].str().c_str());
    if (count != c.pages) {
        c.error = "page tree /Count " + std::to_string(count) + " differs from " + std::to_string(c.pages) + " page objects";
        return c;
    }
    c.ok = c.pages > 0;
    if (!c.ok) c.error = "no pages";
    return c;
}

}  // namespace yantra::report
