// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HTML back end of the report (EXP-05; docs/REPORT.md): one
// self-contained file (inline CSS; the pictures as PNG data: URIs, charts
// as inline SVG, the curtat.be logo as its SVG and the Yantra logo as the
// favicon, both data: URIs), written as XHTML-compatible markup (every
// element closed, attributes quoted, text escaped) so XML tools parse it
// too. The cover carries the curtat.be logo and reference with its link
// (BRD-02); the footer repeats them on every printed page (fixed in print
// media) next to the DEMO label (DEMO-07), which is also a banner at the
// top, in the title and in a meta element.
// Copied from yantra-v1 c24e8f5: core/report/html.cpp, adapted for EXP-05
// (cover, branding, favicon BRD-06, plan credits).

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "report_internal.h"
#include "yantra/core/report/document.h"

namespace yantra::report {

namespace {

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&#39;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string f1(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}

std::string hex(std::uint32_t v) {
    char b[8];
    std::snprintf(b, sizeof b, "#%06x", static_cast<unsigned>(v & 0xFFFFFF));
    return b;
}

const char* tone_class(Tone t) {
    switch (t) {
    case Tone::Normal: return "";
    case Tone::Note: return "note";
    case Tone::Pass: return "pass";
    case Tone::Fail: return "fail";
    case Tone::Demo: return "demo";
    }
    return "";
}

std::string data_uri(const char* type, const std::vector<std::uint8_t>& bytes) {
    return std::string("data:") + type + ";base64," + detail::base64(bytes);
}

const char* kCss = R"(body{font-family:Inter,"Helvetica Neue",Arial,sans-serif;color:#222;margin:0;background:#fff;line-height:1.4}
main,header,footer{max-width:60em;margin:0 auto;padding:0 1.2em}
header.cover{padding-top:1.2em;padding-bottom:1em;border-bottom:1px solid #ccc;margin-bottom:1em}
.brand{display:flex;align-items:center;gap:.9em;margin-bottom:1.4em}
.brand img{width:64px;height:64px}
.brand a{color:#14549e;text-decoration:none}
.brand .ref{font-weight:600;font-size:1.1em;color:#222}
h1{font-size:1.8em;margin:.4em 0 .1em}
h2{font-size:1.25em;margin:1.4em 0 .4em;border-bottom:1px solid #ccc;padding-bottom:.15em}
h3{font-size:1.05em;margin:1em 0 .3em}
.subtitle{color:#555;margin:0 0 1em}
.demo-banner{position:fixed;top:0;left:0;right:0;background:#b71c1c;color:#fff;font-weight:600;text-align:center;padding:.3em;z-index:10;letter-spacing:.05em}
body.demo{padding-top:2.2em}
.demo-label{background:#b71c1c;color:#fff;font-weight:600;font-size:1.2em;text-align:center;padding:.5em;margin:1em 0}
p.note{color:#555;font-size:.92em}
p.demo{background:#fdecea;border-left:4px solid #b71c1c;padding:.4em .6em}
p.pass{background:#e8f5e9;border-left:4px solid #2e7d32;padding:.4em .6em;font-weight:600}
p.fail{background:#fdecea;border-left:4px solid #c62828;padding:.4em .6em;font-weight:600}
table{border-collapse:collapse;width:100%;margin:.4em 0 1em;font-size:.9em}
th,td{border:1px solid #ddd;padding:.25em .45em;text-align:left;vertical-align:top}
th{background:#f3f3f3}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
td.num{font-family:"JetBrains Mono",ui-monospace,monospace;font-size:.92em}
table.kv th{width:28%;background:#fafafa;font-weight:600}
tr.pass td{background:#e8f5e9}
tr.fail td{background:#fdecea}
tr.note td{color:#666}
figure{margin:.6em 0 1em}
figure img{max-width:100%;height:auto;border:1px solid #ccc}
figcaption{font-size:.88em;color:#555}
figcaption .credit{display:block}
ul.legend{list-style:none;padding:0;margin:.3em 0 1em;columns:2}
ul.legend li{margin:.1em 0}
.swatch{display:inline-block;width:1em;height:1em;border:1px solid #555;vertical-align:-.15em;margin-right:.4em}
svg text{font-family:inherit;font-size:11px;fill:#333}
footer.page-footer{color:#666;font-size:.85em;border-top:1px solid #ccc;margin-top:2em;padding-top:.4em;padding-bottom:1.5em;display:flex;flex-wrap:wrap;align-items:center;gap:.4em 1.2em}
footer.page-footer img{width:16px;height:16px;vertical-align:-3px;margin-right:.35em}
footer.page-footer a{color:#222;font-weight:600;text-decoration:none}
footer.page-footer .demo{color:#b71c1c;font-weight:600}
@media print{.demo-banner{position:fixed}h2{break-after:avoid}figure,table.kv{break-inside:avoid}
body{padding-bottom:3.2em}footer.page-footer{position:fixed;bottom:0;left:0;right:0;max-width:none;margin:0;padding:.3em 1.2em;background:#fff}}
)";

void chart_svg(std::ostringstream& o, const ChartBlock& c) {
    const double W = 640, H = 270, L = 58, R = 16, T = 26, B = 44;
    const double pw = W - L - R, ph = H - T - B;
    const double xs = c.x_max > c.x_min ? c.x_max - c.x_min : 1, ys = c.y_max > c.y_min ? c.y_max - c.y_min : 1;
    auto X = [&](double x) { return L + (x - c.x_min) / xs * pw; };
    auto Y = [&](double y) { return T + ph - (y - c.y_min) / ys * ph; };
    o << "<figure><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << W << " " << H
      << "\" width=\"100%\" role=\"img\" aria-label=\"" << esc(c.title) << "\">\n";
    o << "<text x=\"" << L << "\" y=\"16\" style=\"font-weight:600;font-size:13px\">" << esc(c.title) << "</text>\n";
    for (std::size_t k = 0; k < c.y_tick_labels.size(); ++k) {
        const double y = Y(c.y_min + static_cast<double>(k) * c.y_tick);
        o << "<line x1=\"" << f1(L) << "\" y1=\"" << f1(y) << "\" x2=\"" << f1(L + pw) << "\" y2=\"" << f1(y)
          << "\" stroke=\"#e2e2e2\" />\n";
        o << "<text x=\"" << f1(L - 6) << "\" y=\"" << f1(y + 4) << "\" text-anchor=\"end\">" << esc(c.y_tick_labels[k])
          << "</text>\n";
    }
    for (std::size_t k = 0; k < c.x_tick_labels.size(); ++k) {
        const double x = X(c.x_min + static_cast<double>(k) * c.x_tick);
        o << "<line x1=\"" << f1(x) << "\" y1=\"" << f1(T + ph) << "\" x2=\"" << f1(x) << "\" y2=\"" << f1(T + ph + 4)
          << "\" stroke=\"#555\" />\n";
        o << "<text x=\"" << f1(x) << "\" y=\"" << f1(T + ph + 16) << "\" text-anchor=\"middle\">"
          << esc(c.x_tick_labels[k]) << "</text>\n";
    }
    if (c.kind == ChartBlock::Kind::Bars) {
        for (const auto& b : c.bars) {
            if (b.y <= 0) continue;
            const double x0 = X(b.x0) + 0.5, x1 = X(b.x1) - 0.5;
            o << "<rect x=\"" << f1(x0) << "\" y=\"" << f1(Y(b.y)) << "\" width=\"" << f1(std::max(0.5, x1 - x0))
              << "\" height=\"" << f1(Y(c.y_min) - Y(b.y)) << "\" fill=\"" << (b.good ? "#2e7d32" : "#c62828") << "\" />\n";
        }
    } else if (!c.steps.empty()) {
        o << "<polyline fill=\"none\" stroke=\"#1565c0\" stroke-width=\"2\" points=\"";
        for (std::size_t i = 0; i < c.steps.size(); ++i) {
            const auto& p = c.steps[i];
            if (i) o << f1(X(p.first)) << "," << f1(Y(c.steps[i - 1].second)) << " ";
            o << f1(X(p.first)) << "," << f1(Y(p.second)) << (i + 1 < c.steps.size() ? " " : "");
        }
        o << "\" />\n";
    }
    if (c.has_marker && c.marker_x >= c.x_min && c.marker_x <= c.x_max) {
        const double x = X(c.marker_x);
        o << "<line x1=\"" << f1(x) << "\" y1=\"" << f1(T) << "\" x2=\"" << f1(x) << "\" y2=\"" << f1(T + ph)
          << "\" stroke=\"#000\" stroke-dasharray=\"4 3\" />\n";
        o << "<text x=\"" << f1(x + 4) << "\" y=\"" << f1(T + 11) << "\">" << esc(c.marker_label) << "</text>\n";
    }
    o << "<line x1=\"" << f1(L) << "\" y1=\"" << f1(T + ph) << "\" x2=\"" << f1(L + pw) << "\" y2=\"" << f1(T + ph)
      << "\" stroke=\"#555\" />\n";
    o << "<line x1=\"" << f1(L) << "\" y1=\"" << f1(T) << "\" x2=\"" << f1(L) << "\" y2=\"" << f1(T + ph)
      << "\" stroke=\"#555\" />\n";
    o << "<text x=\"" << f1(L + pw / 2) << "\" y=\"" << f1(H - 6) << "\" text-anchor=\"middle\">" << esc(c.x_label)
      << "</text>\n";
    o << "<text x=\"14\" y=\"" << f1(T + ph / 2) << "\" text-anchor=\"middle\" transform=\"rotate(-90 14 "
      << f1(T + ph / 2) << ")\">" << esc(c.y_label) << "</text>\n";
    o << "</svg><figcaption>" << esc(c.summary) << "</figcaption></figure>\n";
}

}  // namespace

std::string render_html(const Document& d, const Assets& assets, const HtmlOptions&) {
    const std::string logo = data_uri("image/svg+xml", detail::read_file(assets.curtat_logo_svg));
    const std::string favicon = data_uri("image/png", detail::read_file(assets.favicon));
    std::ostringstream o;
    const bool demo = !d.demo_label.empty();
    o << "<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"" << esc(d.lang) << "\" xml:lang=\""
      << esc(d.lang) << "\">\n<head>\n<meta charset=\"utf-8\" />\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n";
    if (!d.generator.empty()) o << "<meta name=\"generator\" content=\"" << esc(d.generator) << "\" />\n";
    if (demo) o << "<meta name=\"yantra-label\" content=\"" << esc(d.demo_label) << "\" />\n";
    o << "<link rel=\"icon\" type=\"image/png\" sizes=\"32x32\" href=\"" << favicon << "\" />\n";
    o << "<title>" << esc(demo ? d.demo_label + " - " + d.title : d.title) << "</title>\n<style>" << kCss
      << "</style>\n</head>\n<body" << (demo ? " class=\"demo\"" : "") << ">\n";
    if (demo) o << "<div class=\"demo-banner\" role=\"note\">" << esc(d.demo_label) << "</div>\n";

    // The cover (BRD-02).
    o << "<header class=\"cover\">\n<div class=\"brand\"><a href=\"" << kReferenceUrl << "\"><img src=\"" << logo
      << "\" alt=\"curtat.be\" width=\"64\" height=\"64\" /></a><div><a class=\"ref\" href=\"" << kReferenceUrl << "\">"
      << esc(kReference) << "</a><br /><a href=\"" << kReferenceUrl << "\">" << kReferenceUrl << "</a></div></div>\n";
    o << "<h1>" << esc(d.title) << "</h1>\n";
    if (!d.subtitle.empty()) o << "<p class=\"subtitle\">" << esc(d.subtitle) << "</p>\n";
    if (demo) o << "<div class=\"demo-label\">" << esc(d.demo_label) << "</div>\n";
    auto kv = [&](const KeyValues& k) {
        o << "<table class=\"kv\">\n";
        for (const auto& [key, v] : k.rows) o << "<tr><th scope=\"row\">" << esc(key) << "</th><td>" << esc(v) << "</td></tr>\n";
        o << "</table>\n";
    };
    if (!d.cover.rows.empty()) kv(d.cover);
    o << "</header>\n<main>\n";

    for (const Block& b : d.blocks) {
        switch (b.type) {
        case Block::Type::Heading: {
            const int h = b.heading.level <= 1 ? 2 : 3;
            o << "<h" << h;
            if (!b.heading.anchor.empty()) o << " id=\"" << esc(b.heading.anchor) << "\"";
            o << ">" << esc(b.heading.text) << "</h" << h << ">\n";
            break;
        }
        case Block::Type::Paragraph: {
            const char* cls = tone_class(b.paragraph.tone);
            o << "<p" << (*cls ? std::string(" class=\"") + cls + "\"" : "") << ">" << esc(b.paragraph.text) << "</p>\n";
            break;
        }
        case Block::Type::KeyValues: kv(b.kv); break;
        case Block::Type::Table: {
            const TableBlock& t = b.table;
            auto num = [&](std::size_t j) { return j < t.numeric.size() && t.numeric[j]; };
            o << "<table>\n<thead><tr>";
            for (std::size_t j = 0; j < t.header.size(); ++j)
                o << "<th scope=\"col\"" << (num(j) ? " class=\"num\"" : "") << ">" << esc(t.header[j]) << "</th>";
            o << "</tr></thead>\n<tbody>\n";
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const char* cls = i < t.row_tone.size() ? tone_class(t.row_tone[i]) : "";
                o << "<tr" << (*cls ? std::string(" class=\"") + cls + "\"" : "") << ">";
                for (std::size_t j = 0; j < t.rows[i].size(); ++j)
                    o << "<td" << (num(j) ? " class=\"num\"" : "") << ">" << esc(t.rows[i][j]) << "</td>";
                o << "</tr>\n";
            }
            o << "</tbody>\n</table>\n";
            break;
        }
        case Block::Type::Image: {
            const ImageBlock& im = b.image;
            if (im.image.empty()) break;
            o << "<figure><img src=\"" << data_uri("image/png", exports::encode_png(im.image)) << "\" width=\""
              << im.image.width() << "\" height=\"" << im.image.height() << "\" alt=\"" << esc(im.alt) << "\" />";
            if (!im.caption.empty() || im.credit) {
                o << "<figcaption>" << esc(im.caption);
                if (im.credit) o << "<span class=\"credit\">" << esc(credit_line(*im.credit, d.words)) << "</span>";
                o << "</figcaption>";
            }
            o << "</figure>\n";
            break;
        }
        case Block::Type::Legend:
            o << "<h3>" << esc(b.legend.title) << "</h3>\n<ul class=\"legend\">\n";
            for (const auto& e : b.legend.entries)
                o << "<li><span class=\"swatch\" style=\"background:" << hex(e.rgb) << "\"></span>" << esc(e.label)
                  << (e.count.empty() ? "" : " (" + esc(e.count) + ")") << "</li>\n";
            o << "</ul>\n";
            break;
        case Block::Type::Chart: chart_svg(o, b.chart); break;
        case Block::Type::PageBreak: o << "<div style=\"break-after:page\"></div>\n"; break;
        }
    }
    // DEMO-12: the credits of every plan shown.
    const std::vector<PlanCredit> credits = d.plan_credits();
    if (!credits.empty()) {
        o << "<h2>" << esc(d.words.plan_credits) << "</h2>\n<ul class=\"credits\">\n";
        for (const PlanCredit& c : credits) {
            o << "<li>" << esc(credit_line(c, d.words));
            if (!c.item_url.empty()) o << " <a href=\"" << esc(c.item_url) << "\">" << esc(c.item_url) << "</a>";
            o << "</li>\n";
        }
        o << "</ul>\n";
    }
    o << "</main>\n<footer class=\"page-footer\"><span><a href=\"" << kReferenceUrl << "\"><img src=\"" << logo
      << "\" alt=\"\" width=\"16\" height=\"16\" />" << esc(kReference) << "</a></span>";
    if (demo) o << "<span class=\"demo\">" << esc(d.demo_label) << "</span>";
    if (!d.footer.empty()) o << "<span>" << esc(d.footer) << "</span>";
    o << "</footer>\n</body>\n</html>\n";
    return o.str();
}

}  // namespace yantra::report
