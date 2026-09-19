#include <cassert>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "aster_core_plugin_interface/core_plugin_main.h"
#include "aster_module_c_interface/aster_module_c_interface.h"
#include "aster_module_cpp_interface/status.hpp"

static_assert(std::is_standard_layout_v<aster_core_base_t>);
static_assert(std::is_standard_layout_v<aster_execution_context_t>);
static_assert(std::is_standard_layout_v<aster_type_descriptor_t>);
static_assert(std::is_standard_layout_v<aster_channel_descriptor_t>);
static_assert(std::is_standard_layout_v<aster_service_descriptor_t>);
static_assert(std::is_standard_layout_v<aster_configurator_base_t>);
static_assert(std::is_standard_layout_v<aster_logger_base_t>);
static_assert(std::is_standard_layout_v<aster_executor_base_t>);
static_assert(std::is_standard_layout_v<aster_channel_base_t>);
static_assert(std::is_standard_layout_v<aster_rpc_base_t>);
static_assert(std::is_standard_layout_v<aster_parameter_base_t>);
static_assert(std::is_standard_layout_v<aster_clock_base_t>);
static_assert(std::is_standard_layout_v<aster_allocator_base_t>);
static_assert(std::is_standard_layout_v<aster_hardware_manager_base_t>);
static_assert(std::is_standard_layout_v<aster_module_base_t>);
static_assert(std::is_standard_layout_v<aster_interface_header_t>);
static_assert(std::is_standard_layout_v<aster_core_plugin_t>);
static_assert(static_cast<aster_status_t>(aster::Status::kOk) == ASTER_STATUS_OK);
static_assert(static_cast<aster_status_t>(aster::Status::kInvalidArgument) ==
              ASTER_STATUS_INVALID_ARGUMENT);
static_assert(static_cast<aster_status_t>(aster::Status::kNotFound) == ASTER_STATUS_NOT_FOUND);
static_assert(static_cast<aster_status_t>(aster::Status::kCapacityExceeded) ==
              ASTER_STATUS_CAPACITY_EXCEEDED);
static_assert(static_cast<aster_status_t>(aster::Status::kUnavailable) == ASTER_STATUS_UNAVAILABLE);
static_assert(static_cast<aster_status_t>(aster::Status::kAlreadyExists) ==
              ASTER_STATUS_ALREADY_EXISTS);
static_assert(static_cast<aster_status_t>(aster::Status::kTimeout) == ASTER_STATUS_TIMEOUT);
static_assert(static_cast<aster_status_t>(aster::Status::kCancelled) == ASTER_STATUS_CANCELLED);
static_assert(static_cast<aster_status_t>(aster::Status::kTypeMismatch) ==
              ASTER_STATUS_TYPE_MISMATCH);
static_assert(static_cast<aster_status_t>(aster::Status::kVersionMismatch) ==
              ASTER_STATUS_VERSION_MISMATCH);
static_assert(static_cast<aster_status_t>(aster::Status::kInternal) == ASTER_STATUS_INTERNAL);
static_assert(static_cast<aster_status_t>(aster::Status::kProtocolError) ==
              ASTER_STATUS_PROTOCOL_ERROR);
static_assert(static_cast<aster_status_t>(aster::Status::kInvalidState) ==
              ASTER_STATUS_INVALID_STATE);
static_assert(aster::CategoryOf(aster::Status::kInvalidArgument) ==
              aster::StatusCategory::kConfiguration);
static_assert(aster::CategoryOf(aster::Status::kCapacityExceeded) ==
              aster::StatusCategory::kResource);
static_assert(aster::CategoryOf(aster::Status::kTimeout) == aster::StatusCategory::kTimeout);
static_assert(aster::CategoryOf(aster::Status::kVersionMismatch) ==
              aster::StatusCategory::kProtocol);
static_assert(aster::CategoryOf(aster::Status::kProtocolError) == aster::StatusCategory::kProtocol);
static_assert(aster::CategoryOf(aster::Status::kInvalidState) == aster::StatusCategory::kLifecycle);
static_assert(aster::CategoryOf(aster::Status::kInternal) == aster::StatusCategory::kPlatform);

int main() {
  assert(ASTER_ABI_VERSION == 2);
  assert(std::string_view(ASTER_CORE_PLUGIN_CREATE_SYMBOL) == "AsterDynlibCreateCorePlugin");
  assert(std::string_view(ASTER_CORE_PLUGIN_DESTROY_SYMBOL) == "AsterDynlibDestroyCorePlugin");
  assert(offsetof(aster_core_plugin_t, abi_version) == 0);
  assert(offsetof(aster_interface_header_t, interface_version) == 0);
  assert(offsetof(aster_module_base_t, abi_version) == 0);
  assert(offsetof(aster_module_base_t, impl) < offsetof(aster_module_base_t, info));
  assert(offsetof(aster_module_base_t, info) < offsetof(aster_module_base_t, initialize));
  assert(offsetof(aster_logger_base_t, struct_size) == 0);
  assert(offsetof(aster_rpc_base_t, struct_size) == 0);
  assert(sizeof(aster_status_t) == 4);
}
