# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(asterctrl_zephyr_runtime_smoke LANGUAGES CXX)

zephyr_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics)
target_sources(app PRIVATE src/main.cpp)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../..)
