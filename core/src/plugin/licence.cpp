// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v2 68ea20a: core/src/plugin/licence.cpp (PLG-10).

#include "yantra/core/plugin/licence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <vector>

namespace yantra::plugin {

namespace {

// SPDX identifiers whose terms allow combining the code with GPL-3.0 code
// in one program (FSF list of GPL-compatible licences). GPL-2.0-only and
// LGPL-2.0-only are missing on purpose: they are not compatible with GPLv3.
constexpr std::array<std::string_view, 30> kCompatible = {
    "gpl-3.0-only",       "gpl-3.0-or-later", "gpl-3.0",      "gpl-3.0+",
    "gpl-2.0-or-later",   "gpl-2.0+",         "agpl-3.0-only", "agpl-3.0-or-later",
    "agpl-3.0",           "lgpl-3.0-only",    "lgpl-3.0-or-later", "lgpl-3.0",
    "lgpl-3.0+",          "lgpl-2.1-only",    "lgpl-2.1-or-later", "lgpl-2.1",
    "lgpl-2.1+",          "mit",              "mit-0",        "x11",
    "bsd-2-clause",       "bsd-3-clause",     "0bsd",         "isc",
    "zlib",               "bsl-1.0",          "apache-2.0",   "mpl-2.0",
    "unlicense",          "cc0-1.0",
};

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

class Parser {
public:
    explicit Parser(const std::string& text) {
        std::string cur;
        auto flush = [&] {
            if (!cur.empty()) {
                tokens_.push_back(cur);
                cur.clear();
            }
        };
        for (char c : text) {
            if (c == '(' || c == ')') {
                flush();
                tokens_.emplace_back(1, c);
            } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                flush();
            } else {
                cur += c;
            }
        }
        flush();
    }

    // Returns compatible?; sets error on a syntax problem.
    bool parse(std::string& error, std::string& offending) {
        const bool ok = or_expr(error, offending);
        if (error.empty() && pos_ != tokens_.size()) {
            error = "unexpected '" + tokens_[pos_] + "'";
        }
        return ok;
    }

private:
    bool at(std::string_view keyword) const {
        return pos_ < tokens_.size() && lower(tokens_[pos_]) == lower(keyword);
    }
    bool or_expr(std::string& error, std::string& offending) {
        bool ok = and_expr(error, offending);
        while (error.empty() && at("OR")) {
            ++pos_;
            std::string other;
            ok = and_expr(error, other) || ok;
            if (ok) {
                offending.clear();
            } else if (offending.empty()) {
                offending = other;
            }
        }
        return ok;
    }
    bool and_expr(std::string& error, std::string& offending) {
        bool ok = atom(error, offending);
        while (error.empty() && at("AND")) {
            ++pos_;
            std::string other;
            const bool right = atom(error, other);
            if (!right && offending.empty()) {
                offending = other;
            }
            ok = ok && right;
        }
        return ok;
    }
    bool atom(std::string& error, std::string& offending) {
        if (pos_ >= tokens_.size()) {
            error = "expression ends too early";
            return false;
        }
        if (tokens_[pos_] == "(") {
            ++pos_;
            const bool ok = or_expr(error, offending);
            if (error.empty() && !(pos_ < tokens_.size() && tokens_[pos_] == ")")) {
                error = "missing ')'";
            }
            ++pos_;
            return ok;
        }
        const std::string id = tokens_[pos_];
        if (id == ")" || at("AND") || at("OR") || at("WITH")) {
            error = "licence identifier expected before '" + id + "'";
            return false;
        }
        ++pos_;
        if (at("WITH")) {
            ++pos_;
            if (pos_ >= tokens_.size() || tokens_[pos_] == "(" || tokens_[pos_] == ")") {
                error = "exception identifier expected after WITH";
                return false;
            }
            ++pos_;  // exceptions only add permissions
        }
        const bool ok =
            std::find(kCompatible.begin(), kCompatible.end(), lower(id)) != kCompatible.end();
        if (!ok) {
            offending = id;
        }
        return ok;
    }

    std::vector<std::string> tokens_;
    std::size_t pos_ = 0;
};

}  // namespace

LicenceCheck check_licence(const std::string& spdx_expression) {
    LicenceCheck result;
    if (spdx_expression.find_first_not_of(" \t") == std::string::npos) {
        result.message = "no SPDX licence declared";
        return result;
    }
    std::string error;
    std::string offending;
    Parser parser(spdx_expression);
    const bool ok = parser.parse(error, offending);
    if (!error.empty()) {
        result.message = "licence '" + spdx_expression + "' is not a valid SPDX expression: " +
                         error;
        return result;
    }
    if (!ok) {
        result.message = "licence '" + spdx_expression + "' is not GPL-3.0-compatible (" +
                         offending + "); a plugin forms one program with Yantra (PLG-10)";
        return result;
    }
    result.compatible = true;
    return result;
}

const char* licence_notice() noexcept {
    return "A plugin runs inside Yantra and forms one program with it. Yantra is licensed "
           "under the GPL-3.0, so every plugin must be under a GPL-3.0-compatible licence, "
           "declared as an SPDX identifier. Plugins with another licence are not loaded.";
}

}  // namespace yantra::plugin
