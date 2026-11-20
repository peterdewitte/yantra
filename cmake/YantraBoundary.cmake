# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio boundary in the build (PLG-30, RAD-141). The CMake targets
# express it: app/ and core/ link the two API targets (yantra::radio_api,
# yantra::plugin_api) and never a plugin, radio-common or gen target; the
# radio plugins, radio-common, gen and the radio tools link
# yantra::radio_api and their own targets, never yantra::core, an app
# target or yantra::plugin_api. Nothing sets include folders for the whole
# tree, so a target only sees the headers of what it links.
#
# yantra_boundary_write(), called at the end of the top-level
# CMakeLists.txt, writes every target of the build with its folder and its
# direct link dependencies (aliases resolved) to
# <build>/boundary/targets.tsv; the ctest boundary_lint
# (tools/ci/lint_boundary.py) checks that file and every #include of the
# sources, both ways, and boundary_lint_selftest proves that the lint
# finds the violations of tests/lint/boundary_fixture.

function(_yantra_boundary_targets dir out)
  get_property(_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_imported DIRECTORY ${dir} PROPERTY IMPORTED_TARGETS)
  set(_result)
  foreach(_t IN LISTS _targets _imported)
    list(APPEND _result "${_t}|${dir}")
  endforeach()
  get_property(_subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(_s IN LISTS _subs)
    _yantra_boundary_targets(${_s} _more)
    list(APPEND _result ${_more})
  endforeach()
  set(${out} ${_result} PARENT_SCOPE)
endfunction()

function(_yantra_boundary_resolve item out)
  set(${out} "" PARENT_SCOPE)
  if(TARGET "${item}")
    get_target_property(_aliased "${item}" ALIASED_TARGET)
    if(_aliased)
      set(${out} ${_aliased} PARENT_SCOPE)
    else()
      set(${out} ${item} PARENT_SCOPE)
    endif()
  endif()
endfunction()

function(yantra_boundary_write)
  _yantra_boundary_targets(${PROJECT_SOURCE_DIR} _all)
  set(_lines "# target\tfolder (relative to the source folder)\tdirect link dependencies (PLG-30)\n")
  foreach(_entry IN LISTS _all)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _t)
    list(GET _entry 1 _dir)
    if(NOT TARGET ${_t})
      continue() # an imported target seen only in its own folder (third-party)
    endif()
    file(RELATIVE_PATH _rel ${PROJECT_SOURCE_DIR} ${_dir})
    set(_items)
    get_target_property(_type ${_t} TYPE)
    if(NOT _type STREQUAL "INTERFACE_LIBRARY" OR CMAKE_VERSION VERSION_GREATER_EQUAL 3.19)
      get_target_property(_link ${_t} LINK_LIBRARIES)
      if(_link)
        list(APPEND _items ${_link})
      endif()
    endif()
    get_target_property(_ilink ${_t} INTERFACE_LINK_LIBRARIES)
    if(_ilink)
      list(APPEND _items ${_ilink})
    endif()
    # Names inside generator expressions ($<LINK_ONLY:x>, $<$<BOOL:..>:x>)
    # count too: every word that names a target.
    set(_deps)
    foreach(_item IN LISTS _items)
      string(REGEX MATCHALL "[A-Za-z0-9_.+:-]+" _words "${_item}")
      foreach(_w IN LISTS _words)
        _yantra_boundary_resolve("${_w}" _r)
        if(_r AND NOT _r STREQUAL _t)
          list(APPEND _deps ${_r})
        endif()
      endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _deps)
    string(APPEND _lines "${_t}\t${_rel}\t${_deps}\n")
  endforeach()
  file(WRITE ${PROJECT_BINARY_DIR}/boundary/targets.tsv "${_lines}")
endfunction()

# The two ctests (label lint, area lint); they read the sources and the
# file above, so they run on the build machine (build_host).
function(yantra_boundary_tests)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME boundary_lint
           COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/ci/lint_boundary.py
                   --targets ${PROJECT_BINARY_DIR}/boundary/targets.tsv ${PROJECT_SOURCE_DIR})
  add_test(NAME boundary_lint_selftest
           COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/ci/lint_boundary.py
                   --self-test ${PROJECT_SOURCE_DIR}/tests/lint/boundary_fixture)
  set_tests_properties(boundary_lint boundary_lint_selftest PROPERTIES LABELS "fast;lint" TIMEOUT 120)
  yantra_build_host_tests(boundary_lint boundary_lint_selftest)
endfunction()
