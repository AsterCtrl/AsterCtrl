# Copyright (c) 2026 AsterCtrl contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT ASTER_GENERATED_DIR)
  message(FATAL_ERROR "run 'aster codegen' and set ASTER_GENERATED_DIR")
endif()

set(ASTER_MODULE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
include("${ASTER_GENERATED_DIR}/aster.generated.cmake")

if(ASTER_ZEPHYR_BUILD)
  target_sources(app PRIVATE "${CMAKE_CURRENT_LIST_DIR}/generated_node_main.cpp")
  target_include_directories(app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ASTER_GENERATED_DIR}")
else()
  if(NOT ASTERCTRL_SOURCE_DIR)
    get_filename_component(ASTERCTRL_SOURCE_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
  endif()
  set(ASTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ASTERCTRL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/asterctrl"
                   EXCLUDE_FROM_ALL)

  add_executable(aster_generated_node
    "${CMAKE_CURRENT_LIST_DIR}/generated_node_main.cpp")
  target_compile_features(aster_generated_node PRIVATE cxx_std_20)
  target_include_directories(aster_generated_node PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ASTER_GENERATED_DIR}")
  target_link_libraries(aster_generated_node PRIVATE aster_generated aster::core)

  include(CTest)
  add_test(NAME aster_generated_node_runs COMMAND aster_generated_node)
endif()
