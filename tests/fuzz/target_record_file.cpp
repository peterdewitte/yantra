// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target record_file (RAD-96): a yantra-radio-records file (RAD-65:
// yantra-gen's ground truth, decode-iq output, recordings Yantra
// imports). Every line the reader accepts is checked against the header's
// schemas and must survive the writer: a record written again reads back
// to the same line.

#include "fuzz.hpp"

#include "yantra/radios/record_file.hpp"

#include <sstream>
#include <string>

namespace radios = yantra::radios;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 256 * 1024)
        return 0;
    std::istringstream in(yantra::fuzz::text_of(data, size));
    radios::RecordFileReader reader(in);
    std::string error;
    if (!reader.read_header(error))
        return 0;
    std::ostringstream out;
    radios::RecordFileWriter writer(out);
    writer.write_header(reader.header());
    radios::RecordFileLine line;
    for (int guard = 0; guard < 4096; ++guard) {
        const auto r = reader.next(line, error);
        if (r != radios::RecordFileReader::Result::line)
            break;
        if (line.kind != radios::RecordFileLine::Kind::record)
            continue;
        if (line.record.schema_index >= reader.header().schemas.size())
            fail("record of a schema the header does not have");
        // Written again and read back: the same line.
        const std::string text = writer.record_line(line.record);
        std::istringstream again_in(out.str() + text + "\n");
        radios::RecordFileReader again(again_in);
        radios::RecordFileLine back;
        if (!again.read_header(error) || again.next(back, error) != radios::RecordFileReader::Result::line) {
            std::fprintf(stderr, "%s\n", error.c_str());
            fail("a record written by the writer is refused");
        }
        if (writer.record_line(back.record) != text)
            fail("record write + read changed the record");
    }
    return 0;
}
