cmake_minimum_required(VERSION 3.28)

project(AsterCtrlConsumer LANGUAGES CXX)

option(ASTER_CONSUMER_REQUIRE_STABLE "Require the final AsterCtrl 0.2.0 package" OFF)

if(ASTER_CONSUMER_REQUIRE_STABLE)
  find_package(AsterCtrl 0.2.0 EXACT CONFIG REQUIRED)
else()
  find_package(AsterCtrl CONFIG REQUIRED)
endif()

if(NOT AsterCtrl_VERSION_STRING STREQUAL "0.2.0-alpha.1")
  message(FATAL_ERROR "unexpected AsterCtrl package version: ${AsterCtrl_VERSION_STRING}")
endif()

add_executable(asterctrl_consumer main.cpp)
target_link_libraries(asterctrl_consumer PRIVATE aster::core)
target_compile_features(asterctrl_consumer PRIVATE cxx_std_20)
