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
  aster_add_test(aster_name_resolver_test tests/runtime/name_resolver_test.cpp aster::core)
  aster_add_test(aster_configuration_test
                 tests/runtime/configuration_test.cpp aster::core)
  aster_add_test(aster_local_channel_test
                 tests/runtime/local_channel_test.cpp aster::core)
  aster_add_test(aster_local_rpc_test tests/runtime/local_rpc_test.cpp aster::core)
  aster_add_test(aster_rpc_router_test tests/runtime/rpc_router_test.cpp aster::core)
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
  add_executable(aster_plugin_abi_c_test tests/runtime/plugin_abi_c_test.c)
  target_link_libraries(aster_plugin_abi_c_test PRIVATE
    aster::pkg_c_interface
    aster::core_plugin_interface)
  target_compile_features(aster_plugin_abi_c_test PRIVATE c_std_11)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(aster_plugin_abi_c_test PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
  endif()
  add_test(NAME aster_plugin_abi_c_test COMMAND aster_plugin_abi_c_test)

  if(UNIX)
    add_library(aster_test_plugin SHARED tests/linux/test_plugin.cpp)
    target_link_libraries(aster_test_plugin PRIVATE
      aster::pkg_c_interface Threads::Threads)
    set_target_properties(aster_test_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_test_plugin)

    add_library(aster_test_cpp_package SHARED tests/linux/test_cpp_package.cpp)
    target_include_directories(aster_test_cpp_package PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime)
    target_link_libraries(aster_test_cpp_package PRIVATE
      aster::pkg_c_interface)
    set_target_properties(aster_test_cpp_package PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_test_cpp_package)

    add_library(aster_invalid_plugin SHARED tests/linux/invalid_plugin.cpp)
    target_link_libraries(aster_invalid_plugin PRIVATE
      aster::pkg_c_interface)
    set_target_properties(aster_invalid_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_invalid_plugin)

    add_library(aster_test_core_plugin SHARED tests/linux/test_core_plugin.cpp)
    target_link_libraries(aster_test_core_plugin PRIVATE
      aster::core_plugin_interface)
    set_target_properties(aster_test_core_plugin PROPERTIES
      CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    aster_target_defaults(aster_test_core_plugin)

    aster_add_test(aster_linux_thread_executor_test
                   tests/linux/thread_executor_test.cpp aster::linux)
    aster_add_test(aster_linux_runtime_config_test
                   tests/linux/runtime_config_test.cpp aster::linux)
    aster_add_test(aster_linux_channel_manager_test
                   tests/linux/channel_manager_test.cpp aster::linux)
    aster_add_test(aster_linux_rpc_manager_test tests/linux/rpc_manager_test.cpp aster::linux)
    aster_add_test(aster_linux_configured_runtime_test
                   tests/linux/configured_runtime_test.cpp aster::linux)
    target_compile_definitions(aster_linux_configured_runtime_test PRIVATE
      ASTER_TEST_CPP_PACKAGE_PATH="$<TARGET_FILE:aster_test_cpp_package>")
    add_dependencies(aster_linux_configured_runtime_test aster_test_cpp_package)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/configured-runtime-test.yaml" CONTENT
"api_version: aster.dev/v1alpha3
aster:
  packages:
    - {name: demo, path: '$<TARGET_FILE:aster_test_cpp_package>'}
  modules:
    - name: source
      type: test.ConfiguredSource
      package: demo
      config: {gain: 42, expected_executor: main}
")
    add_test(NAME aster_runtime_configured_launch COMMAND aster_runtime
      --config "${CMAKE_CURRENT_BINARY_DIR}/configured-runtime-test.yaml" --duration-ms 20)
    add_test(NAME aster_runtime_parse_only COMMAND aster_runtime
      --config "${CMAKE_CURRENT_SOURCE_DIR}/tests/linux/runtime-parse-only.yaml" --validate-config)
    aster_add_test(aster_linux_plugin_loader_test
                   tests/linux/plugin_loader_test.cpp aster::linux)
    aster_add_test(aster_linux_core_plugin_loader_test
                   tests/linux/core_plugin_loader_test.cpp aster::linux)
    aster_add_test(aster_linux_supervisor_test
                   tests/linux/supervisor_test.cpp aster::linux)
    aster_add_test(aster_linux_node_runtime_test
                   tests/linux/node_runtime_test.cpp aster::linux)
    aster_add_test(aster_linux_shutdown_signal_test
                   tests/linux/shutdown_signal_test.cpp aster::linux)
    aster_add_test(aster_linux_tty_stream_test
                   tests/linux/tty_stream_test.cpp aster::transport)
    aster_add_test(aster_socketcan_vcan_test
                   tests/linux/socketcan_vcan_test.cpp aster::transport)
    target_compile_definitions(aster_linux_plugin_loader_test PRIVATE
      ASTER_TEST_PLUGIN_PATH="$<TARGET_FILE:aster_test_plugin>"
      ASTER_TEST_CPP_PACKAGE_PATH="$<TARGET_FILE:aster_test_cpp_package>"
      ASTER_INVALID_PLUGIN_PATH="$<TARGET_FILE:aster_invalid_plugin>")
    add_dependencies(aster_linux_plugin_loader_test
                     aster_test_plugin aster_test_cpp_package aster_invalid_plugin)
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
    aster_add_test(aster_channel_bridge_test
                   tests/transports/channel_bridge_test.cpp aster::transport)
    aster_add_test(aster_channel_transport_module_test
                   tests/transports/channel_transport_module_test.cpp
                   aster::transport)
    aster_add_test(aster_can_protocol_test
                   tests/transports/can_protocol_test.cpp aster::transport)
    aster_add_test(aster_can_link_control_test
                   tests/transports/can_link_control_test.cpp aster::transport)
    aster_add_test(aster_can_channel_bridge_test
                   tests/transports/can_channel_bridge_test.cpp aster::transport)
    aster_add_test(aster_can_channel_transport_module_test
                   tests/transports/can_channel_transport_module_test.cpp
                   aster::transport)
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
