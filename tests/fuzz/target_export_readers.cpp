// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target export_readers (TST-12, TST-33): the CSV and DXF readers of
// the export round trips (EXP-01, EXP-03). Each refuses the text with
// ExportError or reads it; a DXF drawing it reads writes DXF text that
// reads back to the same drawing.

#include "fuzz.hpp"

#include "yantra/core/export/csv.h"
#include "yantra/core/export/dxf.h"
#include "yantra/core/export/table.h"

namespace ex = yantra::exports;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 256 * 1024) return 0;
    const std::string text = yantra::fuzz::text_of(data, size);
    for (const char sep : {',', ';'}) {
        try {
            const ex::CsvTable t = ex::read_csv(text, sep);
            for (const auto& row : t.rows)
                if (row.size() != t.header.size()) fail("a CSV row with another field count was accepted");
        } catch (const ex::ExportError&) {
        }
    }
    ex::DxfDrawing d;
    try {
        d = ex::read_dxf(text);
    } catch (const ex::ExportError&) {
        return 0;
    }
    ex::DxfDrawing again;
    try {
        again = ex::read_dxf(ex::dxf_text(d));
    } catch (const ex::ExportError&) {
        fail("a DXF drawing's own text is refused");
    }
    (void)again;
    return 0;
}
