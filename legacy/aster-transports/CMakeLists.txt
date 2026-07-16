cmake_minimum_required(VERSION 3.22)

project(aster_transports VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ASTER_TRANSPORTS_BUILD_TESTS "Build aster-transports tests" ON)

add_library(aster_transports INTERFACE)
add_library(aster::transports ALIAS aster_transports)

target_include_directories(
  aster_transports
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(NOT TARGET aster::runtime)
  set(ASTER_RUNTIME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../aster-runtime"
      CACHE PATH "Path to aster-runtime")
  set(ASTER_RUNTIME_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ASTER_RUNTIME_SOURCE_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/aster-runtime")
endif()
target_link_libraries(aster_transports INTERFACE aster::runtime)

if(ASTER_TRANSPORTS_BUILD_TESTS)
  include(CTest)
  add_executable(aster_can_protocol_test tests/can_protocol_test.cpp)
  target_link_libraries(aster_can_protocol_test PRIVATE aster::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_can_protocol_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_can_protocol_test COMMAND aster_can_protocol_test)

  add_executable(aster_can_topic_bridge_test tests/can_topic_bridge_test.cpp)
  target_link_libraries(aster_can_topic_bridge_test PRIVATE aster::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_can_topic_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_can_topic_bridge_test COMMAND aster_can_topic_bridge_test)

  add_executable(aster_can_service_bridge_test tests/can_service_bridge_test.cpp)
  target_link_libraries(aster_can_service_bridge_test PRIVATE aster::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_can_service_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_can_service_bridge_test COMMAND aster_can_service_bridge_test)

  add_executable(aster_can_action_bridge_test tests/can_action_bridge_test.cpp)
  target_link_libraries(aster_can_action_bridge_test PRIVATE aster::transports)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_can_action_bridge_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_can_action_bridge_test COMMAND aster_can_action_bridge_test)

  add_executable(aster_can_link_control_test tests/can_link_control_test.cpp)
  target_link_libraries(aster_can_link_control_test PRIVATE aster::transports)
  target_include_directories(aster_can_link_control_test
                             PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_can_link_control_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_can_link_control_test COMMAND aster_can_link_control_test)
endif()
