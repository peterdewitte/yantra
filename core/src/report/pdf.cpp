// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PDF back end of the report (EXP-05; docs/REPORT.md) with libharu (spec
// 16.2): A4 portrait. The text is set in Inter and the numeric table
// columns in JetBrains Mono, both TrueType fonts embedded by libharu as a
// subset (only the glyphs used are kept) with a ToUnicode map, through
// its UTF-8 encoder, so every character of names and notes prints. Page 1
// is the cover: the curtat.be logo and reference with its link (BRD-02),
// the title, the DEMO label (DEMO-07) and the cover details. Every page
// has the title (and the red DEMO band in demo mode) at the top and, in
// the footer, the curtat.be logo, "Made with Yantra - curtat.be" linked
// to https://curtat.be, the DEMO label and "Page n of N". The output
// depends only on the document (no clock, no random ID).
// Copied from yantra-v1 c24e8f5: core/report/pdf.cpp, adapted for EXP-05
// (embedded fonts instead of the base-14 fonts, cover, branding, plan
// credits).

#include <hpdf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "report_internal.h"
#include "yantra/core/report/document.h"
#include "yantra/core/platform/time.h"

namespace yantra::report {

namespace {

// The ToUnicode map of a Type0 font of libharu's UTF-8 encoder: its
// character codes are the Unicode code points (Identity-H), so the map is
// the identity over the BMP without the surrogates, in 256-code ranges
// (bfrange).
std::string identity_cmap() {
    std::string t =
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    std::vector<unsigned> blocks;
    for (unsigned hi = 0; hi < 256; ++hi)
        if (hi < 0xD8 || hi > 0xDF) blocks.push_back(hi);
    for (std::size_t i = 0; i < blocks.size(); i += 100) {
        const std::size_t n = std::min<std::size_t>(100, blocks.size() - i);
        t += std::to_string(n) + " beginbfrange\n";
        char b[40];
        for (std::size_t k = i; k < i + n; ++k) {
            std::snprintf(b, sizeof b, "<%02X00> <%02XFF> <%02X00>\n", blocks[k], blocks[k], blocks[k]);
            t += b;
        }
        t += "endbfrange\n";
    }
    t += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return t;
}

#if defined(_WIN32)
// The MSYS2 libharu DLL exports only the public API, so the font's
// ToUnicode stream cannot be replaced while the document is built (as
// on Linux). The saved PDF is patched instead: every object a
// /ToUnicode entry names gets identity_cmap(), then the cross-reference
// table and startxref are written again (libharu writes a classic table
// of 20-byte entries).
void patch_to_unicode(std::vector<std::uint8_t>& pdf) {
    std::string s(pdf.begin(), pdf.end());
    std::vector<long> targets;
    for (std::size_t at = s.find("/ToUnicode "); at != std::string::npos; at = s.find("/ToUnicode ", at + 1)) {
        long n = 0;
        int gen = -1;
        if (std::sscanf(s.c_str() + at + 11, "%ld %d R", &n, &gen) == 2 && gen == 0) targets.push_back(n);
    }
    if (targets.empty()) return;
    const std::string cmap = identity_cmap();
    for (const long n : targets) {
        const std::string head = "\n" + std::to_string(n) + " 0 obj";
        const std::size_t start = s.find(head);
        if (start == std::string::npos) continue;
        const std::size_t stream_end = s.find("endstream", start);
        if (stream_end == std::string::npos) continue;
        const std::size_t end = s.find("endobj", stream_end);
        if (end == std::string::npos) continue;
        const std::string obj = std::to_string(n) + " 0 obj\n<</Length " + std::to_string(cmap.size()) +
                                ">>\nstream\n" + cmap + "\nendstream\n";
        s.replace(start + 1, end - start - 1, obj);
    }
    // The cross-reference table: every in-use entry gets its object's new offset.
    const std::size_t xref = s.rfind("\nxref\n");
    if (xref == std::string::npos) return;
    long first = 0, count = 0;
    std::size_t line = xref + 6;
    if (std::sscanf(s.c_str() + line, "%ld %ld", &first, &count) != 2) return;
    std::size_t entry = s.find('\n', line) + 1;
    for (long k = 0; k < count; ++k, entry += 20) {
        if (entry + 20 > s.size()) return;
        if (s[entry + 17] != 'n') continue;
        const std::string head = "\n" + std::to_string(first + k) + " 0 obj";
        const std::size_t at = s.find(head);
        if (at == std::string::npos || at + 1 > xref) continue;
        char digits[16];
        std::snprintf(digits, sizeof digits, "%010zu", at + 1);
        s.replace(entry, 10, digits);
    }
    const std::size_t sx = s.rfind("startxref");
    if (sx != std::string::npos) {
        const std::size_t num = s.find('\n', sx) + 1;
        const std::size_t num_end = s.find('\n', num);
        s.replace(num, num_end - num, std::to_string(xref + 1));
    }
    pdf.assign(s.begin(), s.end());
}
#endif


using detail::clean_utf8;

struct Rgb {
    float r, g, b;
};
constexpr Rgb kText{0.13f, 0.13f, 0.13f}, kGrey{0.4f, 0.4f, 0.4f}, kRule{0.8f, 0.8f, 0.8f};
constexpr Rgb kDemo{0.718f, 0.11f, 0.11f}, kPassBg{0.91f, 0.96f, 0.91f}, kFailBg{0.99f, 0.925f, 0.918f};
constexpr Rgb kGood{0.18f, 0.49f, 0.196f}, kBad{0.776f, 0.157f, 0.157f}, kLine{0.082f, 0.396f, 0.753f};
constexpr Rgb kLink{0.08f, 0.33f, 0.62f};

Rgb rgb_of(std::uint32_t v) {
    return {static_cast<float>((v >> 16) & 0xFF) / 255.f, static_cast<float>((v >> 8) & 0xFF) / 255.f,
            static_cast<float>(v & 0xFF) / 255.f};
}

enum class Face { Regular, Bold, Mono };

class Writer {
public:
    Writer(const Document& d, const Assets& a, const PdfOptions& o) : d_(d), a_(a) {
        pdf_ = HPDF_New(&Writer::on_error, this);
        if (!pdf_) return;
        HPDF_SetCompressionMode(pdf_, o.compress ? HPDF_COMP_ALL : HPDF_COMP_NONE);
        compress_ = o.compress;
        issues_ = o.layout_issues;
        HPDF_UseUTFEncodings(pdf_);
        HPDF_SetCurrentEncoder(pdf_, "UTF-8");
    }
    ~Writer() {
        if (pdf_) HPDF_Free(pdf_);
    }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    std::vector<std::uint8_t> run() {
        if (!pdf_) throw ReportError("libharu: cannot create a document");
        check();
        regular_ = load_font(a_.font_regular);
        bold_ = load_font(a_.font_bold);
        logo_ = image_of(detail::load_png(a_.curtat_logo));
        logo_small_ = image_of(detail::load_png(a_.curtat_logo_small));
        info();
        cover();
        if (!d_.blocks.empty()) new_page();
        for (std::size_t i = 0; i < d_.blocks.size(); ++i) {
            const Block& b = d_.blocks[i];
            // An image keeps its legend on the same page.
            const Block* next = i + 1 < d_.blocks.size() ? &d_.blocks[i + 1] : nullptr;
            float keep = next && next->type == Block::Type::Legend ? legend_height(next->legend) : 0;
            // A heading stays with the image (and its legend) that follows.
            if (b.type == Block::Type::Heading && next && next->type == Block::Type::Image) {
                const Block* after = i + 2 < d_.blocks.size() ? &d_.blocks[i + 2] : nullptr;
                keep = image_height(next->image) + 36 +
                       (after && after->type == Block::Type::Legend ? legend_height(after->legend) : 0);
            }
            switch (b.type) {
            case Block::Type::Heading: heading(b.heading, keep); break;
            case Block::Type::Paragraph: paragraph(b.paragraph); break;
            case Block::Type::KeyValues: key_values(b.kv); break;
            case Block::Type::Table: table(b.table); break;
            case Block::Type::Image: image(b.image, keep); break;
            case Block::Type::Legend: legend(b.legend); break;
            case Block::Type::Chart: chart(b.chart); break;
            case Block::Type::PageBreak: new_page(); break;
            }
            check();
        }
        credits();
        footers();
        overlaps();
        check();
        std::vector<std::uint8_t> out;
        if (HPDF_SaveToStream(pdf_) == HPDF_OK) {
            HPDF_UINT32 size = HPDF_GetStreamSize(pdf_);
            out.resize(size);
            HPDF_ResetStream(pdf_);
            if (HPDF_ReadFromStream(pdf_, out.data(), &size) != HPDF_OK && size == 0) out.clear();
            out.resize(size);
        }
        check();
        if (out.empty()) throw ReportError("libharu: saving the PDF failed");
#if defined(_WIN32)
        patch_to_unicode(out);
#endif
        return out;
    }

private:
    static void on_error(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user) {
        auto* w = static_cast<Writer*>(user);
        if (!w->failed_) {
            char b[96];
            std::snprintf(b, sizeof b, "libharu error 0x%04X (detail %u)", static_cast<unsigned>(error_no),
                          static_cast<unsigned>(detail_no));
            w->error_ = b;
        }
        w->failed_ = true;
    }
    void check() const {
        if (failed_) throw ReportError(error_);
    }

    HPDF_Font load_font(const std::filesystem::path& p) {
        // Read here first, so a missing file is named (libharu only reports a code).
        (void)detail::read_file(p);
        const char* name = HPDF_LoadTTFontFromFile(pdf_, p.string().c_str(), HPDF_TRUE);
        if (name == nullptr || failed_) throw ReportError("cannot load the font " + p.string() + ": " + error_);
        HPDF_Font f = HPDF_GetFont(pdf_, name, "UTF-8");
        if (f != nullptr && !failed_) to_unicode(f);
        return f;
    }

    // libharu 2.3.0 gives a Type0 font of its UTF-8 encoder a ToUnicode
    // map that is a CID map (every code to 0), so viewers cannot copy or
    // search the text. The character codes are the Unicode code points
    // (Identity-H), so the correct map is the identity over the BMP
    // without the surrogates, in 256-code ranges (bfrange).
    void to_unicode(HPDF_Font font) {
#if defined(_WIN32)
        // The MSYS2 libharu DLL exports only the public API (no
        // HPDF_DictStream_New/HPDF_Dict_Add): patch_to_unicode() replaces
        // the map in the saved file.
        (void)font;
#else
        HPDF_Dict cmap = HPDF_DictStream_New(pdf_->mmgr, pdf_->xref);
        if (cmap == nullptr) return;
        if (compress_) cmap->filter = HPDF_STREAM_FILTER_FLATE_DECODE;
        const std::string t = identity_cmap();
        HPDF_Stream_WriteStr(cmap->stream, t.c_str());
        HPDF_Dict_Add(font, "ToUnicode", cmap);
#endif
    }
    HPDF_Font font(Face f) {
        if (f == Face::Mono && mono_ == nullptr) mono_ = load_font(a_.font_mono);  // only when a table needs it
        return f == Face::Bold ? bold_ : f == Face::Mono ? mono_ : regular_;
    }

    // An RGBA canvas composited on white as an RGB image.
    HPDF_Image image_of(const exports::Canvas& c) {
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(c.width()) * static_cast<std::size_t>(c.height()) * 3);
        const auto& px = c.pixels();
        for (std::size_t i = 0, n = rgb.size() / 3; i < n; ++i) {
            const unsigned al = px[i * 4 + 3];
            for (std::size_t k = 0; k < 3; ++k)
                rgb[i * 3 + k] = static_cast<std::uint8_t>((px[i * 4 + k] * al + 255 * (255 - al) + 127) / 255);
        }
        HPDF_Image img = HPDF_LoadRawImageFromMem(pdf_, rgb.data(), static_cast<HPDF_UINT>(c.width()),
                                                  static_cast<HPDF_UINT>(c.height()), HPDF_CS_DEVICE_RGB, 8);
        check();
        return img;
    }

    // Page geometry (points).
    static constexpr float kW = 595.276f, kH = 841.89f, kL = 50, kR = 50;
    static constexpr float kTop = kH - 72, kBottom = 64;
    static float width() { return kW - kL - kR; }

    void info() {
        const bool demo = !d_.demo_label.empty();
        // With the UTF-8 encoder libharu writes the information as UTF-16
        // text strings, so names keep every character.
        auto info_text = [](const std::string& t) { return clean_utf8(t); };
        HPDF_SetInfoAttr(pdf_, HPDF_INFO_TITLE, info_text(demo ? d_.demo_label + " - " + d_.title : d_.title).c_str());
        if (!d_.generator.empty()) HPDF_SetInfoAttr(pdf_, HPDF_INFO_CREATOR, info_text(d_.generator).c_str());
        if (demo) {
            HPDF_SetInfoAttr(pdf_, HPDF_INFO_SUBJECT, info_text(d_.demo_label).c_str());
            HPDF_SetInfoAttr(pdf_, HPDF_INFO_KEYWORDS, info_text(d_.demo_label).c_str());
        }
        if (d_.created_utc_ns > 0) {
            const auto t = static_cast<std::time_t>(d_.created_utc_ns / 1000000000);
            const std::tm tm = platform::utc_tm(t);
            HPDF_Date dt{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, 'Z', 0, 0};
            HPDF_SetInfoDateAttr(pdf_, HPDF_INFO_CREATION_DATE, dt);
        }
    }

    void fill_colour(Rgb c) { HPDF_Page_SetRGBFill(page_, c.r, c.g, c.b); }
    void stroke_colour(Rgb c) { HPDF_Page_SetRGBStroke(page_, c.r, c.g, c.b); }

    float text_w(const std::string& t, Face f, float size) {
        HPDF_Page_SetFontAndSize(page_, font(f), size);
        return HPDF_Page_TextWidth(page_, t.c_str());
    }

    // TST-52: the box the next texts must stay in (x0..x1), see PdfOptions.
    struct Box {
        Writer& w;
        float x0, x1;
        Box(Writer& wr, float a, float b) : w(wr), x0(wr.box0_), x1(wr.box1_) {
            w.box0_ = a;
            w.box1_ = b;
        }
        ~Box() {
            w.box0_ = x0;
            w.box1_ = x1;
        }
        Box(const Box&) = delete;
        Box& operator=(const Box&) = delete;
    };
    void issue(const std::string& what) {
        if (issues_ != nullptr) issues_->push_back("page " + std::to_string(page_no_) + ": " + what);
    }
    static std::string pt(float v) {
        char b[32];
        std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
        return b;
    }

    void text(float x, float y, const std::string& t, Face f, float size, Rgb c) {
        if (t.empty()) return;
        if (issues_ != nullptr) {
            const float w = text_w(t, f, size);
            const float x1 = x + w;
            if (x < box0_ - 0.5f || x1 > box1_ + 0.5f)
                issue("\"" + t + "\" runs past its box by " + pt(std::max(box0_ - x, x1 - box1_)) + " pt");
            if (x < 0 || x1 > kW || y - 0.25f * size < 0 || y + size > kH) issue("\"" + t + "\" runs off the page");
            placed_.push_back({page_no_, x, y - 0.2f * size, x1, y + 0.75f * size, t});
        }
        fill_colour(c);
        HPDF_Page_BeginText(page_);
        HPDF_Page_SetFontAndSize(page_, font(f), size);
        HPDF_Page_TextOut(page_, x, y, t.c_str());
        HPDF_Page_EndText(page_);
    }

    void link(float x, float y, float w, float h, const char* uri) {
        const HPDF_Rect r{x, y, x + w, y + h};
        HPDF_Annotation an = HPDF_Page_CreateURILinkAnnot(page_, r, uri);
        if (an) HPDF_LinkAnnot_SetBorderStyle(an, 0, 0, 0);
    }

    void rect(float x, float y, float w, float h, Rgb c) {
        fill_colour(c);
        HPDF_Page_Rectangle(page_, x, y, w, h);
        HPDF_Page_Fill(page_);
    }

    void line(float x0, float y0, float x1, float y1, float w, Rgb c) {
        stroke_colour(c);
        HPDF_Page_SetLineWidth(page_, w);
        HPDF_Page_MoveTo(page_, x0, y0);
        HPDF_Page_LineTo(page_, x1, y1);
        HPDF_Page_Stroke(page_);
    }

    // Greedy word wrap into lines of at most `w` points; a word wider
    // than a line is cut between characters (never inside one).
    // wrap() of each line of a multi-line text (IND-47 notes).
    std::vector<std::string> wrap_lines(const std::string& raw, float w, Face f, float size) {
        std::vector<std::string> out;
        std::size_t start = 0;
        for (;;) {
            const std::size_t nl = raw.find('\n', start);
            const auto part = wrap(raw.substr(start, nl == std::string::npos ? std::string::npos : nl - start), w, f, size);
            out.insert(out.end(), part.begin(), part.end());
            if (nl == std::string::npos) return out;
            start = nl + 1;
        }
    }

    std::vector<std::string> wrap(const std::string& raw, float w, Face f, float size) {
        const std::string t = clean_utf8(raw);
        std::vector<std::string> lines;
        std::string cur;
        std::size_t i = 0;
        while (i <= t.size()) {
            std::size_t j = t.find(' ', i);
            if (j == std::string::npos) j = t.size();
            const std::string word = t.substr(i, j - i);
            const std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && text_w(trial, f, size) > w) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = trial;
            }
            while (cur.size() > 1 && text_w(cur, f, size) > w) {
                std::size_t k = cur.size() - 1;
                for (;;) {
                    while (k > 1 && (static_cast<unsigned char>(cur[k]) & 0xC0) == 0x80) --k;
                    if (k <= 1 || text_w(cur.substr(0, k), f, size) <= w) break;
                    --k;
                }
                if (k == 0) k = 1;
                if (issues_ != nullptr)
                    issue("\"" + cur + "\" is too wide for its line of " + pt(w) + " pt: cut between characters");
                lines.push_back(cur.substr(0, k));
                cur = cur.substr(k);
            }
            i = j + 1;
        }
        if (!cur.empty() || lines.empty()) lines.push_back(cur);
        return lines;
    }

    void add_page() {
        page_ = HPDF_AddPage(pdf_);
        HPDF_Page_SetSize(page_, HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);
        pages_.push_back(page_);
        page_no_ = pages_.size();
    }

    float demo_band() {
        if (d_.demo_label.empty()) return kH;
        rect(0, kH - 24, kW, 24, kDemo);
        const Box band(*this, 0, kW);
        const std::string l = clean_utf8(d_.demo_label);
        text((kW - text_w(l, Face::Bold, 11)) / 2, kH - 16, l, Face::Bold, 11, {1, 1, 1});
        return kH - 24;
    }

    void new_page() {
        add_page();
        const float y = demo_band();
        text(kL, y - 22, clean_utf8(d_.title), Face::Regular, 8.5f, kGrey);
        line(kL, y - 28, kW - kR, y - 28, 0.5f, kRule);
        y_ = kTop;
    }

    void ensure(float h) {
        if (y_ - h < kBottom && y_ < kTop) new_page();  // never leave a fresh page empty
    }

    // Page 1: logo and reference, title, DEMO label, details.
    void cover() {
        add_page();
        float y = demo_band() - 40;
        const float logo = 64;
        HPDF_Page_DrawImage(page_, logo_, kL, y - logo, logo, logo);
        link(kL, y - logo, logo, logo, kReferenceUrl);
        const std::string ref = kReference;
        text(kL + logo + 14, y - 26, ref, Face::Bold, 13, kText);
        link(kL + logo + 14, y - 30, text_w(ref, Face::Bold, 13), 17, kReferenceUrl);
        text(kL + logo + 14, y - 44, kReferenceUrl, Face::Regular, 10, kLink);
        link(kL + logo + 14, y - 47, text_w(kReferenceUrl, Face::Regular, 10), 13, kReferenceUrl);
        y -= logo + 70;
        for (const auto& l : wrap(d_.title, width(), Face::Bold, 24)) {
            text(kL, y - 24, l, Face::Bold, 24, kText);
            y -= 30;
        }
        if (!d_.subtitle.empty()) {
            y -= 4;
            for (const auto& l : wrap(d_.subtitle, width(), Face::Regular, 13)) {
                text(kL, y - 13, l, Face::Regular, 13, kGrey);
                y -= 18;
            }
        }
        y -= 16;
        if (!d_.demo_label.empty()) {
            const auto lines = wrap(d_.demo_label, width() - 24, Face::Bold, 16);
            const float h = 22 * static_cast<float>(lines.size()) + 16;
            rect(kL, y - h, width(), h, kDemo);
            float ty = y - 8;
            for (const auto& l : lines) {
                text(kL + (width() - text_w(l, Face::Bold, 16)) / 2, ty - 16, l, Face::Bold, 16, {1, 1, 1});
                ty -= 22;
            }
            y -= h + 20;
        }
        y_ = y;
        if (!d_.cover.rows.empty()) key_values(d_.cover);
    }

    void heading(const Heading& h, float keep) {
        const bool top = h.level <= 1;
        ensure(std::max(top ? 90.f : 60.f, keep + 40));  // keep a heading with (some of) its content
        const float size = top ? 14 : 11;
        y_ -= top ? 12 : 6;
        for (const auto& l : wrap(h.text, width(), Face::Bold, size)) {
            text(kL, y_ - size, l, Face::Bold, size, kText);
            y_ -= size + 4;
        }
        if (top) {
            line(kL, y_, kW - kR, y_, 0.6f, kRule);
            y_ -= 8;
        } else {
            y_ -= 3;
        }
    }

    void paragraph(const Paragraph& p) {
        const float size = 9.5f, lead = 13;
        const bool boxed = p.tone == Tone::Pass || p.tone == Tone::Fail || p.tone == Tone::Demo;
        const float inset = boxed ? 8 : 0;
        const Face f = boxed ? Face::Bold : Face::Regular;
        const auto lines = wrap(p.text, width() - 2 * inset, f, size);
        const float h = lead * static_cast<float>(lines.size()) + (boxed ? 8 : 0);
        ensure(h + 4);
        if (boxed) {
            const Rgb bg = p.tone == Tone::Pass ? kPassBg : kFailBg;
            const Rgb bar = p.tone == Tone::Pass ? kGood : p.tone == Tone::Demo ? kDemo : kBad;
            rect(kL, y_ - h, width(), h, bg);
            rect(kL, y_ - h, 3, h, bar);
            y_ -= 4;
        }
        const Rgb c = p.tone == Tone::Note ? kGrey : kText;
        for (const auto& l : lines) {
            text(kL + inset, y_ - 10, l, f, size, c);
            y_ -= lead;
        }
        y_ -= boxed ? 10 : 5;
    }

    void key_values(const KeyValues& kv) {
        const float size = 9, lead = 12, kw = 150;
        for (const auto& [k, v] : kv.rows) {
            const auto kl = wrap(k, kw - 8, Face::Bold, size);
            const auto vl = wrap_lines(v, width() - kw, Face::Regular, size);
            const float h = lead * static_cast<float>(std::max(kl.size(), vl.size())) + 4;
            ensure(h);
            float y = y_;
            const Box key(*this, kL, kL + kw - 8);
            for (const auto& l : kl) {
                text(kL, y - 10, l, Face::Bold, size, kText);
                y -= lead;
            }
            y = y_;
            const Box value(*this, kL + kw, kW - kR);
            for (const auto& l : vl) {
                text(kL + kw, y - 10, l, Face::Regular, size, kText);
                y -= lead;
            }
            y_ -= h;
            line(kL, y_ + 1, kW - kR, y_ + 1, 0.3f, kRule);
        }
        y_ -= 8;
    }

    void table(const TableBlock& t) {
        const std::size_t n = t.header.size();
        if (n == 0) return;
        std::vector<float> w(n, width() / static_cast<float>(n));
        if (t.widths.size() == n) {
            double sum = 0;
            for (double x : t.widths) sum += x;
            if (sum > 0)
                for (std::size_t j = 0; j < n; ++j) w[j] = static_cast<float>(t.widths[j] / sum) * width();
        }
        const float size = 8, mono_size = 7.5f, lead = 10, pad = 3;
        auto num = [&](std::size_t j) { return j < t.numeric.size() && t.numeric[j]; };
        auto face = [&](std::size_t j, bool head) { return head ? Face::Bold : num(j) ? Face::Mono : Face::Regular; };
        auto fsize = [&](std::size_t j, bool head) { return !head && num(j) ? mono_size : size; };
        using Lines = std::vector<std::vector<std::string>>;
        auto row_h = [&](const std::vector<std::string>& cells, bool head, Lines& lines) {
            lines.clear();
            std::size_t m = 1;
            for (std::size_t j = 0; j < n; ++j) {
                lines.push_back(wrap(j < cells.size() ? cells[j] : "", w[j] - 2 * pad, face(j, head), fsize(j, head)));
                m = std::max(m, lines.back().size());
            }
            return lead * static_cast<float>(m) + 4;
        };
        auto draw_row = [&](const Lines& lines, float h, bool head, const Rgb* bg) {
            if (bg) rect(kL, y_ - h, width(), h, *bg);
            float x = kL;
            for (std::size_t j = 0; j < n; ++j) {
                float y = y_;
                const Box cell(*this, x + pad, x + w[j] - pad);
                for (const auto& l : lines[j]) {
                    const Face f = face(j, head);
                    const float s = fsize(j, head);
                    const float tx = num(j) ? x + w[j] - pad - text_w(l, f, s) : x + pad;
                    text(tx, y - 9.5f, l, f, s, kText);
                    y -= lead;
                }
                x += w[j];
            }
            y_ -= h;
            line(kL, y_, kW - kR, y_, 0.3f, kRule);
        };
        Lines hl, rl;
        const float hh = row_h(t.header, true, hl);
        const Rgb head_bg{0.95f, 0.95f, 0.95f};
        ensure(hh + 2 * lead + 4);
        draw_row(hl, hh, true, &head_bg);
        for (std::size_t i = 0; i < t.rows.size(); ++i) {
            const float h = row_h(t.rows[i], false, rl);
            if (y_ - h < kBottom) {
                new_page();
                draw_row(hl, hh, true, &head_bg);
            }
            const Tone tone = i < t.row_tone.size() ? t.row_tone[i] : Tone::Normal;
            const Rgb* bg = tone == Tone::Pass ? &kPassBg : tone == Tone::Fail ? &kFailBg : nullptr;
            draw_row(rl, h, false, bg);
        }
        y_ -= 10;
    }

    static float legend_height(const LegendBlock& lb) {
        return 26 + 13 * static_cast<float>((lb.entries.size() + 1) / 2);
    }

    // Height of an image scaled to the text width (at most one page).
    static float image_height(const ImageBlock& ib) {
        if (ib.image.empty()) return 0;
        const float h = width() * static_cast<float>(ib.image.height()) / static_cast<float>(ib.image.width());
        return std::min(h, kTop - kBottom - 60);
    }

    void image(const ImageBlock& ib, float keep) {
        const exports::Canvas& c = ib.image;
        if (c.empty()) return;
        HPDF_Image img = image_of(c);
        float w = width();
        float h = w * static_cast<float>(c.height()) / static_cast<float>(c.width());
        const float max_h = kTop - kBottom - 60;
        if (h > max_h) {
            w *= max_h / h;
            h = max_h;
        }
        ensure(h + 36 + keep);
        HPDF_Page_DrawImage(page_, img, kL + (width() - w) / 2, y_ - h, w, h);
        stroke_colour(kRule);
        HPDF_Page_SetLineWidth(page_, 0.5f);
        HPDF_Page_Rectangle(page_, kL + (width() - w) / 2, y_ - h, w, h);
        HPDF_Page_Stroke(page_);
        y_ -= h + 4;
        auto small = [&](const std::string& s) {
            for (const auto& l : wrap(s, width(), Face::Regular, 8)) {
                text(kL, y_ - 9, l, Face::Regular, 8, kGrey);
                y_ -= 10;
            }
        };
        if (!ib.caption.empty()) small(ib.caption);
        if (ib.credit) small(credit_line(*ib.credit, d_.words));
        y_ -= 8;
    }

    void legend(const LegendBlock& lb) {
        const float size = 9, lead = 13;
        const std::size_t rows = (lb.entries.size() + 1) / 2;
        ensure(legend_height(lb));
        text(kL, y_ - 11, clean_utf8(lb.title), Face::Bold, 10, kText);
        y_ -= 18;
        const float top = y_;
        for (std::size_t i = 0; i < lb.entries.size(); ++i) {
            const auto& e = lb.entries[i];
            const float x = kL + (i < rows ? 0 : width() / 2);
            const float y = top - lead * static_cast<float>(i < rows ? i : i - rows);
            const Box column(*this, x + 14, i < rows ? kL + width() / 2 - 6 : kW - kR);
            rect(x, y - 10, 9, 9, rgb_of(e.rgb));
            stroke_colour(kGrey);
            HPDF_Page_SetLineWidth(page_, 0.4f);
            HPDF_Page_Rectangle(page_, x, y - 10, 9, 9);
            HPDF_Page_Stroke(page_);
            text(x + 14, y - 9, clean_utf8(e.label + (e.count.empty() ? "" : " (" + e.count + ")")), Face::Regular, size,
                 kText);
        }
        y_ = top - lead * static_cast<float>(rows) - 8;
    }

    void chart(const ChartBlock& c) {
        const float H = 215, L = 48, R = 10, T = 20, B = 34;
        const float pw = width() - L - R, ph = H - T - B;
        ensure(H + 30);
        const float ox = kL + L, oy = y_ - T - ph;  // plot origin (bottom left)
        const double xs = c.x_max > c.x_min ? c.x_max - c.x_min : 1, ys = c.y_max > c.y_min ? c.y_max - c.y_min : 1;
        auto X = [&](double x) { return ox + static_cast<float>((x - c.x_min) / xs) * pw; };
        auto Y = [&](double y) { return oy + static_cast<float>((y - c.y_min) / ys) * ph; };
        text(kL + L, y_ - 12, clean_utf8(c.title), Face::Bold, 10, kText);
        for (std::size_t k = 0; k < c.y_tick_labels.size(); ++k) {
            const float y = Y(c.y_min + static_cast<double>(k) * c.y_tick);
            line(ox, y, ox + pw, y, 0.3f, {0.89f, 0.89f, 0.89f});
            const std::string l = clean_utf8(c.y_tick_labels[k]);
            const Box axis(*this, kL + 12, ox);
            text(ox - 4 - text_w(l, Face::Regular, 7.5f), y - 2.5f, l, Face::Regular, 7.5f, kText);
        }
        for (std::size_t k = 0; k < c.x_tick_labels.size(); ++k) {
            const float x = X(c.x_min + static_cast<double>(k) * c.x_tick);
            line(x, oy, x, oy - 3, 0.5f, kGrey);
            const std::string l = clean_utf8(c.x_tick_labels[k]);
            text(x - text_w(l, Face::Regular, 7.5f) / 2, oy - 12, l, Face::Regular, 7.5f, kText);
        }
        if (c.kind == ChartBlock::Kind::Bars) {
            for (const auto& b : c.bars) {
                if (b.y <= 0) continue;
                const float x0 = X(b.x0) + 0.4f, x1 = X(b.x1) - 0.4f;
                rect(x0, oy, std::max(0.4f, x1 - x0), Y(b.y) - oy, b.good ? kGood : kBad);
            }
        } else if (!c.steps.empty()) {
            stroke_colour(kLine);
            HPDF_Page_SetLineWidth(page_, 1.4f);
            HPDF_Page_MoveTo(page_, X(c.steps[0].first), Y(c.steps[0].second));
            for (std::size_t i = 1; i < c.steps.size(); ++i) {
                HPDF_Page_LineTo(page_, X(c.steps[i].first), Y(c.steps[i - 1].second));
                HPDF_Page_LineTo(page_, X(c.steps[i].first), Y(c.steps[i].second));
            }
            HPDF_Page_Stroke(page_);
        }
        if (c.has_marker && c.marker_x >= c.x_min && c.marker_x <= c.x_max) {
            const float x = X(c.marker_x);
            // libharu 2.4 takes the pattern as HPDF_REAL, 2.3 as HPDF_UINT16.
#if HPDF_MAJOR_VERSION > 2 || (HPDF_MAJOR_VERSION == 2 && HPDF_MINOR_VERSION >= 4)
            const HPDF_REAL dash[] = {3, 2};
#else
            const HPDF_UINT16 dash[] = {3, 2};
#endif
            HPDF_Page_SetDash(page_, dash, 2, 0);
            line(x, oy, x, oy + ph, 0.8f, {0, 0, 0});
            HPDF_Page_SetDash(page_, nullptr, 0, 0);
            const Box marker(*this, ox, ox + pw);
            text(x + 3, oy + ph - 9, clean_utf8(c.marker_label), Face::Regular, 7.5f, kText);
        }
        line(ox, oy, ox + pw, oy, 0.6f, kGrey);
        line(ox, oy, ox, oy + ph, 0.6f, kGrey);
        const std::string xl = clean_utf8(c.x_label);
        text(ox + (pw - text_w(xl, Face::Regular, 8)) / 2, oy - 26, xl, Face::Regular, 8, kText);
        // The y label turned by 90 degrees.
        const std::string yl = clean_utf8(c.y_label);
        if (!yl.empty()) {
            // A label longer than the axis is set smaller to fit it (TST-52).
            const float ysize = std::min(8.0f, 8.0f * ph / text_w(yl, Face::Regular, 8));
            const float yw = text_w(yl, Face::Regular, ysize);
            if (yw > ph + 0.5f) issue("\"" + yl + "\" (the y axis) is longer than its axis by " + pt(yw - ph) + " pt");
            fill_colour(kText);
            HPDF_Page_BeginText(page_);
            HPDF_Page_SetFontAndSize(page_, font(Face::Regular), ysize);
            HPDF_Page_SetTextMatrix(page_, 0, 1, -1, 0, kL + 10, oy + (ph - yw) / 2);
            HPDF_Page_ShowText(page_, yl.c_str());
            HPDF_Page_EndText(page_);
        }
        y_ -= H;
        if (!c.summary.empty())
            for (const auto& l : wrap(c.summary, width(), Face::Regular, 8)) {
                text(kL, y_ - 9, l, Face::Regular, 8, kGrey);
                y_ -= 10;
            }
        y_ -= 10;
    }

    // DEMO-12: the credits of every plan shown, at the end.
    void credits() {
        const std::vector<PlanCredit> cs = d_.plan_credits();
        if (cs.empty()) return;
        heading({1, d_.words.plan_credits, {}}, 40);
        for (const PlanCredit& c : cs) {
            std::string s = credit_line(c, d_.words);
            if (!c.item_url.empty()) s += " " + c.item_url;
            paragraph({s, Tone::Normal});
        }
    }

    void footers() {
        const std::size_t n = pages_.size();
        const std::string ref = kReference;
        for (std::size_t i = 0; i < n; ++i) {
            page_ = pages_[i];
            page_no_ = i + 1;
            line(kL, 48, kW - kR, 48, 0.5f, kRule);
            const float logo = 14;
            HPDF_Page_DrawImage(page_, logo_small_, kL, 29, logo, logo);
            text(kL + logo + 5, 33, ref, Face::Bold, 7.5f, kText);
            const float rw = text_w(ref, Face::Bold, 7.5f);
            link(kL, 28, logo + 5 + rw, 16, kReferenceUrl);
            if (!d_.demo_label.empty()) {
                const std::string l = clean_utf8(d_.demo_label);
                text((kW - text_w(l, Face::Bold, 7.5f)) / 2, 33, l, Face::Bold, 7.5f, kDemo);
            }
            const std::string p = clean_utf8(d_.words.page + " " + std::to_string(i + 1) + " " + d_.words.of + " " +
                                             std::to_string(n));
            text(kW - kR - text_w(p, Face::Regular, 7.5f), 33, p, Face::Regular, 7.5f, kGrey);
            if (!d_.footer.empty()) text(kL, 18, clean_utf8(d_.footer), Face::Regular, 7, kGrey);
        }
    }

    // TST-52: the texts placed on each page and the overlaps among them.
    struct Placed {
        std::size_t page;
        float x0, y0, x1, y1;
        std::string text;
    };
    void overlaps() {
        if (issues_ == nullptr) return;
        for (std::size_t i = 0; i < placed_.size(); ++i)
            for (std::size_t j = i + 1; j < placed_.size(); ++j) {
                const Placed& a = placed_[i];
                const Placed& b = placed_[j];
                if (a.page != b.page) continue;
                const float ox = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
                const float oy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
                if (ox > 0.5f && oy > 0.5f)
                    issues_->push_back("page " + std::to_string(a.page) + ": \"" + a.text + "\" and \"" + b.text +
                                       "\" overlap");
            }
    }
    std::vector<std::string>* issues_ = nullptr;
    std::vector<Placed> placed_;
    std::size_t page_no_ = 0;
    float box0_ = kL, box1_ = kW - kR;

    const Document& d_;
    const Assets& a_;
    HPDF_Doc pdf_ = nullptr;
    HPDF_Font regular_ = nullptr, bold_ = nullptr, mono_ = nullptr;
    HPDF_Image logo_ = nullptr, logo_small_ = nullptr;
    HPDF_Page page_ = nullptr;
    std::vector<HPDF_Page> pages_;
    float y_ = 0;
    bool failed_ = false;
    bool compress_ = true;
    std::string error_;
};

}  // namespace

std::vector<std::uint8_t> render_pdf(const Document& d, const Assets& assets, const PdfOptions& o) {
    Writer w(d, assets, o);
    return w.run();
}

}  // namespace yantra::report
