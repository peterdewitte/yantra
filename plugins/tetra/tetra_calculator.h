// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA tools window of the plugin (DB-07, PLG-06): a channel <->
// frequency calculator (band code, carrier number, offset, duplex spacing
// code, reverse operation, both ways) and a subscriber-class bitmask
// calculator. It is drawn through Yantra's yantra_ui_api, so the window
// follows Yantra's help and touch rules, and the TETRA arithmetic stays
// in the plugin (PLG-01). docs/plugins/TETRA.md "Tools".
//
// Subscriber classes: the 16-bit element of D-MLE-SYSINFO, sent first bit
// first; class 1 is the first bit sent, i.e. the most significant bit of
// the record value (value 0x8000 = class 1 only, 0x0001 = class 16 only).
#pragma once

#include <yantra/plugin.h>

#include <cstdint>

namespace yantra::tetra {

// What the calculator keeps between frames (UI thread only).
struct CalculatorState {
    double band = 3;        // frequency band code 1..15
    double carrier = 3600;  // carrier number 0..4095
    std::int64_t offset = 0;  // offset code 0..3
    std::int64_t duplex = 0;  // duplex spacing code 0..7
    std::int32_t reverse = 0;
    char frequency_mhz[32] = "390.0";
    double subscriber_value = 65535;  // all classes
    std::int32_t classes[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
};

// Value bit of subscriber class n (1..16): class 1 = bit 15.
constexpr std::uint32_t subscriber_class_bit(int n) noexcept
{
    return 1u << (16 - n);
}

// The calculator's window id (one Tools menu entry opens it).
inline constexpr const char* kCalculatorWindow = "calculator";

// Draws the window (yantra_plugin_ui_api::draw of the plugin).
void draw_calculator(CalculatorState& s, const yantra_ui_api* ui);

}  // namespace yantra::tetra
