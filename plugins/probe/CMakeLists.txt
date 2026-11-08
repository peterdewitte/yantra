# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The probe feature plugin (PLG-24): the remote API (API-02) through
# CivetWeb 1.16 (MIT, spec 16.2; vendored in third_party/civetweb,
# THIRD_PARTY.md), the fixed-probe summaries (API-03) and the
# remote-probes view. Only civetweb.c and the .inl files it includes are
# built: no CGI, no file serving, no Lua/Duktape, no HTTP/2, no zlib and
# no TLS (HTTPS through a reverse proxy, docs/API.md). The plugin uses
# the public plugin API only and exports only its entry point.
# Copied from yantra-v1 c24e8f5: core/engine/CMakeLists.txt (the CivetWeb
# library), adapted for PLG-24.

add_library(yantra_civetweb STATIC ${PROJECT_SOURCE_DIR}/third_party/civetweb/src/civetweb.c)
target_include_directories(yantra_civetweb SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/civetweb/include)
target_compile_definitions(yantra_civetweb PRIVATE NO_CGI NO_FILES NO_CACHING NO_SSL USE_WEBSOCKET USE_IPV6)
target_compile_options(yantra_civetweb PRIVATE -w)
set_target_properties(yantra_civetweb PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_link_libraries(yantra_civetweb PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# The probe's parts without the plugin tables, for the unit tests.
add_library(yantra_probe_api STATIC api_auth.cpp api_routes.cpp api_server.cpp http_client.cpp live.cpp)
target_include_directories(yantra_probe_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_probe_api PUBLIC yantra::plugin_api yantra::json PRIVATE yantra::warnings yantra_civetweb
                      Threads::Threads)
set_target_properties(yantra_probe_api PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden
                                                  VISIBILITY_INLINES_HIDDEN ON)

add_library(yantra_probe MODULE probe_plugin.cpp)
target_link_libraries(yantra_probe PRIVATE yantra_probe_api yantra::warnings)
target_compile_definitions(yantra_probe PRIVATE YANTRA_PROBE_PLUGIN_VERSION="${PROJECT_VERSION}"
                           YANTRA_SOURCE_URL="${YANTRA_SOURCE_URL}")
set_target_properties(yantra_probe PROPERTIES
  PREFIX ""
  OUTPUT_NAME probe
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib/yantra/plugins)
install(TARGETS yantra_probe LIBRARY DESTINATION lib/yantra/plugins)
