// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target calibration (TST-12): a calibration table file (RF-02,
// docs/CALIBRATION.md). parse() refuses it with an error or reads it; a
// table it reads writes JSON that parses to the same JSON again.

#include "fuzz.hpp"

#include "yantra/core/calibration/table.h"

namespace cal = yantra::calibration;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;
    cal::Table t;
    std::string error;
    if (!cal::Table::parse(yantra::fuzz::text_of(data, size), t, error)) {
        if (error.empty()) fail("a refused calibration table has no error");
        return 0;
    }
    const std::string json = t.to_json();
    cal::Table again;
    if (!cal::Table::parse(json, again, error)) fail("a calibration table's own JSON is refused");
    if (again.to_json() != json) fail("calibration JSON round trip changed the table");
    return 0;
}
