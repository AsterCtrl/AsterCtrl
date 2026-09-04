zephyr_library()

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)
zephyr_library_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
zephyr_library_sources(
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
