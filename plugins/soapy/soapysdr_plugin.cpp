// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generic SoapySDR plugin (RAD-62): every SoapySDR module compiled into
// it (RAD-136) except the drivers with a plugin of their own (hackrf,
// RAD-60), on the shared layer (RAD-54). Its settings come from the
// module (RAD-51). The test build (soapysdr_fake) takes the fake module
// only (RAD-92).
#include <yantra/plugins/soapy_plugin.hpp>

#ifndef YANTRA_SOAPY_PLUGIN_NAME
#define YANTRA_SOAPY_PLUGIN_NAME "soapysdr"
#endif
#ifndef YANTRA_SOAPY_ONLY
#define YANTRA_SOAPY_ONLY
#endif

extern "C" YR_EXPORT const yr_descriptor* yantra_radio_entry(void)
{
    static const yantra::soapy::Options options = {
        YANTRA_SOAPY_PLUGIN_NAME,
        "SDR receiver (SoapySDR)",
        "0.1.2",
        "TETRA levels and cell information with an SDR through its SoapySDR module (RAD-62)",
        YANTRA_RADIOS_SOURCE_URL,
        YANTRA_SOAPY_MODULE_TABLE,
        {{YANTRA_SOAPY_ONLY}, {"hackrf"}},
    };
    return yantra::soapy::descriptor(options);
}
