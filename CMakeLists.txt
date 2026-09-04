cmake_minimum_required(VERSION 3.28)

project(AsterCtrl VERSION 0.2.0 LANGUAGES CXX)

# CMake's VERSION grammar is numeric-only. Export the complete SemVer string
# separately so alpha packages cannot be mistaken for the final v0.2.0 release.
set(ASTERCTRL_VERSION_STRING "0.2.0-alpha.1")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ASTER_BUILD_TESTS "Build AsterCtrl tests" ON)
option(ASTER_ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)
option(ASTER_ENABLE_TSAN "Enable the thread sanitizer" OFF)

if(ASTER_ENABLE_SANITIZERS AND ASTER_ENABLE_TSAN)
  message(FATAL_ERROR "Address/undefined sanitizers and TSan are mutually exclusive")
endif()

function(aster_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
    if(ASTER_ENABLE_SANITIZERS)
      target_compile_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=address,undefined)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    elseif(ASTER_ENABLE_TSAN)
      target_compile_options(${target} PRIVATE
        -fno-omit-frame-pointer -fsanitize=thread)
      target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
  endif()
endfunction()

add_library(aster_core STATIC src/core/runtime.cpp)
add_library(aster::core ALIAS aster_core)
set_target_properties(aster_core PROPERTIES EXPORT_NAME core)
target_include_directories(aster_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
aster_target_defaults(aster_core)

set(ASTER_INSTALL_TARGETS aster_core)
if(UNIX)
  find_package(Threads REQUIRED)
  add_library(aster_linux STATIC
    src/platform/linux/plugin_loader.cpp
    src/platform/linux/shutdown_signal.cpp
    src/platform/linux/supervisor.cpp)
  add_library(aster::linux ALIAS aster_linux)
  set_target_properties(aster_linux PROPERTIES EXPORT_NAME linux)
  target_include_directories(aster_linux PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(aster_linux PUBLIC aster::core Threads::Threads
                                      PRIVATE ${CMAKE_DL_LIBS})
  aster_target_defaults(aster_linux)
  list(APPEND ASTER_INSTALL_TARGETS aster_linux)

  add_library(aster_transport STATIC
    src/transports/can/socketcan.cpp
    src/transports/usb/linux_tty.cpp)
  add_library(aster::transport ALIAS aster_transport)
  set_target_properties(aster_transport PROPERTIES EXPORT_NAME transport)
  target_include_directories(aster_transport PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(aster_transport PUBLIC aster::core)
  aster_target_defaults(aster_transport)
  list(APPEND ASTER_INSTALL_TARGETS aster_transport)
endif()

install(TARGETS ${ASTER_INSTALL_TARGETS}
  EXPORT AsterCtrlTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/aster/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aster)
install(EXPORT AsterCtrlTargets
  FILE AsterCtrlTargets.cmake
  NAMESPACE aster::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsterCtrl)

set(ASTERCTRL_CONFIG_DEPENDENCIES "")
if(UNIX)
  set(ASTERCTRL_CONFIG_DEPENDENCIES "find_dependency(Threads)")
endif()
configure_package_config_file(
  src/core/AsterCtrlConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsterCtrl)
if(ASTERCTRL_VERSION_STRING STREQUAL PROJECT_VERSION)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
else()
  # CMake only accepts numeric package requirements. A prerelease must not
  # satisfy a consumer asking for the future stable 0.2.0 (or even 0.2), so
  # versioned discovery is deliberately rejected until the final release.
  configure_file(
    src/core/AsterCtrlPrereleaseConfigVersion.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfigVersion.cmake
    @ONLY)
endif()
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsterCtrl)
if(UNIX AND NOT APPLE)
  install(FILES packaging/systemd/aster-node@.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
endif()

if(ASTER_BUILD_TESTS)
  include(CTest)

  function(aster_add_test name source library)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library})
    target_include_directories(${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/transports)
    aster_target_defaults(${name})
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      # The contract tests intentionally use the standard assert macro. Keep
      # those checks active in Release builds used by architecture and release
      # CI, and avoid compiling their observed values into unused variables.
      target_compile_options(${name} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  aster_add_test(aster_core_ref_test tests/runtime/core_ref_test.cpp aster::core)
  aster_add_test(aster_configuration_test
                 tests/runtime/configuration_test.cpp aster::core)
  aster_add_test(aster_local_channel_test
                 tests/runtime/local_channel_test.cpp aster::core)
  aster_add_test(aster_local_rpc_test tests/runtime/local_rpc_test.cpp aster::core)
  aster_add_test(aster_runtime_lifecycle_test
                 tests/runtime/runtime_lifecycle_test.cpp aster::core)
  aster_add_test(aster_portable_pubsub_test
                 tests/runtime/portable_pubsub_test.cpp aster::core)
  target_include_directories(aster_portable_pubsub_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  aster_add_test(aster_sim_runtime_services_test
                 tests/runtime/sim_runtime_services_test.cpp aster::core)
  aster_add_test(aster_plugin_abi_test
                 tests/runtime/plugin_abi_test.cpp aster::core)

  if(UNIX)
    add_library(aster_test_plugin SHARED tests/linux/test_plugin.cpp)
    target_include_directories(aster_test_plugin PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(aster_test_plugin PRIVATE Threads::Threads)
    set_target_properties(aster_test_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_test_plugin)

    add_library(aster_invalid_plugin SHARED tests/linux/invalid_plugin.cpp)
    target_include_directories(aster_invalid_plugin PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(aster_invalid_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_invalid_plugin)

    add_library(aster_test_core_plugin SHARED tests/linux/test_core_plugin.cpp)
    target_include_directories(aster_test_core_plugin PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(aster_test_core_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_test_core_plugin)

    aster_add_test(aster_linux_thread_executor_test
                   tests/linux/thread_executor_test.cpp aster::linux)
    aster_add_test(aster_linux_plugin_loader_test
                   tests/linux/plugin_loader_test.cpp aster::linux)
    aster_add_test(aster_linux_core_plugin_loader_test
                   tests/linux/core_plugin_loader_test.cpp aster::linux)
    aster_add_test(aster_linux_supervisor_test
                   tests/linux/supervisor_test.cpp aster::linux)
    aster_add_test(aster_linux_shutdown_signal_test
                   tests/linux/shutdown_signal_test.cpp aster::linux)
    aster_add_test(aster_linux_tty_stream_test
                   tests/linux/tty_stream_test.cpp aster::transport)
    aster_add_test(aster_socketcan_vcan_test
                   tests/linux/socketcan_vcan_test.cpp aster::transport)
    target_compile_definitions(aster_linux_plugin_loader_test PRIVATE
      ASTER_TEST_PLUGIN_PATH="$<TARGET_FILE:aster_test_plugin>"
      ASTER_INVALID_PLUGIN_PATH="$<TARGET_FILE:aster_invalid_plugin>")
    add_dependencies(aster_linux_plugin_loader_test
                     aster_test_plugin aster_invalid_plugin)
    target_compile_definitions(aster_linux_core_plugin_loader_test PRIVATE
      ASTER_TEST_CORE_PLUGIN_PATH="$<TARGET_FILE:aster_test_core_plugin>"
      ASTER_TEST_MODULE_PLUGIN_PATH="$<TARGET_FILE:aster_test_plugin>")
    add_dependencies(aster_linux_core_plugin_loader_test
                     aster_test_core_plugin aster_test_plugin)

    aster_add_test(aster_transport_router_test
                   tests/transports/router_test.cpp aster::transport)
    aster_add_test(aster_peer_registry_test
                   tests/transports/peer_registry_test.cpp aster::transport)
    aster_add_test(aster_local_transport_test
                   tests/transports/local_transport_test.cpp aster::transport)
    aster_add_test(aster_can_protocol_test
                   tests/transports/can_protocol_test.cpp aster::transport)
    aster_add_test(aster_can_link_control_test
                   tests/transports/can_link_control_test.cpp aster::transport)
    aster_add_test(aster_can_channel_bridge_test
                   tests/transports/can_channel_bridge_test.cpp aster::transport)
    aster_add_test(aster_can_rpc_bridge_test
                   tests/transports/can_rpc_bridge_test.cpp aster::transport)
    if(ASTER_ENABLE_TSAN)
      # TSan supplies its own global allocation interceptors. Ordinary and
      # ASan builds retain the no-allocation assertions; TSan still exercises
      # the protocol behavior while using the sanitizer interceptors.
      target_compile_definitions(aster_can_protocol_test PRIVATE
        ASTER_TEST_DISABLE_ALLOCATION_TRACKER=1)
      target_compile_definitions(aster_can_link_control_test PRIVATE
        ASTER_TEST_DISABLE_ALLOCATION_TRACKER=1)
      target_compile_definitions(aster_can_rpc_bridge_test PRIVATE
        ASTER_TEST_DISABLE_ALLOCATION_TRACKER=1)
    endif()
    aster_add_test(aster_usb_framing_test
                   tests/transports/usb_framing_test.cpp aster::transport)
    aster_add_test(aster_usb_stream_transport_test
                   tests/transports/usb_stream_transport_test.cpp
                   aster::transport)
  endif()
endif()
