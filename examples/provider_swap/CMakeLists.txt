# Copyright (c) 2026 AsterCtrl contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
project(aster_provider_swap_example LANGUAGES CXX)

if(ASTERCTRL_SOURCE_DIR)
  set(ASTER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ASTERCTRL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/asterctrl"
                   EXCLUDE_FROM_ALL)
else()
  find_package(AsterCtrl 0.2 CONFIG REQUIRED)
endif()

if(NOT ASTER_GENERATED_DIR)
  message(FATAL_ERROR "run aster codegen and set ASTER_GENERATED_DIR")
endif()

set(ASTER_MODULE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
include("${ASTER_GENERATED_DIR}/aster.generated.cmake")

add_executable(aster_provider_sim main.cpp)
target_compile_features(aster_provider_sim PRIVATE cxx_std_20)
target_include_directories(aster_provider_sim PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${ASTER_GENERATED_DIR}")
target_link_libraries(aster_provider_sim PRIVATE aster_generated aster::core)

include(CTest)
add_test(NAME aster_provider_sim_runs COMMAND aster_provider_sim)
