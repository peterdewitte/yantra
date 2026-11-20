# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# radio-common/: code shared by the radio plugins and radio tools only
# (PLG-29, RAD-02, RAD-141), never by the application (PLG-30):
#   radios_common/  record files (RAD-65), scene JSON (RAD-36), TETRA
#                   record schemas: yantra::radios_common
#   receiver/       DSP, carrier search, TETRA decoder (osmo-tetra lower
#                   layers), level and quality meters (RAD-100..115) and
#                   the Rust protocol crate receiver/rust (RAD-101)
#   tetra_common/   glue of the TETRA radio plugins: yantra::tetra_plugin
#   soapy_common/   the SoapySDR layer of the SDR plugins (RAD-54):
#                   yantra::soapy_plugin
# All static libraries, linked into the self-contained plugins (RAD-132).
yantra_radio_directory()
add_subdirectory(radios_common)
add_subdirectory(receiver)
# The TETRA plugin glue needs the measurement engine; the SoapySDR layer
# needs the TETRA glue.
add_subdirectory(tetra_common)
add_subdirectory(soapy_common)
