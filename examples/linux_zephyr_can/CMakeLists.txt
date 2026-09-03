# Copyright (c) 2026 AsterCtrl contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

if(ASTER_ZEPHYR_BUILD)
  find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
  project(aster_can_generated_node LANGUAGES CXX)
  zephyr_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics)
else()
  project(aster_can_generated_node LANGUAGES CXX)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../common/cross_node.cmake")
