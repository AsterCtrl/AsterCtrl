cmake_minimum_required(VERSION 3.22)

project(xrobot_runtime VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XROBOT_RUNTIME_BUILD_TESTS "Build xrobot-runtime tests" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

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

  add_executable(xrobot_runtime_executor_test tests/executor_test.cpp)
  target_link_libraries(xrobot_runtime_executor_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_executor_test COMMAND xrobot_runtime_executor_test)

  add_executable(xrobot_runtime_periodic_scheduler_test
                 tests/periodic_scheduler_test.cpp)
  target_link_libraries(xrobot_runtime_periodic_scheduler_test
                        PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_periodic_scheduler_test
           COMMAND xrobot_runtime_periodic_scheduler_test)

  add_executable(xrobot_runtime_topic_test tests/topic_test.cpp)
  target_link_libraries(xrobot_runtime_topic_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_topic_test COMMAND xrobot_runtime_topic_test)

  add_executable(xrobot_runtime_service_test tests/service_test.cpp)
  target_link_libraries(xrobot_runtime_service_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_service_test COMMAND xrobot_runtime_service_test)

  add_executable(xrobot_runtime_action_test tests/action_test.cpp)
  target_link_libraries(xrobot_runtime_action_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_action_test COMMAND xrobot_runtime_action_test)

  add_executable(xrobot_runtime_parameter_test tests/parameter_test.cpp)
  target_link_libraries(xrobot_runtime_parameter_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_parameter_test COMMAND xrobot_runtime_parameter_test)

  add_executable(xrobot_runtime_module_context_test tests/module_context_test.cpp)
  target_link_libraries(xrobot_runtime_module_context_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_module_context_test COMMAND xrobot_runtime_module_context_test)

  add_executable(xrobot_runtime_legacy_adapter_test tests/legacy_adapter_test.cpp)
  target_link_libraries(xrobot_runtime_legacy_adapter_test PRIVATE xrobot::runtime)
  add_test(NAME xrobot_runtime_legacy_adapter_test COMMAND xrobot_runtime_legacy_adapter_test)
endif()
