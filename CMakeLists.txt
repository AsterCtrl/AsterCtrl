cmake_minimum_required(VERSION 3.28)

project(AsterCtrl VERSION 0.2.0 LANGUAGES C CXX)

# CMake's VERSION grammar is numeric-only. Export the complete SemVer string
# separately so alpha packages cannot be mistaken for the final v0.2.0 release.
set(ASTERCTRL_VERSION_STRING "0.2.0-alpha.1")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ASTER_BUILD_TESTS "Build AsterCtrl tests" ON)
option(ASTER_ENABLE_SANITIZERS "Enable address and undefined sanitizers" OFF)
option(ASTER_ENABLE_TSAN "Enable the thread sanitizer" OFF)

set(ASTER_C_INTERFACE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/aster_module_c_interface)
set(ASTER_CPP_INTERFACE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/aster_module_cpp_interface)
set(ASTER_PKG_INTERFACE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/aster_pkg_c_interface)
set(ASTER_CORE_PLUGIN_INTERFACE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/aster_core_plugin_interface)
set(ASTER_INTERFACE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/interface)

if(ASTER_ENABLE_SANITIZERS AND ASTER_ENABLE_TSAN)
  message(FATAL_ERROR "Address/undefined sanitizers and TSan are mutually exclusive")
endif()

include(cmake/AsterBuildOptions.cmake)


include(cmake/AsterInterfaces.cmake)
include(cmake/AsterPackage.cmake)

add_library(aster_core STATIC
  src/core/execution.cpp
  src/core/service/allocator.cpp
  src/core/service/channel.cpp
  src/core/service/clock.cpp
  src/core/service/configurator.cpp
  src/core/service/executor.cpp
  src/core/service/hardware.cpp
  src/core/service/logger.cpp
  src/core/service/parameter.cpp
  src/core/service/rpc.cpp
  src/core/core_adapter.cpp
  src/core/runtime.cpp)
add_library(aster::core ALIAS aster_core)
set_target_properties(aster_core PROPERTIES
  EXPORT_NAME core
  POSITION_INDEPENDENT_CODE ON)
target_link_libraries(aster_core PUBLIC aster::runtime_interface)
aster_target_defaults(aster_core)

set(ASTER_INSTALL_TARGETS
  aster_runtime_interface
  aster_module_c_interface
  aster_module_cpp_interface
  aster_pkg_c_interface
  aster_core_plugin_interface
  aster_core)
if(UNIX)
  include(cmake/GetYamlCpp.cmake)
  target_sources(aster_core PRIVATE src/platform/linux/execution.cpp)
  find_package(Threads REQUIRED)
  add_library(aster_linux STATIC
    src/platform/linux/channel_manager.cpp
    src/platform/linux/configuration.cpp
    src/platform/linux/node_runtime.cpp
    src/platform/linux/rpc_manager.cpp
    src/platform/linux/plugin_loader.cpp
    src/platform/linux/shutdown_signal.cpp
    src/platform/linux/supervisor.cpp)
  add_library(aster::linux ALIAS aster_linux)
  set_target_properties(aster_linux PROPERTIES EXPORT_NAME linux)
  target_link_libraries(aster_linux PUBLIC
    aster::core
    aster::core_plugin_interface
    Threads::Threads
    PRIVATE ${CMAKE_DL_LIBS} yaml-cpp::yaml-cpp)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # GNU dialects predefine `linux` as a macro. Keep the public
    # aster::platform::linux namespace usable by downstream targets even when
    # they have not disabled compiler extensions themselves.
    target_compile_options(aster_linux INTERFACE -Ulinux)
  endif()
  aster_target_defaults(aster_linux)
  list(APPEND ASTER_INSTALL_TARGETS aster_linux)

  add_executable(aster_runtime src/platform/linux/main.cpp)
  target_link_libraries(aster_runtime PRIVATE aster::linux)
  aster_target_defaults(aster_runtime)
  list(APPEND ASTER_INSTALL_TARGETS aster_runtime)

  add_library(aster_transport STATIC
    src/transports/can/socketcan.cpp
    src/transports/usb/linux_tty.cpp)
  add_library(aster::transport ALIAS aster_transport)
  set_target_properties(aster_transport PROPERTIES EXPORT_NAME transport)
  target_link_libraries(aster_transport PUBLIC aster::core)
  aster_target_defaults(aster_transport)
  list(APPEND ASTER_INSTALL_TARGETS aster_transport)
endif()

include(cmake/AsterInstall.cmake)
include(cmake/AsterTests.cmake)
