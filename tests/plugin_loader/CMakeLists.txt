# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Plugin loader tests (PLG-09, PLG-03, PLG-10): the C test plugin of
# tests/plugin_api built as plugin modules and loaded through the
# PluginLoader, with variants (wrong API major, failing init, no entry
# point) by compile definition. Every module goes to its own folder of
# the build tree; the tests never use the real home folder.
set(_modules
  ctest_ok ""
  ctest_v2 "CTEST_API_MAJOR=2"
  ctest_initfail "CTEST_INIT_STATUS=YANTRA_ERR_FAILED"
  ctest_noentry "CTEST_NO_ENTRY=1")
set(_defs)
while(_modules)
  list(POP_FRONT _modules _name _def)
  add_library(${_name} MODULE ${PROJECT_SOURCE_DIR}/tests/plugin_api/header_c.c)
  target_include_directories(${_name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/plugin_api)
  target_link_libraries(${_name} PRIVATE yantra::plugin_api)
  if(_def)
    target_compile_definitions(${_name} PRIVATE ${_def})
  endif()
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${_name} PRIVATE -Wall -Wextra -Werror)
  endif()
  set_target_properties(${_name} PROPERTIES PREFIX "" OUTPUT_NAME ctest
    C_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/modules/${_name})
  string(TOUPPER ${_name} _up)
  list(APPEND _defs "${_up}_PATH=\"$<TARGET_FILE:${_name}>\"")
  list(APPEND _deps ${_name})
endwhile()

yantra_add_test_dir(plugin_loader LIBS yantra::core)
add_dependencies(test_plugin_loader ${_deps})
target_compile_definitions(test_plugin_loader PRIVATE ${_defs}
  LOADER_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
