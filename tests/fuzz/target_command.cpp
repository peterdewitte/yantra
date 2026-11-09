// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target command (TST-12): command lines of the command-line control
// and the remote API (API-01, API-02), text or JSON, one per input line.
// Each is refused with an error or gives a command whose text and JSON
// forms parse back to the same command.

#include "fuzz.hpp"

#include "yantra/core/control/command.h"

#include <sstream>

namespace ctl = yantra::control;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 64 * 1024) return 0;
    std::istringstream in(yantra::fuzz::text_of(data, size));
    std::string line;
    while (std::getline(in, line)) {
        std::string error;
        const auto c = ctl::parse_command_any(line, &error);
        if (!c) {
            if (error.empty()) fail("a refused command has no error");
            continue;
        }
        const auto from_text = ctl::parse_command(c->to_text(), &error);
        if (!from_text || from_text->id != c->id || from_text->args != c->args)
            fail("a command's text form does not parse back to it");
        const auto from_json = ctl::parse_command_json(c->to_json(), &error);
        if (!from_json || from_json->id != c->id || from_json->args != c->args)
            fail("a command's JSON form does not parse back to it");
    }
    return 0;
}
