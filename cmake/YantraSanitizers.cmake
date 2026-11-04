# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Developer sanitizer builds (TST-05), selected with YANTRA_SANITIZER
# (copied from yantra-radios fb194f5: cmake/YantraSanitizers.cmake, itself
# from yantra-v2 68ea20a):
#   asan-ubsan  AddressSanitizer + UndefinedBehaviorSanitizer
#   tsan        ThreadSanitizer
# The flags apply to the whole tree (also third-party code), as the
# sanitizer runtimes require. UBSan errors abort, so ctest sees them.

if(YANTRA_SANITIZER STREQUAL "")
  return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  message(FATAL_ERROR "YANTRA_SANITIZER needs gcc or clang")
endif()

if(YANTRA_SANITIZER STREQUAL "asan-ubsan")
  set(_yantra_san -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(YANTRA_SANITIZER STREQUAL "tsan")
  set(_yantra_san -fsanitize=thread)
else()
  message(FATAL_ERROR "Unknown YANTRA_SANITIZER '${YANTRA_SANITIZER}' "
                      "(expected asan-ubsan or tsan)")
endif()

add_compile_options(${_yantra_san} -fno-omit-frame-pointer)
add_link_options(${_yantra_san})
unset(_yantra_san)
