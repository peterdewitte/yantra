// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The report engine (docs/REPORT.md): PDF via libharu with the text in an
// embedded Inter subset (a name with accented and Greek letters is drawn
// with glyphs of the embedded font) and the numeric columns in JetBrains
// Mono (EXP-05); one self-contained HTML file with the Yantra favicon
// (BRD-06); the curtat.be logo and linked reference on the cover and in
// every page footer (BRD-02) next to the DEMO label (DEMO-07);
// reproducible bytes (fixed creation time); plan credits, and a
// licence-restricted plan drawn only from the user's cached unaltered
// copy (DEMO-12; a Fraunces sheet stands in for a restricted sheet, the
// MAK images are never used in tests).

#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>
#include <regex>

#include "yantra/core/building/model.h"
#include "yantra/core/export/table.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/project/project.h"
#include "yantra/core/report/document.h"
#include "yantra/core/report/figure.h"

namespace fs = std::filesystem;
namespace yr = yantra::report;
namespace yx = yantra::exports;
namespace pj = yantra::project;

namespace {

const fs::path kData = fs::path(YANTRA_SOURCE_DIR) / "data";
const fs::path kFraunces = kData / "demo/fraunces/fraunces_tavern_first_floor.png";
constexpr std::int64_t kCreated = 1793000000LL * 1000000000LL;  // 2026-10-26T07:33:20Z

// A name every report must print: accents, a ligature-prone pair, Greek.
const std::string kName = "Zo\xc3\xab \xc3\x85ngstr\xc3\xb6m, Caf\xc3\xa9 \xce\x94\xce\xbf\xce\xba\xce\xb9\xce\xbc\xce\xae";

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string text_of(const std::vector<std::uint8_t>& b) { return std::string(b.begin(), b.end()); }

std::vector<std::uint8_t> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// UTF-8 -> the code points (BMP only in these tests).
std::vector<unsigned> code_points(const std::string& s) {
    std::vector<unsigned> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(c);
            i += 1;
        } else if ((c >> 5) == 6) {
            out.push_back(((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu));
            i += 2;
        } else {
            out.push_back(((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                          (static_cast<unsigned char>(s[i + 2]) & 0x3Fu));
            i += 3;
        }
    }
    return out;
}

// libharu's UTF-8 text through a Type0/Identity-H font: a hex string of
// 2-byte character codes, which are the code points.
std::string cid_hex(const std::string& utf8) {
    std::string o;
    char b[8];
    for (unsigned cp : code_points(utf8)) {
        std::snprintf(b, sizeof b, "%04X", cp);
        o += b;
    }
    return o;
}

std::size_t count(const std::string& hay, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
    return n;
}

// The body of object `id` (between "id 0 obj" and "endobj").
std::string object(const std::string& pdf, int id) {
    const std::string head = "\n" + std::to_string(id) + " 0 obj";
    const auto a = pdf.find(head);
    REQUIRE(a != std::string::npos);
    const auto e = pdf.find("endobj", a);
    return pdf.substr(a + head.size(), e - a - head.size());
}

// The font dictionary of a subtype ("/CIDFontType2", "/Type0") for a
// font ("Inter-Regular"), "" when there is none.
std::string font_dict(const std::string& pdf, const std::string& subtype, const std::string& font) {
    static const std::regex obj(R"(\n(\d+) 0 obj)");
    for (auto it = std::sregex_iterator(pdf.begin(), pdf.end(), obj); it != std::sregex_iterator(); ++it) {
        const std::string body = object(pdf, std::stoi((*it)[1].str()));
        if (body.find("/Subtype " + subtype + "\n") != std::string::npos && body.find("+" + font + "\n") != std::string::npos)
            return body;
    }
    return {};
}

std::string cid_font(const std::string& pdf, const std::string& font) { return font_dict(pdf, "/CIDFontType2", font); }

int ref(const std::string& dict, const std::string& key) {
    std::smatch m;
    const std::regex r(key + R"(\s+(\d+) 0 R)");
    REQUIRE(std::regex_search(dict, m, r));
    return std::stoi(m[1].str());
}

yr::Assets assets() { return yr::find_assets({kData}); }

yx::Canvas swatch(int w, int h, std::uint32_t rgb) {
    yx::Canvas c(w, h);
    c.fill_rect(0, 0, w, h, yx::rgb(rgb));
    return c;
}

// A document with every kind of block.
yr::Document sample(bool demo) {
    yr::Document d;
    d.title = "Coverage report " + kName;
    d.subtitle = "Site: " + kName;
    d.cover.rows = {{"Project", kName}, {"Operator", "Test network"}, {"Date", "2026-10-27 08:53 UTC"}};
    if (demo) d.demo_label = "DEMO - simulated data";
    d.footer = "Yantra 0.0.0 - 2026-10-27 08:53 UTC";
    d.generator = "Yantra 0.0.0";
    d.created_utc_ns = kCreated;
    d.heading(1, "Ground floor", "floor-1");
    d.paragraph("Notes: " + kName + " measured the hall.");
    d.paragraph("Passed", yr::Tone::Pass);
    d.key_values({{{"Points", "3"}, {"Operator", kName}}});
    yr::TableBlock t;
    t.header = {"Label", "RSSI (dBm)", "C1 (dB)"};
    t.numeric = {false, true, true};
    t.rows = {{kName, "-87.5", "12.0"}, {"Room 2", "-101.0", "-3.5"}};
    t.row_tone = {yr::Tone::Pass, yr::Tone::Fail};
    d.table(t);
    d.image({swatch(320, 160, 0x2e7d32), "Ground floor", "Measured points", std::nullopt});
    yr::LegendBlock l;
    l.title = "RSSI";
    l.entries = {{0x2e7d32, ">= -85 dBm", "1"}, {0xc62828, "< -95 dBm", "1"}};
    d.legend(l);
    yr::ChartBlock c;
    c.title = "Histogram";
    c.x_label = "dBm";
    c.y_label = "points";
    c.x_min = -120;
    c.x_max = -40;
    c.y_min = 0;
    c.y_max = 2;
    c.x_tick = 20;
    c.y_tick = 1;
    c.x_tick_labels = {"-120", "-100", "-80", "-60", "-40"};
    c.y_tick_labels = {"0", "1", "2"};
    c.bars = {{-90, -85, 1, true}, {-105, -100, 1, false}};
    c.has_marker = true;
    c.marker_x = -95;
    c.marker_label = "threshold";
    c.summary = "2 points";
    d.chart(c);
    d.page_break();
    d.heading(1, "First floor", "floor-2");
    d.paragraph("No points.", yr::Tone::Note);
    return d;
}

// A project whose floor uses a reference source (as a MAK sheet is used)
// made from a copy of the Fraunces sheet, and the description of that
// sheet as a licence-restricted one.
struct RestrictedFixture {
    fs::path dir, cache;
    std::shared_ptr<pj::Project> p;
    std::string floor;
    yantra::demo::DemoSheet sheet;
    explicit RestrictedFixture(const std::string& name) {
        dir = scratch(name);
        cache = dir / "cache";
        fs::create_directories(cache);
        fs::copy_file(kFraunces, cache / "sheet.png");
        const auto bytes = read_bytes(cache / "sheet.png");
        const auto src = yantra::plan::open_plan_source(bytes, "sheet.png");
        const auto size = src->page_size(0);
        p = pj::Project::create(dir / "r.yantra", pj::ProjectInfo{"Restricted", "", "", ""});
        const pj::AddSourceResult added = p->add_source_reference(cache / "sheet.png");
        yantra::building::Floor f;
        f.name = "Ground floor";
        f.source = added.id;
        f.plan_width = size.width;
        f.plan_height = size.height;
        floor = p->add_floor(f);
        sheet.id = "sheet";
        sheet.file = "sheet.png";
        sheet.sha256 = p->source(added.id)->plan.sha256;
        sheet.restricted = true;
        sheet.size_bytes = bytes.size();
        sheet.media_type = "image/png";
        sheet.width = static_cast<int>(size.width);
        sheet.height = static_cast<int>(size.height);
        sheet.title = "Test sheet";
        sheet.licence = "CC BY-NC-ND 4.0";
        sheet.licence_url = "https://creativecommons.org/licenses/by-nc-nd/4.0/";
        sheet.item_url = "https://example.org/item";
        sheet.credit = {"Test collection", "A. Designer", "1930", "KI 1-2"};
    }
    yr::FigureOptions options() const {
        yr::FigureOptions o;
        o.image.max_plan_px = 300;
        o.image.font_file = kData / "fonts/Inter-Regular.ttf";
        o.image.legend = false;
        o.sheets = {sheet};
        o.sheet_cache = cache;
        return o;
    }
};

}  // namespace

TEST_CASE("EXP-05 the PDF is valid, starts with a cover and is reproducible") {
    const yr::Document d = sample(false);
    const auto a = assets();
    const auto pdf = yr::render_pdf(d, a);
    const yr::PdfCheck c = yr::check_pdf(pdf);
    INFO(c.error);
    CHECK(c.ok);
    CHECK(c.pages >= 3);  // cover, ground floor, first floor (page break)
    // The same document gives the same bytes: no clock, no random ID.
    CHECK(yr::render_pdf(d, a) == pdf);
    CHECK(yr::render_html(d, a) == yr::render_html(d, a));
    yr::PdfOptions plain;
    plain.compress = false;
    const std::string s = text_of(yr::render_pdf(d, a, plain));
    CHECK(s.find("/CreationDate (D:20261026073320Z") != std::string::npos);
    CHECK(s.find("/ID") == std::string::npos);
    // Without a creation time there is no date at all.
    yr::Document undated = d;
    undated.created_utc_ns = 0;
    CHECK(text_of(yr::render_pdf(undated, a, plain)).find("/CreationDate") == std::string::npos);
    // An empty document is the cover alone.
    yr::Document empty;
    empty.title = "Empty";
    CHECK(yr::check_pdf(yr::render_pdf(empty, a)).pages == 1);
}

TEST_CASE("EXP-05 a name with accented and Greek letters renders through the embedded Inter subset") {
    yr::PdfOptions plain;
    plain.compress = false;
    const std::string s = text_of(yr::render_pdf(sample(false), assets(), plain));
    // The text is drawn as character codes of the embedded Inter font ...
    CHECK(s.find("<" + cid_hex(kName) + "> Tj") != std::string::npos);
    const std::string font = cid_font(s, "Inter-Regular");
    REQUIRE_FALSE(font.empty());
    const std::string descriptor = object(s, ref(font, "/FontDescriptor"));
    const std::string file = object(s, ref(descriptor, "/FontFile2"));
    // ... embedded (FontFile2) as a subset: fewer bytes than the font file.
    std::smatch m;
    REQUIRE(std::regex_search(file, m, std::regex(R"(/Length1 (\d+))")));
    const auto embedded = std::stoull(m[1].str());
    CHECK(embedded > 1000);
    CHECK(embedded < fs::file_size(kData / "fonts/Inter-Regular.ttf"));
    // Every character of the name maps to a real glyph (not .notdef).
    const std::string map = object(s, ref(font, "/CIDToGIDMap"));
    const auto st = map.find("stream\r\n");
    REQUIRE(st != std::string::npos);
    const std::string gids = map.substr(st + 8);
    for (unsigned cp : code_points(kName)) {
        if (cp == ' ') continue;
        REQUIRE(2 * cp + 1 < gids.size());
        const unsigned gid = (static_cast<unsigned char>(gids[2 * cp]) << 8) | static_cast<unsigned char>(gids[2 * cp + 1]);
        INFO("U+" << std::hex << cp);
        CHECK(gid != 0);
    }
    // A ToUnicode map (the identity over the BMP), so the text can be
    // searched and copied (libharu 2.3.0's own map is a CID map).
    const std::string type0 = font_dict(s, "/Type0", "Inter-Regular");
    REQUIRE_FALSE(type0.empty());
    const std::string cmap = object(s, ref(type0, "/ToUnicode"));
    CHECK(cmap.find("/CMapType 2") != std::string::npos);
    CHECK(cmap.find("<0300> <03FF> <0300>") != std::string::npos);  // Greek
    CHECK(cmap.find("<0000> <00FF> <0000>") != std::string::npos);
    CHECK(cmap.find("begincidrange") == std::string::npos);
    // The bold face too; no base-14 font.
    CHECK_FALSE(cid_font(s, "Inter-SemiBold").empty());
    CHECK(s.find("/Helvetica") == std::string::npos);
    // Characters libharu cannot encode (beyond the BMP, controls) do not break the PDF.
    yr::Document odd;
    odd.title = std::string("Odd \x01 \xf0\x9f\x93\xa1 \xff end");
    odd.paragraph(odd.title);
    CHECK(yr::check_pdf(yr::render_pdf(odd, assets())).ok);
}

TEST_CASE("EXP-05 numeric table columns are set in the embedded JetBrains Mono") {
    yr::PdfOptions plain;
    plain.compress = false;
    const std::string s = text_of(yr::render_pdf(sample(false), assets(), plain));
    CHECK_FALSE(cid_font(s, "JetBrainsMono-Regular").empty());
    CHECK(s.find("<" + cid_hex("-87.5") + "> Tj") != std::string::npos);
    // Only when a table has a numeric column.
    yr::Document d;
    d.title = "No numbers";
    yr::TableBlock t;
    t.header = {"Name"};
    t.rows = {{"A"}};
    d.table(t);
    CHECK(cid_font(text_of(yr::render_pdf(d, assets(), plain)), "JetBrainsMono-Regular").empty());
}

TEST_CASE("BRD-02 DEMO-07 the curtat.be logo and reference are on the cover and every page footer, next to the DEMO label") {
    yr::PdfOptions plain;
    plain.compress = false;
    for (bool demo : {false, true}) {
        CAPTURE(demo);
        const auto bytes = yr::render_pdf(sample(demo), assets(), plain);
        const int pages = yr::check_pdf(bytes).pages;
        const std::string s = text_of(bytes);
        // The reference: once on the cover and in every footer.
        CHECK(count(s, "<" + cid_hex(yr::kReference) + "> Tj") == static_cast<std::size_t>(pages) + 1);
        // Linked: logo, reference and URL on the cover, the footer of every page.
        CHECK(count(s, "/URI (https:\\057\\057curtat.be)") == static_cast<std::size_t>(pages) + 3);
        // The logo images (cover 256 px, footer 64 px), each stored once.
        CHECK(count(s, "/Width 256") == 1);
        CHECK(count(s, "/Width 64") == 1);
        const std::size_t labels = count(s, "<" + cid_hex("DEMO - simulated data") + "> Tj");
        if (demo) {
            // The band at the top and the footer of every page, and the cover's box.
            CHECK(labels == 2 * static_cast<std::size_t>(pages) + 1);
            // The information as UTF-16 text strings (every character kept).
            CHECK(s.find("/Subject <FEFF" + cid_hex("DEMO - simulated data") + ">") != std::string::npos);
            CHECK(s.find("/Title <FEFF" + cid_hex("DEMO - simulated data - Coverage report " + kName) + ">") !=
                  std::string::npos);
        } else {
            CHECK(labels == 0);
            CHECK(s.find("/Subject") == std::string::npos);
        }
        // "Page n of N" on every page.
        CHECK(s.find("<" + cid_hex("Page 1 of " + std::to_string(pages)) + "> Tj") != std::string::npos);
        CHECK(s.find("<" + cid_hex("Page " + std::to_string(pages) + " of " + std::to_string(pages)) + "> Tj") !=
              std::string::npos);
    }
}

TEST_CASE("BRD-06 BRD-02 the HTML report is one self-contained file with the Yantra favicon and the curtat.be branding") {
    const yr::Assets a = assets();
    for (bool demo : {false, true}) {
        CAPTURE(demo);
        const std::string h = yr::render_html(sample(demo), a);
        CHECK(h.rfind("<!DOCTYPE html>", 0) == 0);
        // The favicon is the Yantra logo PNG, as supplied.
        const std::string fav = "<link rel=\"icon\" type=\"image/png\" sizes=\"32x32\" href=\"data:image/png;base64,";
        const auto f = h.find(fav);
        REQUIRE(f != std::string::npos);
        const std::vector<std::uint8_t> png = read_bytes(kData / "logo/png/yantra_logo_32.png");
        const auto end = h.find('"', f + fav.size());
        CHECK(h.substr(f + fav.size(), end - f - fav.size()).size() == (png.size() + 2) / 3 * 4);
        // The curtat.be logo (its SVG) and the linked reference on the cover and in the footer.
        CHECK(count(h, "src=\"data:image/svg+xml;base64,") == 2);
        CHECK(count(h, yr::kReference) == 2);
        CHECK(h.find("<header class=\"cover\">") < h.find("<main>"));
        CHECK(h.find("<footer class=\"page-footer\">") > h.find("</main>"));
        CHECK(count(h, "href=\"https://curtat.be\"") >= 4);
        // Self-contained: no external file or stylesheet.
        CHECK(h.find("src=\"http") == std::string::npos);
        CHECK(h.find("rel=\"stylesheet\"") == std::string::npos);
        CHECK(count(h, "<img src=\"data:image/png;base64,") == 1);  // the one picture of the sample
        // Text escaped, UTF-8 kept.
        CHECK(h.find(kName) != std::string::npos);
        CHECK(h.find("<td class=\"num\">-87.5</td>") != std::string::npos);
        if (demo) {
            CHECK(h.find("<div class=\"demo-banner\" role=\"note\">DEMO - simulated data</div>") != std::string::npos);
            CHECK(h.find("<meta name=\"yantra-label\" content=\"DEMO - simulated data\" />") != std::string::npos);
            CHECK(h.find("<title>DEMO - simulated data - ") != std::string::npos);
            CHECK(h.find("<span class=\"demo\">DEMO - simulated data</span>") != std::string::npos);
        } else {
            CHECK(h.find("DEMO") == std::string::npos);
        }
    }
}

TEST_CASE("EXP-05 report files: format by extension, atomic write, the assets from the data folders") {
    const fs::path dir = scratch("files");
    const yr::Document d = sample(true);
    const yr::Assets a = assets();
    yr::write_report(dir / "r.PDF", d, a);
    yr::write_report(dir / "r.html", d, a);
    CHECK(yr::check_pdf(read_bytes(dir / "r.PDF")).ok);
    CHECK(text_of(read_bytes(dir / "r.html")) == yr::render_html(d, a));
    CHECK(yr::format_of("x.htm") == yr::Format::Html);
    CHECK_THROWS_AS(yr::write_report(dir / "r.docx", d, a), yr::ReportError);
    CHECK_FALSE(fs::exists(dir / "r.docx"));
    // Assets: the first folder holding each file; a missing one is named.
    const fs::path partial = dir / "data";
    fs::create_directories(partial / "fonts");
    fs::copy_file(kData / "fonts/Inter-Regular.ttf", partial / "fonts/Inter-Regular.ttf");
    CHECK(yr::find_assets({partial, kData}).font_regular == partial / "fonts/Inter-Regular.ttf");
    CHECK(yr::find_assets({partial, kData}).font_mono == kData / "fonts/JetBrainsMono-Regular.ttf");
    try {
        (void)yr::find_assets({partial});
        FAIL("no error");
    } catch (const yr::ReportError& e) {
        CHECK(std::string(e.what()).find("fonts/JetBrainsMono-Regular.ttf") != std::string::npos);
    }
    // A missing font file is a ReportError, never a broken PDF.
    yr::Assets broken = a;
    broken.font_mono = dir / "missing.ttf";
    CHECK_THROWS_AS(yr::render_pdf(d, broken), yr::ReportError);
}

TEST_CASE("EXP-05 SOURCE_DATE_EPOCH fixes the creation time") {
    const char* old = std::getenv("SOURCE_DATE_EPOCH");
    const std::string keep = old ? old : "";
    ::unsetenv("SOURCE_DATE_EPOCH");
    CHECK(yr::creation_time(42) == 42);
    ::setenv("SOURCE_DATE_EPOCH", "1793000000", 1);
    CHECK(yr::creation_time(42) == kCreated);
    ::setenv("SOURCE_DATE_EPOCH", "soon", 1);
    CHECK(yr::creation_time(42) == 42);
    if (old) ::setenv("SOURCE_DATE_EPOCH", keep.c_str(), 1);
    else ::unsetenv("SOURCE_DATE_EPOCH");
}

TEST_CASE("DEMO-12 a licence-restricted plan is drawn only from the user's cached unaltered copy, with its credit and licence") {
    RestrictedFixture fx("restricted");
    const yx::Table table = yx::floor_table(*fx.p, fx.floor);

    // The cached copy is there and unchanged: drawn, with the credit.
    const yr::Figure shown = yr::floor_figure(*fx.p, table, fx.options());
    CHECK(shown.plan_drawn);
    REQUIRE(shown.block.credit.has_value());
    CHECK(shown.block.credit->credit == "Test collection; A. Designer, 1930, inv. KI 1-2");
    CHECK(shown.block.credit->licence == "CC BY-NC-ND 4.0");
    CHECK(shown.block.credit->restricted);
    yr::Document d;
    d.title = "Restricted";
    d.image(shown.block);
    d.image(shown.block);  // twice: listed once in the credits
    const std::string line = yr::credit_line(*shown.block.credit, d.words);
    CHECK(line == "Plan: Test sheet, Test collection; A. Designer, 1930, inv. KI 1-2. Licence CC BY-NC-ND 4.0 "
                  "(https://creativecommons.org/licenses/by-nc-nd/4.0/).");
    CHECK(d.plan_credits().size() == 1);
    const std::string h = yr::render_html(d, assets());
    CHECK(count(h, line) == 3);  // under both pictures and in the list
    CHECK(h.find("<h2>Plan credits</h2>") != std::string::npos);
    yr::PdfOptions plain;
    plain.compress = false;
    const std::string s = text_of(yr::render_pdf(d, assets(), plain));
    CHECK(s.find(cid_hex("Licence CC BY-NC-ND 4.0")) != std::string::npos);
    CHECK(s.find("<" + cid_hex("Plan credits") + "> Tj") != std::string::npos);

    // Without a cache folder, or not downloaded: no plan, no credit, a warning.
    yr::FigureOptions none = fx.options();
    none.sheet_cache.clear();
    const yr::Figure no_cache = yr::floor_figure(*fx.p, table, none);
    CHECK_FALSE(no_cache.plan_drawn);
    CHECK_FALSE(no_cache.block.credit.has_value());
    REQUIRE_FALSE(no_cache.warnings.empty());
    CHECK(no_cache.warnings.front().find("left out of the report") != std::string::npos);
    none.sheet_cache = fx.dir / "empty";
    CHECK_FALSE(yr::floor_figure(*fx.p, table, none).plan_drawn);
    // An unknown sheet (no description) is never drawn either.
    yr::FigureOptions unknown = fx.options();
    unknown.sheets.clear();
    CHECK_FALSE(yr::floor_figure(*fx.p, table, unknown).plan_drawn);

    // An altered copy is refused when the picture is made.
    {
        std::fstream f(fx.cache / "sheet.png", std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(-1, std::ios::end);
        f.put('\x5a');
    }
    const yr::Figure altered = yr::floor_figure(*fx.p, table, fx.options());
    CHECK_FALSE(altered.plan_drawn);
    CHECK_FALSE(altered.block.credit.has_value());

    // The floor image export never draws such a plan, even though the
    // project's path hint points at a copy.
    fs::copy_file(kFraunces, fx.cache / "sheet.png", fs::copy_options::overwrite_existing);
    yx::ImageOptions io = fx.options().image;
    CHECK(fx.p->read_source(fx.p->floor(fx.floor)->source).size() == fs::file_size(kFraunces));
    const yx::ImageResult img = yx::floor_image(*fx.p, table, io);
    CHECK_FALSE(img.plan_drawn);
    CHECK(img.restricted_plan);
    io.reference_file = fx.cache / "sheet.png";
    CHECK(yx::floor_image(*fx.p, table, io).plan_drawn);
}

TEST_CASE("DEMO-12 the shipped sheet descriptions give the MAK credit and licence; the Fraunces sheets need none") {
    const std::vector<yantra::demo::DemoSheet> sheets = yr::described_sheets({kData});
    std::size_t mak = 0, fraunces = 0;
    for (const auto& s : sheets) {
        const auto c = yr::sheet_credit(s);
        if (s.restricted) {
            ++mak;
            REQUIRE(c.has_value());
            CHECK(c->credit.rfind("MAK - Museum of Applied Arts, Vienna; ", 0) == 0);
            CHECK(c->credit.find("inv. KI ") != std::string::npos);
            CHECK(c->licence == "CC BY-NC-ND 4.0");
            CHECK(c->restricted);
        } else {
            ++fraunces;
            CHECK_FALSE(c.has_value());
        }
    }
    CHECK(mak == 3);
    CHECK(fraunces == 2);
}

TEST_CASE("EXP-05 a bundled (embedded) plan is drawn as in the project") {
    const fs::path dir = scratch("embedded");
    auto p = pj::Project::create(dir / "e.yantra", pj::ProjectInfo{"Embedded", "", "", ""});
    const auto added = p->add_source(kFraunces);
    const auto bytes = read_bytes(kFraunces);
    const auto size = yantra::plan::open_plan_source(bytes, "f.png")->page_size(0);
    yantra::building::Floor f;
    f.name = "First floor";
    f.source = added.id;
    f.plan_width = size.width;
    f.plan_height = size.height;
    const std::string floor = p->add_floor(f);
    yr::FigureOptions o;
    o.image.max_plan_px = 300;
    o.image.legend = false;
    o.sheets = yr::described_sheets({kData});
    const yr::Figure fig = yr::floor_figure(*p, yx::floor_table(*p, floor), o);
    CHECK(fig.plan_drawn);
    CHECK_FALSE(fig.block.credit.has_value());  // public domain, no credit required
    CHECK(fig.block.image.width() > 300);
}
