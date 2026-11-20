// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target sigmf (RAD-96): a .sigmf-meta document as decode-iq reads it
// (RAD-63; recordings from anywhere). What the reader accepts has a
// supported format and capture segments sorted by file sample, the first
// at sample 0.

#include "fuzz.hpp"

#include "iq_file.hpp"

#include <cmath>
#include <string>

using yantra::fuzz::fail;
namespace rh = yantra::radio_host;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 256 * 1024)
        return 0;
    rh::IqMeta meta;
    std::string error;
    if (!rh::parse_sigmf_meta(yantra::fuzz::text_of(data, size), "fuzz.sigmf-meta", meta, error)) {
        if (error.empty())
            fail("refused without an error message");
        return 0;
    }
    if (meta.segments.empty() || meta.segments.front().sample_start != 0)
        fail("no capture segment at sample 0");
    for (std::size_t i = 1; i < meta.segments.size(); ++i)
        if (meta.segments[i].sample_start < meta.segments[i - 1].sample_start)
            fail("capture segments not sorted");
    if (meta.data_path != "fuzz.sigmf-data")
        fail("data file name");
    (void)rh::iq_sample_bytes(meta.format);
    return 0;
}
