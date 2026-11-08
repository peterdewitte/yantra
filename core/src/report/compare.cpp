// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tolerant comparison of a regenerated report with a committed one
// (compare.h, DEMO-06).
#include "yantra/core/report/compare.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <utility>

#include "yantra/core/plan/source.h"
#include "yantra/core/report/document.h"

namespace fs = std::filesystem;

namespace yantra::report {

namespace {

constexpr std::size_t kMaxDifferences = 50;

void differ(CompareResult& r, std::string line) {
    r.within = false;
    if (r.differences.size() < kMaxDifferences) r.differences.push_back(std::move(line));
}

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::vector<std::uint8_t> unbase64(const std::string& s) {
    std::vector<std::uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
        const int v = b64_value(c);
        if (v < 0) continue;  // '=' padding
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

// A data: URI of the HTML: its media type and base64 payload.
struct Picture {
    std::string type;
    std::string payload;
};

// The HTML split into text (pictures replaced by a mark) and its pictures.
struct Split {
    std::string text;
    std::vector<Picture> pictures;
};

Split split_html(const std::string& html) {
    Split s;
    std::size_t pos = 0;
    while (true) {
        const std::size_t at = html.find("data:", pos);
        const std::size_t semi = at == std::string::npos ? at : html.find(";base64,", at);
        if (at == std::string::npos || semi == std::string::npos || semi - at > 64) {
            s.text.append(html, pos, std::string::npos);
            break;
        }
        std::size_t end = semi + 8;
        while (end < html.size() && (b64_value(html[end]) >= 0 || html[end] == '=')) ++end;
        s.text.append(html, pos, at - pos);
        s.text += "\x01picture\x01";
        s.pictures.push_back({html.substr(at + 5, semi - at - 5), html.substr(semi + 8, end - semi - 8)});
        pos = end;
    }
    return s;
}

// A number of the text: [-]digits[.digits].
bool number_at(const std::string& t, std::size_t i, std::size_t& end) {
    std::size_t j = i;
    if (j < t.size() && t[j] == '-') ++j;
    const std::size_t digits = j;
    while (j < t.size() && t[j] >= '0' && t[j] <= '9') ++j;
    if (j == digits) return false;
    if (j + 1 < t.size() && t[j] == '.' && t[j + 1] >= '0' && t[j + 1] <= '9') {
        ++j;
        while (j < t.size() && t[j] >= '0' && t[j] <= '9') ++j;
    }
    end = j;
    return true;
}

struct Token {
    bool number = false;
    std::string text;
    double value = 0.0;
};

std::vector<Token> tokens(const std::string& t) {
    std::vector<Token> out;
    std::string run;
    std::size_t i = 0;
    while (i < t.size()) {
        std::size_t end = 0;
        // A '-' is a sign only before a digit and after no letter/digit
        // (keeps "a-1" ids and "1-2" ranges as text plus numbers).
        const bool sign_ok = t[i] != '-' || i == 0 || !std::isalnum(static_cast<unsigned char>(t[i - 1]));
        if (sign_ok && number_at(t, i, end)) {
            if (!run.empty()) out.push_back({false, std::exchange(run, std::string()), 0.0});
            Token n{true, t.substr(i, end - i), 0.0};
            n.value = std::strtod(n.text.c_str(), nullptr);
            out.push_back(std::move(n));
            i = end;
            continue;
        }
        run += t[i++];
    }
    if (!run.empty()) out.push_back({false, run, 0.0});
    return out;
}

std::string excerpt(const std::string& s) {
    std::string e = s.size() > 60 ? s.substr(0, 60) + "..." : s;
    for (char& c : e)
        if (c == '\n' || c == '\x01') c = ' ';
    return e;
}

bool raster_type(const std::string& type) { return type == "image/png" || type == "image/jpeg"; }

void compare_picture(CompareResult& r, std::size_t n, const Picture& got, const Picture& want,
                     const CompareTolerance& tol) {
    const std::string which = "picture " + std::to_string(n + 1) + " (" + want.type + ")";
    if (got.type != want.type) return differ(r, which + ": type " + got.type);
    if (got.payload == want.payload) return;
    r.identical = false;
    if (!raster_type(want.type)) return differ(r, which + ": differs");
    try {
        const auto a_bytes = unbase64(got.payload);
        const auto b_bytes = unbase64(want.payload);
        const auto a = plan::open_plan_source(std::span<const std::uint8_t>(a_bytes));
        const auto b = plan::open_plan_source(std::span<const std::uint8_t>(b_bytes));
        const plan::PageSize sa = a->page_size(0), sb = b->page_size(0);
        if (sa != sb)
            return differ(r, which + ": size " + std::to_string(static_cast<int>(sa.width)) + "x" +
                                 std::to_string(static_cast<int>(sa.height)) + ", committed " +
                                 std::to_string(static_cast<int>(sb.width)) + "x" +
                                 std::to_string(static_cast<int>(sb.height)));
        const plan::PixelRect all{0, 0, static_cast<int>(sa.width), static_cast<int>(sa.height)};
        const plan::Raster ra = a->render(0, 1.0, all), rb = b->render(0, 1.0, all);
        std::size_t off = 0;
        const std::size_t pixels = static_cast<std::size_t>(ra.width) * static_cast<std::size_t>(ra.height);
        for (std::size_t p = 0; p < pixels; ++p) {
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int(ra.rgba[p * 4 + c]) - int(rb.rgba[p * 4 + c]));
                if (d > tol.pixel_delta) {
                    ++off;
                    break;
                }
            }
        }
        const double share = pixels ? static_cast<double>(off) / static_cast<double>(pixels) : 0.0;
        if (share > tol.pixel_share) {
            std::ostringstream o;
            o << which << ": " << off << " of " << pixels << " pixels differ (" << share * 100.0
              << " %, tolerance " << tol.pixel_share * 100.0 << " %)";
            differ(r, o.str());
        }
    } catch (const std::exception& e) {
        differ(r, which + ": cannot be decoded (" + e.what() + ")");
    }
}

std::string read_file(const fs::path& f, bool& ok) {
    std::ifstream in(f, std::ios::binary);
    ok = static_cast<bool>(in);
    return ok ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string();
}

std::string lower_extension(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

}  // namespace

CompareResult compare_html(const std::string& got, const std::string& want, const CompareTolerance& tol) {
    CompareResult r;
    r.identical = got == want;
    if (r.identical) return r;
    const Split a = split_html(got), b = split_html(want);
    const std::vector<Token> ta = tokens(a.text), tb = tokens(b.text);
    const std::size_t n = std::min(ta.size(), tb.size());
    for (std::size_t i = 0; i < n; ++i) {
        const Token& x = ta[i];
        const Token& y = tb[i];
        if (x.number != y.number) {
            differ(r, "text: \"" + excerpt(x.text) + "\", committed \"" + excerpt(y.text) + "\"");
            break;  // the rest is out of step
        }
        if (!x.number) {
            if (x.text != y.text) {
                // Where the runs part, with a little before it.
                std::size_t k = 0;
                while (k < x.text.size() && k < y.text.size() && x.text[k] == y.text[k]) ++k;
                const std::size_t from = k > 20 ? k - 20 : 0;
                differ(r, "text: \"" + excerpt(x.text.substr(from)) + "\", committed \"" +
                              excerpt(y.text.substr(from)) + "\"");
                break;
            }
            continue;
        }
        const double d = std::fabs(x.value - y.value);
        if (d > tol.number_abs && d > tol.number_rel * std::fabs(y.value)) {
            const std::string before = i > 0 && !tb[i - 1].number ? excerpt(tb[i - 1].text) : std::string();
            differ(r, "number " + x.text + ", committed " + y.text + " (after \"" + before + "\")");
        }
    }
    if (ta.size() != tb.size() && r.within)
        differ(r, "text: " + std::to_string(ta.size()) + " parts, committed " + std::to_string(tb.size()));
    if (a.pictures.size() != b.pictures.size()) {
        differ(r, std::to_string(a.pictures.size()) + " pictures, committed " + std::to_string(b.pictures.size()));
        return r;
    }
    for (std::size_t i = 0; i < a.pictures.size(); ++i) compare_picture(r, i, a.pictures[i], b.pictures[i], tol);
    return r;
}

CompareResult compare_pdf(const std::vector<std::uint8_t>& got, const std::vector<std::uint8_t>& want) {
    CompareResult r;
    r.identical = got == want;
    const PdfCheck a = check_pdf(got), b = check_pdf(want);
    if (!a.ok) differ(r, "PDF: " + a.error);
    if (!b.ok) differ(r, "committed PDF: " + b.error);
    if (a.ok && b.ok && a.pages != b.pages)
        differ(r, "PDF: " + std::to_string(a.pages) + " pages, committed " + std::to_string(b.pages));
    return r;
}

CompareResult compare_report_files(const fs::path& got, const fs::path& want, const CompareTolerance& tol) {
    bool ok_a = false, ok_b = false;
    const std::string a = read_file(got, ok_a), b = read_file(want, ok_b);
    CompareResult r;
    if (!ok_a || !ok_b) {
        r.identical = false;
        differ(r, "cannot read " + (!ok_a ? got : want).string());
        return r;
    }
    const std::string ext = lower_extension(want);
    if (ext == ".html" || ext == ".htm") return compare_html(a, b, tol);
    if (ext == ".pdf")
        return compare_pdf(std::vector<std::uint8_t>(a.begin(), a.end()), std::vector<std::uint8_t>(b.begin(), b.end()));
    r.identical = a == b;
    if (!r.identical) differ(r, want.filename().string() + ": differs");
    return r;
}

}  // namespace yantra::report
