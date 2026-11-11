// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The report engine (EXP-05, EXP-06; docs/REPORT.md): a report is a
// format-neutral Document (a cover, then headings, paragraphs, key/value
// lists, tables, images with their plan credits, legends, charts and page
// breaks), laid out by two back ends that show the same content:
//   - PDF with libharu (spec 16.2): A4 portrait, the text in an embedded
//     subset of Inter (every character of names and notes prints), the
//     numeric table columns in an embedded subset of JetBrains Mono;
//   - HTML: one self-contained file (inline CSS, pictures as data: URIs,
//     the curtat.be logo SVG, the Yantra logo as favicon, BRD-06).
// Both carry the curtat.be logo and the reference "Made with Yantra -
// curtat.be" with its link on the cover and in the footer of every page
// (BRD-02), next to the DEMO label in demo mode (DEMO-07). The composer
// (the report content, 10.3) fills the blocks with final text: translated
// and with its numbers formatted; the back ends only lay it out.
// Copied from yantra-v1 c24e8f5: core/report/document.h, adapted for
// EXP-05 (embedded fonts, cover, branding, plan credits, DEMO-12).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "yantra/core/export/raster.h"

namespace yantra::report {

class ReportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// BRD-02: the reference on the cover and in every page footer.
inline constexpr const char* kReference = "Made with Yantra - curtat.be";
inline constexpr const char* kReferenceUrl = "https://curtat.be";

enum class Tone { Normal, Note, Pass, Fail, Demo };

struct Heading {
    int level = 1;  ///< 1 = section (e.g. a floor), 2 = subsection
    std::string text;
    std::string anchor;  ///< HTML id ("" none)
};

struct Paragraph {
    std::string text;
    Tone tone = Tone::Normal;
};

struct KeyValues {
    std::vector<std::pair<std::string, std::string>> rows;
};

struct TableBlock {
    std::vector<std::string> header;
    /// Per column: a numeric column is right-aligned and set in the
    /// monospaced font (JetBrains Mono in the PDF).
    std::vector<bool> numeric;
    std::vector<std::vector<std::string>> rows;
    std::vector<Tone> row_tone;  ///< per row (empty: Normal)
    std::vector<double> widths;  ///< relative column widths (empty: equal)
};

/// The licence and credit of a plan shown in a picture (DEMO-12: a MAK
/// sheet's "MAK - Museum of Applied Arts, Vienna", designer, inventory
/// number and CC BY-NC-ND 4.0), printed with the picture and in the
/// report's list of plan credits.
struct PlanCredit {
    std::string title;        ///< the sheet's title ("" none)
    std::string credit;       ///< holder; designer, year, inventory number
    std::string licence;      ///< e.g. "CC BY-NC-ND 4.0"
    std::string licence_url;  ///< e.g. https://creativecommons.org/licenses/by-nc-nd/4.0/
    std::string item_url;     ///< the catalogue page ("" none)
    bool restricted = false;  ///< a licence-restricted sheet (never stored or shipped)
    bool operator==(const PlanCredit&) const = default;
};

struct ImageBlock {
    exports::Canvas image;
    std::string alt;
    std::string caption;
    /// The credit of the plan in the picture (report/figure.h fills it);
    /// printed under the picture.
    std::optional<PlanCredit> credit;
};

struct LegendBlock {
    std::string title;
    struct Entry {
        std::uint32_t rgb = 0;  ///< 0xRRGGBB
        std::string label;
        std::string count;
    };
    std::vector<Entry> entries;
};

/// A bar chart (histogram) or a step line (CDF) on numeric axes.
struct ChartBlock {
    enum class Kind { Bars, Steps } kind = Kind::Bars;
    std::string title, x_label, y_label;
    double x_min = 0, x_max = 1, y_min = 0, y_max = 1;
    double x_tick = 10, y_tick = 10;
    std::vector<std::string> x_tick_labels;  ///< at x_min + k x_tick
    std::vector<std::string> y_tick_labels;  ///< at y_min + k y_tick
    /// Bars: [x0, x1) with height y; Steps: points (x, y) joined as steps.
    struct Bar {
        double x0 = 0, x1 = 0, y = 0;
        bool good = false;  ///< bar colour (meets the threshold)
    };
    std::vector<Bar> bars;
    std::vector<std::pair<double, double>> steps;
    bool has_marker = false;  ///< vertical threshold line
    double marker_x = 0;
    std::string marker_label;
    std::string summary;  ///< text alternative (HTML figcaption, PDF caption)
};

struct Block {
    enum class Type { Heading, Paragraph, KeyValues, Table, Image, Legend, Chart, PageBreak };
    Type type = Type::Paragraph;
    Heading heading;
    Paragraph paragraph;
    KeyValues kv;
    TableBlock table;
    ImageBlock image;
    LegendBlock legend;
    ChartBlock chart;
};

/// The fixed words of the layout, translated by the composer.
struct Words {
    std::string page = "Page";            ///< footer "Page 2 of 5"
    std::string of = "of";
    std::string plan = "Plan";            ///< "Plan: <credit>. Licence <licence>"
    std::string licence = "Licence";
    std::string plan_credits = "Plan credits";  ///< heading of the list at the end
};

struct Document {
    std::string lang = "en";  ///< BCP 47 code of the text
    std::string title;
    std::string subtitle;
    /// The cover's details (project, site, operator, date, equipment, ...).
    KeyValues cover;
    std::string demo_label;  ///< DEMO-07: on the cover and every page when not empty
    std::string footer;      ///< e.g. "Yantra 0.x - 2026-11-08 10:00 UTC"
    std::string generator;   ///< "Yantra <version>": HTML meta, PDF creator
    /// UTC creation time in ns since the epoch (PDF CreationDate; 0 = none).
    /// creation_time() gives a fixed one for reproducible output.
    std::int64_t created_utc_ns = 0;
    Words words;
    std::vector<Block> blocks;

    void heading(int level, std::string text, std::string anchor = {});
    void paragraph(std::string text, Tone tone = Tone::Normal);
    void key_values(KeyValues kv);
    void table(TableBlock t);
    void image(ImageBlock i);
    void legend(LegendBlock l);
    void chart(ChartBlock c);
    void page_break();

    /// The distinct plan credits of the pictures, in order of appearance.
    std::vector<PlanCredit> plan_credits() const;
};

/// "Plan: <title>, <credit>. Licence <licence> (<url>)." for a picture.
std::string credit_line(const PlanCredit& c, const Words& w);

/// The time for Document::created_utc_ns: the SOURCE_DATE_EPOCH
/// environment variable (seconds, the reproducible-builds convention)
/// when it is set, else `now_utc_ns`. Tests and the committed sample
/// report set it, so the output does not depend on the clock.
std::int64_t creation_time(std::int64_t now_utc_ns);

/// The files the back ends use, found in Yantra's data folders
/// (data/ in the source tree, share/yantra/ installed): never
/// compiled-in paths.
struct Assets {
    std::filesystem::path font_regular;    ///< fonts/Inter-Regular.ttf
    std::filesystem::path font_bold;       ///< fonts/Inter-SemiBold.ttf
    std::filesystem::path font_mono;       ///< fonts/JetBrainsMono-Regular.ttf
    std::filesystem::path curtat_logo;     ///< branding/curtat_logo_light_256.png (white pages)
    std::filesystem::path curtat_logo_small;  ///< branding/curtat_logo_light_64.png
    std::filesystem::path curtat_logo_svg;    ///< branding/curtat_logo.svg (HTML)
    std::filesystem::path favicon;            ///< logo/png/yantra_logo_32.png (HTML, BRD-06)
};
/// Each file from the first data folder that holds it; ReportError
/// naming the missing ones.
Assets find_assets(const std::vector<std::filesystem::path>& data_folders);

struct HtmlOptions {};
/// Self-contained HTML5 (XHTML-compatible markup, so it parses as XML).
/// ReportError when an asset cannot be read.
std::string render_html(const Document& d, const Assets& assets, const HtmlOptions& o = {});

struct PdfOptions {
    bool compress = true;  ///< deflate the streams (false: readable content for tests)
    /// TST-52: when set, every text that runs past its box (its table cell,
    /// key or value column, legend column, chart axis or the page's text
    /// area), off the page, over another text, and every word too wide for
    /// its line (cut between characters) is noted here, one line each.
    std::vector<std::string>* layout_issues = nullptr;
};
/// A4 portrait PDF via libharu. ReportError when an asset cannot be read
/// or libharu fails.
std::vector<std::uint8_t> render_pdf(const Document& d, const Assets& assets, const PdfOptions& o = {});

enum class Format { Pdf, Html };
/// .pdf, .html or .htm (any case); ReportError for another extension.
Format format_of(const std::filesystem::path& file);
/// Renders by the file's extension and replaces the file atomically.
void write_report(const std::filesystem::path& file, const Document& d, const Assets& assets);

/// A structural check of a PDF file (tests, the sample check of 10.4):
/// "%PDF-1.x" header, "%%EOF" trailer, a startxref offset that points at
/// the cross-reference table, a trailer with /Root, and the page count
/// (page objects; the page tree's /Count must agree).
struct PdfCheck {
    bool ok = false;
    std::string error;
    std::string version;  ///< "1.3"
    int pages = 0;
};
PdfCheck check_pdf(const std::vector<std::uint8_t>& bytes);

}  // namespace yantra::report
