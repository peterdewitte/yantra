# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio API header of the pinned yantra-radios submodule (PLG-26)
# compiles in Yantra's build as C11 and C++20 and is of major version 1.
yantra_add_test_dir(radio_api LIBS yantra::radio_api)
target_compile_definitions(test_radio_api PRIVATE
  YANTRA_TEST_RADIO_API_MAJOR=${YANTRA_RADIO_API_VERSION_MAJOR}
  YANTRA_TEST_RADIO_API_MINOR=${YANTRA_RADIO_API_VERSION_MINOR})
