include_guard(GLOBAL)
include(FetchContent)

# Host-only dependency. FETCHCONTENT_SOURCE_DIR_YAML_CPP supports a pre-fetched
# source tree; FetchContent's cache can be reused with networking disabled.
set(YAML_CPP_BUILD_TESTS OFF)
set(YAML_CPP_BUILD_TOOLS OFF)
set(YAML_CPP_BUILD_CONTRIB OFF)
set(YAML_CPP_INSTALL ON)
FetchContent_Declare(yaml_cpp
  URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/0.8.0
  URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
block(SCOPE_FOR VARIABLES)
  # yaml-cpp 0.8 declares CMake 3.4; CMake 4 removed pre-3.5 policy support.
  # Keep this compatibility adjustment local to the pinned dependency.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
  FetchContent_MakeAvailable(yaml_cpp)
  install(FILES "${yaml_cpp_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/AsterCtrl RENAME yaml-cpp.LICENSE)
endblock()
