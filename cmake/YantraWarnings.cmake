# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra::warnings - interface target with the warning flags for Yantra's
# own code, the radio parts' included (PLG-29). Third-party code
# (third_party/) does not link it, so its warnings do not leak into our
# build. The build
# must stay warning-free (OA-02); YANTRA_WERROR (ON in CI, TST-02) makes
# every warning an error.
# Copied from yantra-radios fb194f5: cmake/YantraWarnings.cmake (itself
# from yantra-v2 68ea20a).

add_library(yantra_warnings INTERFACE)
add_library(yantra::warnings ALIAS yantra_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(yantra_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wformat=2
    -Wimplicit-fallthrough -Wundef -Wnull-dereference
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor -Woverloaded-virtual>
    $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes -Wmissing-prototypes>)
  if(YANTRA_WERROR)
    target_compile_options(yantra_warnings INTERFACE -Werror)
  endif()
endif()
