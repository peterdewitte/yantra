# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMAKE_USER_MAKE_RULES_OVERRIDE of toolchain-x86_64-w64-mingw32-clang.cmake
# (RAD-139). CMake reads it after Platform/Windows-GNU.cmake has set the
# standard libraries of each language, so the addition below stays.
# winpthreads (-lpthread) goes into every C and C++ link, last, like
# MSYS2's gcc does by itself: clang's MinGW driver leaves it out, and
# libstdc++'s inline thread and mutex code needs it in every program and
# DLL, also those that do not link Threads::Threads.
foreach(_yantra_lang C CXX)
  if(NOT CMAKE_${_yantra_lang}_STANDARD_LIBRARIES_INIT MATCHES "(^| )-lpthread( |$)")
    string(APPEND CMAKE_${_yantra_lang}_STANDARD_LIBRARIES_INIT " -lpthread")
  endif()
endforeach()
unset(_yantra_lang)
