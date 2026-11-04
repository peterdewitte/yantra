# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The example radio plugin "beacon" (RAD-64): a minimal plugin for the
# made-up beacon technology. It makes its own samples (beacon_gen.c),
# measures them (beacon_demod.c) and delivers beacon records. Built like
# every plugin with yantra_add_radio_plugin(), which also gives it its
# conformance test (conformance_beacon, RAD-91); docs/RADIO_PLUGINS.md
# builds it step by step.
yantra_add_radio_plugin(beacon
  SOURCES beacon_plugin.cpp beacon_demod.c beacon_gen.c beacon_proto.c)
target_compile_definitions(beacon PRIVATE
  YANTRA_RADIOS_SOURCE_URL="${YANTRA_RADIOS_SOURCE_URL}")
if(NOT WIN32)
  target_link_libraries(beacon PRIVATE m)
endif()
