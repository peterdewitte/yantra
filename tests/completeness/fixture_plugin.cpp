// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test fixture for the RAD-133 completeness check: a minimal C++ module
// that uses the C++ runtime (strings, vectors, exceptions), as every real
// plugin does, defines the single entry point and one more exported
// symbol. It is not a working radio plugin: its descriptor is empty.
#include <yantra/radio.h>

#include <stdexcept>
#include <string>
#include <vector>

extern "C" int yantra_fixture_bundled_symbol(void);

// Stands for a symbol of a bundled library that is marked for export in
// its own code: the version script must still keep it local.
extern "C" __attribute__((visibility("default"))) int yantra_fixture_bundled_symbol(void)
{
    return 42;
}

namespace {
int count_names(const std::vector<std::string> &names)
{
    if (names.empty())
        throw std::runtime_error("no names");
    return static_cast<int>(names.size());
}
} // namespace

extern "C" YR_EXPORT const yr_descriptor *yantra_radio_entry(void)
{
    static yr_descriptor descriptor{};
    try {
        descriptor.struct_size =
            count_names({YANTRA_RADIO_ENTRY_SYMBOL, "fixture"}) > 0 ? sizeof descriptor : 0;
    } catch (...) {
        descriptor.struct_size = 0;
    }
    return &descriptor;
}
