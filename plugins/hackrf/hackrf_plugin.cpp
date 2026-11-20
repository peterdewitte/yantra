// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HackRF plugin (RAD-60): the HackRF One through SoapyHackRF on the
// shared SoapySDR layer (RAD-54), with the receiver of section 12 inside.
// What SoapyHackRF documents (gain elements LNA, VGA and AMP, the setting
// bias_tx, hardware info version/part id/serial) becomes clear settings:
// sample rate 2-20 Msps, baseband filter, LNA (IF) and VGA (baseband)
// gain, the RF amplifier on/off, antenna port power (WARNING, off by
// default, off again at every open and replug, never in a preset), the
// external reference where the module offers one, offset tuning away
// from the DC spike; presets "Indoor, sensitive" and "Near site, strong
// signal"; sensors for board, firmware, serial, gains, clipping, noise
// floor and receiver load; band scans (RAD-23) and C1 from a calibration
// offset (RAD-22). A control the module does not report is UNAVAILABLE
// with the reason (RAD-51). The HackRF never transmits here: the layer
// opens receive streams only (Yantra 1.2).
//
// Settings and presets adapted from yantra 68ea20a
// plugins/soapy/hackrf/profile.cpp.
#include <yantra/plugins/soapy_plugin.hpp>

#ifndef YANTRA_HACKRF_PLUGIN_NAME
#define YANTRA_HACKRF_PLUGIN_NAME "hackrf"
#endif

namespace {

using namespace yantra::soapy;

Profile hackrf_profile()
{
    Profile p;
    p.min_rate_hz = 2e6;
    p.max_rate_hz = 20e6;
    p.default_rate_hz = 2e6;
    p.offset_tuning_hz = 250e3;
    p.scan = true;
    p.c1 = true;
    p.gains = {
        {"LNA", "gain_lna", "LNA (IF) gain",
         "Gain of the low-noise amplifier at the intermediate frequency, in 8 dB steps; raise it "
         "first for weak signals",
         false, "SoapyHackRF does not report the LNA gain element"},
        {"VGA", "gain_vga", "VGA (baseband) gain",
         "Gain of the baseband amplifier, in 2 dB steps; after the LNA", false,
         "SoapyHackRF does not report the VGA gain element"},
        {"AMP", "rf_amplifier", "RF amplifier",
         "The 14 dB amplifier at the antenna input: on for weak signals indoors, off near a site "
         "(it overloads first)",
         true, "SoapyHackRF does not report the AMP gain element"},
    };
    p.args = {
        {"bias_tx", "antenna_power", "Antenna port power",
         "Powers the antenna port (bias tee, 3.3 V, up to 50 mA) for an active antenna or "
         "amplifier. Off by default and after every open and replug.",
         "RF", true, "SoapyHackRF does not offer antenna port power for this device"},
    };
    p.info_sensors = {
        {"", "board_id", "Board", "The board as the module names it (e.g. HackRF One).", false},
        {"version", "firmware_version", "Firmware", "Firmware version of the HackRF.", false},
        {"serial", "serial_number", "Serial", "Serial of the HackRF.", true},
        {"part id", "part_id", "Part id", "Part id of the HackRF's microcontroller.", false},
    };
    p.presets = {
        {"indoor_sensitive", "Indoor, sensitive",
         "Weak signals inside a building: RF amplifier on, LNA 32 dB, VGA 30 dB.",
         {{"rf_amplifier", 1.0}, {"gain_lna", 32.0}, {"gain_vga", 30.0}}},
        {"near_site_strong", "Near site, strong signal",
         "Strong signals near a base station: RF amplifier off, LNA 8 dB, VGA 16 dB, so that "
         "nothing clips.",
         {{"rf_amplifier", 0.0}, {"gain_lna", 8.0}, {"gain_vga", 16.0}}},
    };
    p.saturation_remedy = "switch the RF amplifier off, then lower the LNA gain (preset \"Near site, "
                          "strong signal\")";
    return p;
}

} // namespace

extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const Options options = {
        YANTRA_HACKRF_PLUGIN_NAME,
        "HackRF One",
        "0.2.0",
        "TETRA levels, cell information and band scans with a HackRF One (RAD-60)",
        YANTRA_RADIOS_SOURCE_URL,
        YANTRA_SOAPY_MODULE_TABLE,
        {{"hackrf"}, {}},
        hackrf_profile(),
    };
    return descriptor(options);
}
