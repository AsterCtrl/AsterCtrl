#include <cassert>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "aster/plugin.h"
#include "aster/status.hpp"

static_assert(std::is_standard_layout_v<AsterCoreRefV1>);
static_assert(std::is_standard_layout_v<AsterExecutionContextV1>);
static_assert(std::is_standard_layout_v<AsterTypeDescriptorV1>);
static_assert(std::is_standard_layout_v<AsterChannelDescriptorV1>);
static_assert(std::is_standard_layout_v<AsterServiceDescriptorV1>);
static_assert(std::is_standard_layout_v<AsterConfiguratorServiceV1>);
static_assert(std::is_standard_layout_v<AsterLoggerServiceV1>);
static_assert(std::is_standard_layout_v<AsterExecutorServiceV1>);
static_assert(std::is_standard_layout_v<AsterChannelServiceV1>);
static_assert(std::is_standard_layout_v<AsterRpcServiceV1>);
static_assert(std::is_standard_layout_v<AsterParameterServiceV1>);
static_assert(std::is_standard_layout_v<AsterClockServiceV1>);
static_assert(std::is_standard_layout_v<AsterAllocatorServiceV1>);
static_assert(std::is_standard_layout_v<AsterHardwareManagerServiceV1>);
static_assert(std::is_standard_layout_v<AsterModuleV1>);
static_assert(std::is_standard_layout_v<AsterModuleBundleV1>);
static_assert(std::is_standard_layout_v<AsterInterfaceHeaderV1>);
static_assert(std::is_standard_layout_v<AsterCorePluginV1>);
static_assert(static_cast<AsterStatusV1>(aster::Status::kOk) == ASTER_STATUS_OK_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kInvalidArgument) ==
              ASTER_STATUS_INVALID_ARGUMENT_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kNotFound) == ASTER_STATUS_NOT_FOUND_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kCapacityExceeded) ==
              ASTER_STATUS_CAPACITY_EXCEEDED_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kUnavailable) ==
              ASTER_STATUS_UNAVAILABLE_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kAlreadyExists) ==
              ASTER_STATUS_ALREADY_EXISTS_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kTimeout) == ASTER_STATUS_TIMEOUT_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kCancelled) == ASTER_STATUS_CANCELLED_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kTypeMismatch) ==
              ASTER_STATUS_TYPE_MISMATCH_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kVersionMismatch) ==
              ASTER_STATUS_VERSION_MISMATCH_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kInternal) == ASTER_STATUS_INTERNAL_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kProtocolError) ==
              ASTER_STATUS_PROTOCOL_ERROR_V1);
static_assert(static_cast<AsterStatusV1>(aster::Status::kInvalidState) ==
              ASTER_STATUS_INVALID_STATE_V1);
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
  assert(ASTER_CORE_ABI_VERSION_V1 == 1);
  assert(std::string_view(ASTER_CORE_SERVICE_CONFIGURATOR_NAME_V1) == "aster.configurator");
  assert(std::string_view(ASTER_CORE_SERVICE_HARDWARE_MANAGER_NAME_V1) == "aster.hardware_manager");
  assert(ASTER_CORE_SERVICE_CONFIGURATOR_VERSION_V1 == 1);
  assert(ASTER_CORE_SERVICE_RPC_VERSION_V1 == 1);
  assert(offsetof(AsterCorePluginV1, abi_version) == 0);
  assert(offsetof(AsterInterfaceHeaderV1, interface_version) == 0);
  assert(offsetof(AsterModuleBundleV1, abi_version) == 0);
  assert(offsetof(AsterLoggerServiceV1, service_version) == 0);
  assert(offsetof(AsterRpcServiceV1, service_version) == 0);
  assert(sizeof(AsterStatusV1) == 4);
}
