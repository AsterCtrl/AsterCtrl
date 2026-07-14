cmake_minimum_required(VERSION 3.22)

project(xrobot_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XROBOT_RUNTIME_BUILD_TESTS "Build xrobot-runtime tests" ON)

add_library(xrobot_runtime STATIC src/runtime.cpp)
add_library(xrobot::runtime ALIAS xrobot_runtime)

target_include_directories(
  xrobot_runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(XROBOT_RUNTIME_BUILD_TESTS)
  include(CTest)
  add_executable(xrobot_runtime_smoke_test tests/smoke_test.cpp)
  target_link_libraries(xrobot_runtime_smoke_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_smoke_test COMMAND xrobot_runtime_smoke_test)

  add_executable(xrobot_runtime_lifecycle_test tests/runtime_lifecycle_test.cpp)
  target_link_libraries(xrobot_runtime_lifecycle_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_lifecycle_test COMMAND xrobot_runtime_lifecycle_test)
endif()
