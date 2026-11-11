# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Plugin API header tests (PLG-03, PLG-04): C11 and C++20 translation
# units with the ABI layout asserts, a complete plugin written in C, and
# the plugin.hpp helpers, always with -Werror; and a syntax check of the
# public headers in the oldest language modes a plugin may use (C99 and
# C++11 for plugin.h, also next to radio.h; C++20 for plugin.hpp) with
# -pedantic. Copied from yantra-radios fb194f5: tests/radio_api/
# CMakeLists.txt, adapted for the plugin API.
yantra_add_test_dir(plugin_api LIBS yantra::plugin_api)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(test_plugin_api PRIVATE -Wall -Wextra -Werror)
  set(_inc ${PROJECT_SOURCE_DIR}/include)
  set(_radio_inc ${YANTRA_RADIOS_DIR}/include)
  set(_dir ${CMAKE_CURRENT_SOURCE_DIR}/syntax)
  add_test(NAME plugin_api_header_c99
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_C_COMPILER} -std=c99 -pedantic -Wall -Wextra -Werror -Wstrict-prototypes
            -Wmissing-prototypes -Wundef -fsyntax-only -I${_inc} -I${_radio_inc}
            -x c ${_dir}/plugin_h.c)
  add_test(NAME plugin_api_header_cxx11
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_CXX_COMPILER} -std=c++11 -pedantic -Wall -Wextra -Werror -Wundef
            -fsyntax-only -I${_inc} -I${_radio_inc} -x c++ ${_dir}/plugin_h.c)
  add_test(NAME plugin_api_header_hpp
    COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_CXX_COMPILER} -std=c++20 -pedantic -Wall -Wextra -Werror -Wundef -Wshadow
            -Wconversion -fsyntax-only -I${_inc} -x c++ ${_dir}/plugin_hpp.cpp)
  set_tests_properties(plugin_api_header_c99 plugin_api_header_cxx11 plugin_api_header_hpp
    PROPERTIES LABELS fast)
endif()

# Checks of the sources and the build machine's tools: build_host in a
# cross-compiled tree (top-level CMakeLists.txt).
foreach(_t plugin_api_header_c99 plugin_api_header_cxx11 plugin_api_header_hpp)
  if(TEST ${_t})
    yantra_build_host_tests(${_t})
  endif()
endforeach()
