// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA tools window (DB-07; tetra_calculator.h). Only snprintf and
// the raster functions: no allocation, no exceptions across the C ABI.

#include "tetra_calculator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "tetra_raster.h"

namespace yantra::tetra {

namespace {

constexpr yantra_ui_control control(const char* id, const char* label, const char* tooltip)
{
    return yantra_ui_control{sizeof(yantra_ui_control), 0, id, label, tooltip, nullptr, nullptr, nullptr};
}

constexpr yantra_enum_item kOffsets[] = {
    {0, "zero", "0 kHz"}, {1, "plus_6_25", "+6.25 kHz"}, {2, "minus_6_25", "-6.25 kHz"}, {3, "plus_12_5", "+12.5 kHz"}};
constexpr yantra_enum_item kDuplex[] = {{0, "code_0", "Code 0"}, {1, "code_1", "Code 1"}, {2, "code_2", "Code 2"},
                                        {3, "code_3", "Code 3"}, {4, "code_4", "Code 4"}, {5, "code_5", "Code 5"},
                                        {6, "code_6", "Code 6"}, {7, "code_7", "Code 7"}};

void mhz(char* out, std::size_t n, std::uint64_t hz)
{
    std::snprintf(out, n, "%.5f", static_cast<double>(hz) / 1e6);
}

void channel_to_frequency(CalculatorState& s, const yantra_ui_api* ui)
{
    static const yantra_ui_control band = control(
        "band", "Frequency band", "Frequency band code of MAC-SYSINFO, 1 to 15: the 100 MHz block (3 = 300 MHz, 4 = 400 MHz, 8 = 800 MHz).");
    static const yantra_ui_control carrier = control(
        "carrier", "Carrier number", "Main carrier number of MAC-SYSINFO, 0 to 4095: 25 kHz steps from the start of the band.");
    static const yantra_ui_control offset =
        control("offset", "Offset", "Carrier offset of MAC-SYSINFO: 0, +6.25, -6.25 or +12.5 kHz.");
    static const yantra_ui_control duplex = control(
        "duplex", "Duplex spacing", "Duplex spacing code of MAC-SYSINFO, 0 to 7; its spacing depends on the band (ETSI TS 100 392-15).");
    static const yantra_ui_control reverse = control(
        "reverse", "Reverse operation", "Reverse operation of MAC-SYSINFO: the uplink lies above the downlink instead of below.");
    static const yantra_ui_control downlink =
        control("downlink", "Downlink", "Downlink frequency: band x 100 MHz + carrier x 25 kHz + offset.");
    static const yantra_ui_control uplink =
        control("uplink", "Uplink", "Uplink frequency: downlink minus the duplex spacing (plus with reverse operation).");
    static const yantra_ui_control spacing = control("spacing", "Spacing", "Duplex spacing of this band and code.");
    static const yantra_ui_control channel = control(
        "channel", "Channel number (Yantra)", "Yantra's channel number of the carrier: band x 10000 + carrier number (cell database, CSV channel column).");

    ui->heading(ui->ui, "Channel to frequency",
                "Computes the downlink and uplink of a carrier as a cell broadcasts it in MAC-SYSINFO.");
    ui->number(ui->ui, &band, &s.band, 1, kMaxBand, 1, "");
    ui->number(ui->ui, &carrier, &s.carrier, 0, kMaxCarrier, 1, "");
    ui->choice(ui->ui, &offset, kOffsets, 4, &s.offset);
    ui->choice(ui->ui, &duplex, kDuplex, 8, &s.duplex);
    ui->checkbox(ui->ui, &reverse, &s.reverse);

    const Carrier c{static_cast<std::uint32_t>(s.band), static_cast<std::uint32_t>(s.carrier),
                    static_cast<std::uint32_t>(s.offset)};
    char text[48];
    const auto dl = downlink_hz(c);
    if (dl) mhz(text, sizeof text, *dl);
    else std::snprintf(text, sizeof text, "-");
    ui->value(ui->ui, &downlink, text, "MHz");
    const auto sp = duplex_spacing_hz(c.band, static_cast<std::uint32_t>(s.duplex));
    if (sp) mhz(text, sizeof text, *sp);
    else std::snprintf(text, sizeof text, "reserved");
    ui->value(ui->ui, &spacing, text, sp ? "MHz" : "");
    const auto ul = uplink_hz(c, static_cast<std::uint32_t>(s.duplex), s.reverse != 0);
    if (ul) mhz(text, sizeof text, *ul);
    else std::snprintf(text, sizeof text, "-");
    ui->value(ui->ui, &uplink, text, ul ? "MHz" : "");
    std::snprintf(text, sizeof text, "%llu", static_cast<unsigned long long>(channel_number(c.band, c.carrier)));
    ui->value(ui->ui, &channel, text, "");
}

void frequency_to_channel(CalculatorState& s, const yantra_ui_api* ui)
{
    static const yantra_ui_control frequency =
        control("frequency", "Downlink frequency (MHz)", "A downlink frequency in MHz, e.g. 390.0125.");
    static const yantra_ui_control band = control("found_band", "Frequency band", "Band code of the frequency.");
    static const yantra_ui_control carrier = control("found_carrier", "Carrier number", "Carrier number of the frequency.");
    static const yantra_ui_control offset = control("found_offset", "Offset", "The offset that puts it on the 25 kHz raster.");
    static const yantra_ui_control channel =
        control("found_channel", "Channel number (Yantra)", "Yantra's channel number: band x 10000 + carrier number.");

    ui->heading(ui->ui, "Frequency to channel", "The reverse: the band, carrier number and offset of a downlink frequency.");
    ui->text_input(ui->ui, &frequency, s.frequency_mhz, sizeof s.frequency_mhz);
    char* end = nullptr;
    const double f = std::strtod(s.frequency_mhz, &end);
    std::optional<Carrier> c;
    if (end != s.frequency_mhz && std::isfinite(f) && f > 0.0 && f < 2000.0)
        c = carrier_of(static_cast<std::uint64_t>(std::llround(f * 1e6)));
    if (!c) {
        ui->status(ui->ui, YANTRA_UI_TONE_WARN, "Not on the TETRA raster",
                   "The frequency is on none of the four offset rasters (25 kHz steps, offset 0, +6.25, -6.25 or "
                   "+12.5 kHz), or it is not a number in MHz.");
        return;
    }
    char text[32];
    std::snprintf(text, sizeof text, "%u", c->band);
    ui->value(ui->ui, &band, text, "");
    std::snprintf(text, sizeof text, "%u", c->carrier);
    ui->value(ui->ui, &carrier, text, "");
    ui->value(ui->ui, &offset, kOffsets[c->offset].label, "");
    std::snprintf(text, sizeof text, "%llu", static_cast<unsigned long long>(channel_number(c->band, c->carrier)));
    ui->value(ui->ui, &channel, text, "");
}

void subscriber_classes(CalculatorState& s, const yantra_ui_api* ui)
{
    static const yantra_ui_control value = control(
        "subscriber_value", "Value", "The subscriber_class field as a number, 0 to 65535 (the cell database and the records hold it so).");
    static const yantra_ui_control hex = control("subscriber_hex", "Hexadecimal", "The same value in hexadecimal.");
    static const char* const ids[16] = {"class_1",  "class_2",  "class_3",  "class_4", "class_5",  "class_6",
                                        "class_7",  "class_8",  "class_9",  "class_10", "class_11", "class_12",
                                        "class_13", "class_14", "class_15", "class_16"};
    static const char* const labels[16] = {"Class 1",  "Class 2",  "Class 3",  "Class 4",  "Class 5",  "Class 6",
                                           "Class 7",  "Class 8",  "Class 9",  "Class 10", "Class 11", "Class 12",
                                           "Class 13", "Class 14", "Class 15", "Class 16"};

    ui->heading(ui->ui, "Subscriber classes",
                "The 16 subscriber classes a cell admits (D-MLE-SYSINFO): class 1 is the first bit sent, the most "
                "significant bit of the value.");
    if (ui->number(ui->ui, &value, &s.subscriber_value, 0, 65535, 1, "")) {
        const auto v = static_cast<std::uint32_t>(s.subscriber_value);
        for (int n = 1; n <= 16; ++n) s.classes[n - 1] = (v & subscriber_class_bit(n)) != 0 ? 1 : 0;
    }
    bool changed = false;
    for (int n = 1; n <= 16; ++n) {
        const yantra_ui_control box{sizeof(yantra_ui_control), 0, ids[n - 1], labels[n - 1],
                                    "Admits this subscriber class.", nullptr, nullptr, nullptr};
        if ((n - 1) % 4 != 0) ui->same_line(ui->ui);
        if (ui->checkbox(ui->ui, &box, &s.classes[n - 1])) changed = true;
    }
    if (changed) {
        std::uint32_t v = 0;
        for (int n = 1; n <= 16; ++n)
            if (s.classes[n - 1] != 0) v |= subscriber_class_bit(n);
        s.subscriber_value = v;
    }
    char text[16];
    std::snprintf(text, sizeof text, "0x%04X", static_cast<unsigned>(s.subscriber_value));
    ui->value(ui->ui, &hex, text, "");
}

}  // namespace

void draw_calculator(CalculatorState& s, const yantra_ui_api* ui)
{
    channel_to_frequency(s, ui);
    ui->separator(ui->ui);
    frequency_to_channel(s, ui);
    ui->separator(ui->ui);
    subscriber_classes(s, ui);
}

}  // namespace yantra::tetra
