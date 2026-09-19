include_guard(GLOBAL)

# Export metadata lives in one Manifest; user targets add their own dependencies.
# Module Packages link only the SDK, never the Runtime implementation.
function(aster_add_package target)
  cmake_parse_arguments(PARSE_ARGV 1 PKG "" "MANIFEST" "")
  if(NOT PKG_MANIFEST OR PKG_UNPARSED_ARGUMENTS OR PKG_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "usage: aster_add_package(target MANIFEST package.yaml)")
  endif()
  if(DEFINED ZEPHYR_BASE)
    message(FATAL_ERROR "aster_add_package currently generates Linux dynamic Packages only")
  endif()
  find_package(Python3 3.12...<3.13 REQUIRED COMPONENTS Interpreter)
  get_filename_component(manifest "${PKG_MANIFEST}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(package_dir "${manifest}" DIRECTORY)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/${target}.generated")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
  execute_process(
    # Isolate imports from the application working directory and PYTHONPATH.
    COMMAND "${Python3_EXECUTABLE}" -I -m aster_cli.cli codegen
      --package "${manifest}" --output "${generated}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Package generation failed. Activate the aster_cli environment.\n${output}${error}")
  endif()
  include("${generated}/package.generated.cmake")
  add_library(${target} MODULE ${ASTER_PACKAGE_SOURCE_FILES})
  target_include_directories(${target} PRIVATE "${package_dir}")
  target_link_libraries(${target} PRIVATE aster::module_cpp_interface aster::pkg_c_interface)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
endfunction()
