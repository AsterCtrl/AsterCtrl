install(TARGETS ${ASTER_INSTALL_TARGETS}
  EXPORT AsterCtrlTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT AsterCtrlTargets
  FILE AsterCtrlTargets.cmake
  NAMESPACE aster::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsterCtrl)

set(ASTERCTRL_CONFIG_DEPENDENCIES "")
if(UNIX)
  set(ASTERCTRL_CONFIG_DEPENDENCIES "find_dependency(Threads)\nfind_dependency(yaml-cpp 0.8 CONFIG)")
endif()
configure_package_config_file(
  cmake/AsterCtrlConfig.cmake.in
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
    cmake/AsterCtrlPrereleaseConfigVersion.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfigVersion.cmake
    @ONLY)
endif()
install(FILES
  cmake/AsterPackage.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/AsterCtrlConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AsterCtrl)
if(UNIX)
  configure_file(packaging/systemd/aster-node@.service
    ${CMAKE_CURRENT_BINARY_DIR}/aster-node@.service @ONLY)
  if(NOT APPLE)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aster-node@.service
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
  endif()
endif()
