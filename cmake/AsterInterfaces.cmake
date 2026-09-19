add_library(aster_module_c_interface INTERFACE)
add_library(aster::module_c_interface ALIAS aster_module_c_interface)
set_target_properties(aster_module_c_interface PROPERTIES
  EXPORT_NAME module_c_interface)
target_include_directories(aster_module_c_interface INTERFACE
  $<BUILD_INTERFACE:${ASTER_INTERFACE_ROOT}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
file(GLOB_RECURSE ASTER_C_INTERFACE_HEADERS CONFIGURE_DEPENDS
  ${ASTER_C_INTERFACE_DIR}/*.h)
target_sources(aster_module_c_interface INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${ASTER_INTERFACE_ROOT}
  FILES ${ASTER_C_INTERFACE_HEADERS})

add_library(aster_module_cpp_interface INTERFACE)
add_library(aster::module_cpp_interface ALIAS aster_module_cpp_interface)
set_target_properties(aster_module_cpp_interface PROPERTIES
  EXPORT_NAME module_cpp_interface)
target_include_directories(aster_module_cpp_interface INTERFACE
  $<BUILD_INTERFACE:${ASTER_INTERFACE_ROOT}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
file(GLOB_RECURSE ASTER_CPP_INTERFACE_HEADERS CONFIGURE_DEPENDS
  ${ASTER_CPP_INTERFACE_DIR}/*.hpp)
target_sources(aster_module_cpp_interface INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${ASTER_INTERFACE_ROOT}
  FILES ${ASTER_CPP_INTERFACE_HEADERS})
target_link_libraries(aster_module_cpp_interface INTERFACE
  aster::module_c_interface)
target_compile_features(aster_module_cpp_interface INTERFACE cxx_std_20)

add_library(aster_runtime_interface INTERFACE)
add_library(aster::runtime_interface ALIAS aster_runtime_interface)
set_target_properties(aster_runtime_interface PROPERTIES EXPORT_NAME runtime_interface)
target_include_directories(aster_runtime_interface INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/runtime>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aster_runtime_interface INTERFACE aster::module_cpp_interface)
file(GLOB_RECURSE ASTER_RUNTIME_HEADERS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/aster_runtime/*.hpp)
target_sources(aster_runtime_interface INTERFACE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime FILES ${ASTER_RUNTIME_HEADERS})

add_library(aster_pkg_c_interface INTERFACE)
add_library(aster::pkg_c_interface ALIAS aster_pkg_c_interface)
set_target_properties(aster_pkg_c_interface PROPERTIES
  EXPORT_NAME pkg_c_interface)
target_include_directories(aster_pkg_c_interface INTERFACE
  $<BUILD_INTERFACE:${ASTER_INTERFACE_ROOT}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
file(GLOB_RECURSE ASTER_PKG_INTERFACE_HEADERS CONFIGURE_DEPENDS
  ${ASTER_PKG_INTERFACE_DIR}/*.h
  ${ASTER_PKG_INTERFACE_DIR}/*.hpp)
target_sources(aster_pkg_c_interface INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${ASTER_INTERFACE_ROOT}
  FILES ${ASTER_PKG_INTERFACE_HEADERS})
target_link_libraries(aster_pkg_c_interface INTERFACE
  aster::module_c_interface)

add_library(aster_core_plugin_interface INTERFACE)
add_library(aster::core_plugin_interface ALIAS aster_core_plugin_interface)
set_target_properties(aster_core_plugin_interface PROPERTIES
  EXPORT_NAME core_plugin_interface)
target_include_directories(aster_core_plugin_interface INTERFACE
  $<BUILD_INTERFACE:${ASTER_INTERFACE_ROOT}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
file(GLOB_RECURSE ASTER_CORE_PLUGIN_INTERFACE_HEADERS CONFIGURE_DEPENDS
  ${ASTER_CORE_PLUGIN_INTERFACE_DIR}/*.h)
target_sources(aster_core_plugin_interface INTERFACE
  FILE_SET HEADERS
  BASE_DIRS ${ASTER_INTERFACE_ROOT}
  FILES ${ASTER_CORE_PLUGIN_INTERFACE_HEADERS})
target_link_libraries(aster_core_plugin_interface INTERFACE
  aster::module_c_interface)
