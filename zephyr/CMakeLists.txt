zephyr_library()

zephyr_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}/../src/interface
  ${CMAKE_CURRENT_LIST_DIR}/../src/runtime
)
zephyr_library_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
zephyr_library_sources(
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/execution.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/platform/zephyr/execution.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/allocator.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/channel.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/clock.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/configurator.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/executor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/hardware.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/logger.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/parameter.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/service/rpc.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/core_adapter.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/core/runtime.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../src/platform/zephyr/runtime.cpp
)

zephyr_library_sources_ifdef(
  CONFIG_ASTERCTRL_CAN
  ${CMAKE_CURRENT_LIST_DIR}/../src/platform/zephyr/can_device.cpp
)

zephyr_library_sources_ifdef(
  CONFIG_ASTERCTRL_USB_CDC_ACM
  ${CMAKE_CURRENT_LIST_DIR}/../src/platform/zephyr/usb_cdc_acm.cpp
)
