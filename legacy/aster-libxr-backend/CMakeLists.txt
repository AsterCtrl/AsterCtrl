cmake_minimum_required(VERSION 3.22)

project(xrobot_libxr_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XROBOT_LIBXR_BACKEND_BUILD_TESTS
       "Build xrobot-libxr-backend host tests" ON)

if(NOT TARGET xrobot::transports)
  set(XROBOT_TRANSPORTS_SOURCE_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/../xrobot-transports"
      CACHE PATH "Path to xrobot-transports")
  set(XROBOT_TRANSPORTS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${XROBOT_TRANSPORTS_SOURCE_DIR}"
                   "${CMAKE_CURRENT_BINARY_DIR}/xrobot-transports")
endif()

if(NOT TARGET xr)
  set(LIBXR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libxr"
      CACHE PATH "Path to libxr")
  set(LIBXR_SYSTEM none CACHE STRING "" FORCE)
  set(LIBXR_DRIVER "" CACHE STRING "" FORCE)
  set(LIBXR_NO_EIGEN ON CACHE BOOL "" FORCE)
  set(LIBXR_STATIC_BUILD ON CACHE BOOL "" FORCE)
  add_subdirectory("${LIBXR_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/libxr")
endif()

get_target_property(_libxr_include_dirs xr INTERFACE_INCLUDE_DIRECTORIES)
if(_libxr_include_dirs)
  set_property(TARGET xr PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
               "${_libxr_include_dirs}")
endif()

add_library(xrobot_libxr_backend INTERFACE)
add_library(xrobot::libxr-backend ALIAS xrobot_libxr_backend)

target_include_directories(
  xrobot_libxr_backend
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(xrobot_libxr_backend INTERFACE xrobot::transports xr)

if(XROBOT_LIBXR_BACKEND_BUILD_TESTS)
  include(CTest)
  add_executable(xrobot_libxr_classic_can_endpoint_test
                 tests/classic_can_endpoint_test.cpp)
  target_link_libraries(xrobot_libxr_classic_can_endpoint_test
                        PRIVATE xrobot::libxr-backend)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_libxr_classic_can_endpoint_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_libxr_classic_can_endpoint_test
           COMMAND xrobot_libxr_classic_can_endpoint_test)

  add_executable(xrobot_libxr_can_adapter_test tests/can_adapter_test.cpp)
  target_link_libraries(xrobot_libxr_can_adapter_test
                        PRIVATE xrobot::libxr-backend)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_libxr_can_adapter_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_libxr_can_adapter_test
           COMMAND xrobot_libxr_can_adapter_test)

  add_executable(xrobot_libxr_uart_reader_adapter_test
                 tests/uart_reader_adapter_test.cpp)
  target_link_libraries(xrobot_libxr_uart_reader_adapter_test
                        PRIVATE xrobot::libxr-backend)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xrobot_libxr_uart_reader_adapter_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME xrobot_libxr_uart_reader_adapter_test
           COMMAND xrobot_libxr_uart_reader_adapter_test)
endif()
