cmake_minimum_required(VERSION 3.22)

project(xrobot_transports VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XROBOT_TRANSPORTS_BUILD_TESTS "Build xrobot-transports tests" ON)

add_library(xrobot_transports INTERFACE)
add_library(xrobot::transports ALIAS xrobot_transports)

target_include_directories(
  xrobot_transports
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(NOT TARGET xrobot::runtime)
  set(XROBOT_RUNTIME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xrobot-runtime"
      CACHE PATH "Path to xrobot-runtime")
  set(XROBOT_RUNTIME_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${XROBOT_RUNTIME_SOURCE_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/xrobot-runtime")
endif()
target_link_libraries(xrobot_transports INTERFACE xrobot::runtime)

if(XROBOT_TRANSPORTS_BUILD_TESTS)
  include(CTest)
  add_executable(xrobot_can_protocol_test tests/can_protocol_test.cpp)
  target_link_libraries(xrobot_can_protocol_test PRIVATE xrobot::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_can_protocol_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_can_protocol_test COMMAND xrobot_can_protocol_test)

  add_executable(xrobot_can_topic_bridge_test tests/can_topic_bridge_test.cpp)
  target_link_libraries(xrobot_can_topic_bridge_test PRIVATE xrobot::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_can_topic_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_can_topic_bridge_test COMMAND xrobot_can_topic_bridge_test)

  add_executable(xrobot_can_service_bridge_test tests/can_service_bridge_test.cpp)
  target_link_libraries(xrobot_can_service_bridge_test PRIVATE xrobot::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_can_service_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_can_service_bridge_test COMMAND xrobot_can_service_bridge_test)

  add_executable(xrobot_can_action_bridge_test tests/can_action_bridge_test.cpp)
  target_link_libraries(xrobot_can_action_bridge_test PRIVATE xrobot::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_can_action_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_can_action_bridge_test COMMAND xrobot_can_action_bridge_test)

  add_executable(xrobot_can_link_control_test tests/can_link_control_test.cpp)
  target_link_libraries(xrobot_can_link_control_test PRIVATE xrobot::transports)
  target_include_directories(xrobot_can_link_control_test
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_can_link_control_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_can_link_control_test COMMAND xrobot_can_link_control_test)
endif()
