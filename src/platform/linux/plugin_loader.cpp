#include "aster/platform/linux/plugin_loader.hpp"

#include <dlfcn.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace aster::platform::linux {
namespace {

[[nodiscard]] std::string_view ToView(AsterStringViewV1 value) noexcept {
  return value.data == nullptr ? std::string_view{} : std::string_view(value.data, value.size);
}

[[nodiscard]] bool ValidView(AsterStringViewV1 value) noexcept {
  return value.data != nullptr && value.size != 0;
}

[[nodiscard]] Status FromAbiStatus(AsterStatusV1 status) noexcept {
  switch (status) {
    case ASTER_STATUS_OK_V1:
    case ASTER_STATUS_INVALID_ARGUMENT_V1:
    case ASTER_STATUS_NOT_FOUND_V1:
    case ASTER_STATUS_CAPACITY_EXCEEDED_V1:
    case ASTER_STATUS_UNAVAILABLE_V1:
    case ASTER_STATUS_ALREADY_EXISTS_V1:
    case ASTER_STATUS_TIMEOUT_V1:
    case ASTER_STATUS_CANCELLED_V1:
    case ASTER_STATUS_TYPE_MISMATCH_V1:
    case ASTER_STATUS_VERSION_MISMATCH_V1:
    case ASTER_STATUS_PROTOCOL_ERROR_V1:
    case ASTER_STATUS_INVALID_STATE_V1:
    case ASTER_STATUS_INTERNAL_V1:
      return static_cast<Status>(status);
    default:
      return Status::kInternal;
  }
}

[[nodiscard]] AsterStatusV1 ToAbiStatus(Status status) noexcept {
  switch (status) {
    case Status::kOk:
    case Status::kInvalidArgument:
    case Status::kNotFound:
    case Status::kCapacityExceeded:
    case Status::kUnavailable:
    case Status::kAlreadyExists:
    case Status::kTimeout:
    case Status::kCancelled:
    case Status::kTypeMismatch:
    case Status::kVersionMismatch:
    case Status::kProtocolError:
    case Status::kInvalidState:
    case Status::kInternal:
      return static_cast<AsterStatusV1>(status);
  }
  return ASTER_STATUS_INTERNAL_V1;
}

[[nodiscard]] bool ValidBuffer(const void* data, std::size_t size) noexcept {
  return size == 0 || data != nullptr;
}

[[nodiscard]] bool ValidExecutionContext(const AsterExecutionContextV1* context) noexcept {
  return context != nullptr && context->abi_version == ASTER_CORE_ABI_VERSION_V1 &&
         context->struct_size >= sizeof(AsterExecutionContextV1) &&
         ValidView(context->executor_name) && context->kind <= ASTER_EXECUTION_KIND_INTERRUPT_V1;
}

[[nodiscard]] ExecutionContext ToExecutionContext(const AsterExecutionContextV1& context) noexcept {
  return {ToView(context.executor_name),
          context.kind == ASTER_EXECUTION_KIND_THREAD_V1 ? ExecutionKind::kThread
                                                         : ExecutionKind::kInterrupt,
          context.timestamp_ns};
}

[[nodiscard]] AsterExecutionContextV1 ToAbiExecutionContext(
    const ExecutionContext& context) noexcept {
  return {ASTER_CORE_ABI_VERSION_V1,
          sizeof(AsterExecutionContextV1),
          {context.executor_name().data(), context.executor_name().size()},
          context.kind() == ExecutionKind::kThread ? ASTER_EXECUTION_KIND_THREAD_V1
                                                   : ASTER_EXECUTION_KIND_INTERRUPT_V1,
          context.timestamp_ns()};
}

[[nodiscard]] Status ToTypeDescriptor(const AsterTypeDescriptorV1* input,
                                      TypeDescriptor& output) noexcept {
  if (input == nullptr || input->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      input->struct_size < sizeof(AsterTypeDescriptorV1)) {
    return Status::kVersionMismatch;
  }
  if (!ValidView(input->name) || input->max_serialized_size == 0 ||
      input->max_serialized_size >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return Status::kInvalidArgument;
  }
  SchemaHash hash{};
  for (std::size_t index = 0; index < hash.bytes.size(); ++index) {
    hash.bytes[index] = static_cast<std::byte>(input->schema_hash.bytes[index]);
  }
  output = {ToView(input->name), hash, static_cast<std::size_t>(input->max_serialized_size)};
  return Status::kOk;
}

[[nodiscard]] Status ToChannelDescriptor(const AsterChannelDescriptorV1* input,
                                         ChannelDescriptor& output) noexcept {
  if (input == nullptr || input->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      input->struct_size < sizeof(AsterChannelDescriptorV1)) {
    return Status::kVersionMismatch;
  }
  if (!ValidView(input->name)) {
    return Status::kInvalidArgument;
  }
  TypeDescriptor message_type{};
  const auto status = ToTypeDescriptor(&input->message_type, message_type);
  if (!IsOk(status)) {
    return status;
  }
  output = {ToView(input->name), message_type};
  return Status::kOk;
}

[[nodiscard]] Status ToServiceDescriptor(const AsterServiceDescriptorV1* input,
                                         ServiceDescriptor& output) noexcept {
  if (input == nullptr || input->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      input->struct_size < sizeof(AsterServiceDescriptorV1)) {
    return Status::kVersionMismatch;
  }
  if (!ValidView(input->name)) {
    return Status::kInvalidArgument;
  }
  TypeDescriptor request_type{};
  TypeDescriptor response_type{};
  auto status = ToTypeDescriptor(&input->request_type, request_type);
  if (!IsOk(status)) {
    return status;
  }
  status = ToTypeDescriptor(&input->response_type, response_type);
  if (!IsOk(status)) {
    return status;
  }
  SchemaHash hash{};
  for (std::size_t index = 0; index < hash.bytes.size(); ++index) {
    hash.bytes[index] = static_cast<std::byte>(input->schema_hash.bytes[index]);
  }
  output = {ToView(input->name), hash, request_type, response_type};
  return Status::kOk;
}

[[nodiscard]] AsterMessageInfoV1 ToAbiMessageInfo(const MessageInfo& info) noexcept {
  return {ASTER_CORE_ABI_VERSION_V1, sizeof(AsterMessageInfoV1), info.sequence,
          info.source_timestamp_ns};
}

[[nodiscard]] AsterRpcCallInfoV1 ToAbiRpcCallInfo(const RpcCallInfo& info) noexcept {
  return {ASTER_CORE_ABI_VERSION_V1, sizeof(AsterRpcCallInfoV1), info.request_id, info.deadline_ns};
}

[[nodiscard]] bool ValidModule(const AsterModuleV1& module) noexcept {
  return module.abi_version == ASTER_CORE_ABI_VERSION_V1 &&
         module.struct_size >= sizeof(AsterModuleV1) && ValidView(module.info.name) &&
         ValidView(module.info.type) && ValidView(module.info.package) &&
         module.instance != nullptr && module.initialize != nullptr && module.start != nullptr &&
         module.shutdown != nullptr;
}

}  // namespace

class PluginLoader::CAbiModule final : public Module {
 public:
  explicit CAbiModule(const AsterModuleV1& module) noexcept
      : module_(&module),
        configurator_service_{ASTER_CORE_SERVICE_CONFIGURATOR_VERSION_V1,
                              sizeof(AsterConfiguratorServiceV1), this, ConfiguratorGet},
        logger_service_{ASTER_CORE_SERVICE_LOGGER_VERSION_V1, sizeof(AsterLoggerServiceV1), this,
                        LoggerWrite},
        executor_service_{ASTER_CORE_SERVICE_EXECUTOR_VERSION_V1,
                          sizeof(AsterExecutorServiceV1),
                          this,
                          ExecutorGetName,
                          ExecutorTryPost,
                          ExecutorTryPostAt},
        channel_service_{ASTER_CORE_SERVICE_CHANNEL_VERSION_V1,
                         sizeof(AsterChannelServiceV1),
                         this,
                         ChannelRegisterPublisher,
                         ChannelRegisterSubscriber,
                         ChannelPublish},
        rpc_service_{ASTER_CORE_SERVICE_RPC_VERSION_V1,
                     sizeof(AsterRpcServiceV1),
                     this,
                     RpcRegisterClient,
                     RpcRegisterServer,
                     RpcCallAsync},
        parameter_service_{ASTER_CORE_SERVICE_PARAMETER_VERSION_V1, sizeof(AsterParameterServiceV1),
                           this, ParameterGet, ParameterSet},
        clock_service_{ASTER_CORE_SERVICE_CLOCK_VERSION_V1, sizeof(AsterClockServiceV1), this,
                       ClockGetDomain, ClockNowNs},
        allocator_service_{ASTER_CORE_SERVICE_ALLOCATOR_VERSION_V1, sizeof(AsterAllocatorServiceV1),
                           this, AllocatorAllocate, AllocatorDeallocate},
        hardware_service_{ASTER_CORE_SERVICE_HARDWARE_MANAGER_VERSION_V1,
                          sizeof(AsterHardwareManagerServiceV1), this, HardwareResolve} {
    for (auto& slot : work_slots_) {
      slot.owner = this;
    }
    for (auto& slot : channel_slots_) {
      slot.owner = this;
    }
    for (auto& slot : rpc_server_slots_) {
      slot.owner = this;
    }
    for (auto& slot : rpc_completion_slots_) {
      slot.owner = this;
    }
  }
  ~CAbiModule() override { Shutdown(); }

  [[nodiscard]] ModuleInfo Info() const noexcept override {
    return {
        ToView(module_->info.name),
        ToView(module_->info.type),
        ToView(module_->info.package),
        {module_->info.version.major, module_->info.version.minor, module_->info.version.patch},
    };
  }

  Status Initialize(CoreRef core) noexcept override {
    if (initialized_) {
      return Status::kInvalidState;
    }
    core_ = core;
    abi_core_ = {
        ASTER_CORE_ABI_VERSION_V1,
        sizeof(AsterCoreRefV1),
        this,
        QueryService,
    };
    initialized_ = true;
    try {
      return FromAbiStatus(module_->initialize(module_->instance, &abi_core_));
    } catch (...) {
      return Status::kInternal;
    }
  }

  Status Start() noexcept override {
    if (!initialized_ || started_) {
      return Status::kInvalidState;
    }
    Status status{};
    try {
      status = FromAbiStatus(module_->start(module_->instance));
    } catch (...) {
      return Status::kInternal;
    }
    if (IsOk(status)) {
      started_ = true;
    }
    return status;
  }

  void Shutdown() noexcept override {
    if (!initialized_) {
      return;
    }
    try {
      module_->shutdown(module_->instance);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Shutdown is a void C ABI hook; contain a foreign exception at the boundary.
    }
    initialized_ = false;
    started_ = false;
  }

 private:
  static AsterStatusV1 ConfiguratorGet(void* context, AsterStringViewV1 key, uint8_t* output,
                                       size_t output_capacity, size_t* written) noexcept {
    if (context == nullptr || written == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    *written = 0;
    if (!ValidView(key) || !ValidBuffer(output, output_capacity)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    std::size_t native_written{};
    const auto status = self.core_.configurator().Get(
        ToView(key), {reinterpret_cast<std::byte*>(output), output_capacity}, native_written);
    if (native_written > output_capacity && status != Status::kCapacityExceeded) {
      return ASTER_STATUS_INTERNAL_V1;
    }
    *written = native_written;
    return ToAbiStatus(status);
  }

  static AsterStatusV1 LoggerWrite(void* context, uint32_t level, AsterStringViewV1 message,
                                   const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr || !ValidView(message)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    if (caller == nullptr || caller->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
        caller->struct_size < sizeof(AsterExecutionContextV1)) {
      return ASTER_STATUS_VERSION_MISMATCH_V1;
    }
    if (!ValidExecutionContext(caller) || level > ASTER_LOG_LEVEL_CRITICAL_V1) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    const auto native_context = ToExecutionContext(*caller);
    return ToAbiStatus(
        self.core_.logger().Write(static_cast<LogLevel>(level), ToView(message), native_context));
  }

  static AsterStatusV1 ExecutorGetName(void* context, AsterStringViewV1* name) noexcept {
    if (context == nullptr || name == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    *name = {};
    auto& self = *static_cast<CAbiModule*>(context);
    if (!self.core_.executor()) {
      return ASTER_STATUS_UNAVAILABLE_V1;
    }
    const auto native_name = self.core_.executor().Name();
    if (native_name.empty()) {
      return ASTER_STATUS_INTERNAL_V1;
    }
    *name = {native_name.data(), native_name.size()};
    return ASTER_STATUS_OK_V1;
  }

  static AsterStatusV1 ExecutorTryPost(void* context, AsterWorkCallbackV1 callback,
                                       void* callback_state,
                                       const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    return static_cast<CAbiModule*>(context)->Post(0, false, callback, callback_state, caller);
  }

  static AsterStatusV1 ExecutorTryPostAt(void* context, uint64_t timestamp_ns,
                                         AsterWorkCallbackV1 callback, void* callback_state,
                                         const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    return static_cast<CAbiModule*>(context)->Post(timestamp_ns, true, callback, callback_state,
                                                   caller);
  }

  static AsterStatusV1 ChannelRegisterPublisher(
      void* context, const AsterChannelDescriptorV1* descriptor) noexcept {
    if (context == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ChannelDescriptor native_descriptor{};
    const auto status = ToChannelDescriptor(descriptor, native_descriptor);
    if (!IsOk(status)) {
      return ToAbiStatus(status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    return ToAbiStatus(self.core_.channel().RegisterPublisher(native_descriptor));
  }

  static AsterStatusV1 ChannelRegisterSubscriber(void* context,
                                                 const AsterChannelDescriptorV1* descriptor,
                                                 AsterChannelCallbackV1 callback,
                                                 void* callback_state) noexcept {
    if (context == nullptr || callback == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ChannelDescriptor native_descriptor{};
    const auto descriptor_status = ToChannelDescriptor(descriptor, native_descriptor);
    if (!IsOk(descriptor_status)) {
      return ToAbiStatus(descriptor_status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    ChannelSlot* available{};
    for (auto& slot : self.channel_slots_) {
      if (!slot.in_use) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      return ASTER_STATUS_CAPACITY_EXCEEDED_V1;
    }
    available->callback = callback;
    available->callback_state = callback_state;
    available->in_use = true;
    const auto status =
        self.core_.channel().RegisterSubscriber(native_descriptor, DispatchChannel, available);
    if (!IsOk(status)) {
      available->callback = nullptr;
      available->callback_state = nullptr;
      available->in_use = false;
    }
    return ToAbiStatus(status);
  }

  static AsterStatusV1 ChannelPublish(void* context, const AsterChannelDescriptorV1* descriptor,
                                      const uint8_t* message, size_t message_size,
                                      uint64_t source_timestamp_ns,
                                      const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr || !ValidBuffer(message, message_size)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    if (caller == nullptr || caller->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
        caller->struct_size < sizeof(AsterExecutionContextV1)) {
      return ASTER_STATUS_VERSION_MISMATCH_V1;
    }
    if (!ValidExecutionContext(caller)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ChannelDescriptor native_descriptor{};
    const auto descriptor_status = ToChannelDescriptor(descriptor, native_descriptor);
    if (!IsOk(descriptor_status)) {
      return ToAbiStatus(descriptor_status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    const auto native_context = ToExecutionContext(*caller);
    return ToAbiStatus(self.core_.channel().Publish(
        native_descriptor, {reinterpret_cast<const std::byte*>(message), message_size},
        source_timestamp_ns, native_context));
  }

  static AsterStatusV1 RpcRegisterClient(void* context,
                                         const AsterServiceDescriptorV1* descriptor) noexcept {
    if (context == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ServiceDescriptor native_descriptor{};
    const auto status = ToServiceDescriptor(descriptor, native_descriptor);
    if (!IsOk(status)) {
      return ToAbiStatus(status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    return ToAbiStatus(self.core_.rpc().RegisterClient(native_descriptor));
  }

  static AsterStatusV1 RpcRegisterServer(void* context, const AsterServiceDescriptorV1* descriptor,
                                         AsterRpcHandlerV1 handler, void* handler_state) noexcept {
    if (context == nullptr || handler == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ServiceDescriptor native_descriptor{};
    const auto descriptor_status = ToServiceDescriptor(descriptor, native_descriptor);
    if (!IsOk(descriptor_status)) {
      return ToAbiStatus(descriptor_status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    RpcServerSlot* available{};
    for (auto& slot : self.rpc_server_slots_) {
      if (!slot.in_use) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      return ASTER_STATUS_CAPACITY_EXCEEDED_V1;
    }
    available->handler = handler;
    available->handler_state = handler_state;
    available->in_use = true;
    const auto status = self.core_.rpc().RegisterServer(native_descriptor, DispatchRpc, available);
    if (!IsOk(status)) {
      available->handler = nullptr;
      available->handler_state = nullptr;
      available->in_use = false;
    }
    return ToAbiStatus(status);
  }

  static AsterStatusV1 RpcCallAsync(void* context, const AsterServiceDescriptorV1* descriptor,
                                    const uint8_t* request, size_t request_size,
                                    uint64_t deadline_ns, AsterRpcCompletionV1 completion,
                                    void* completion_state,
                                    const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr || completion == nullptr || !ValidBuffer(request, request_size)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    if (caller == nullptr || caller->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
        caller->struct_size < sizeof(AsterExecutionContextV1)) {
      return ASTER_STATUS_VERSION_MISMATCH_V1;
    }
    if (!ValidExecutionContext(caller)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    ServiceDescriptor native_descriptor{};
    const auto descriptor_status = ToServiceDescriptor(descriptor, native_descriptor);
    if (!IsOk(descriptor_status)) {
      return ToAbiStatus(descriptor_status);
    }
    auto& self = *static_cast<CAbiModule*>(context);
    RpcCompletionSlot* available{};
    for (auto& slot : self.rpc_completion_slots_) {
      bool expected = false;
      if (slot.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      return ASTER_STATUS_CAPACITY_EXCEEDED_V1;
    }
    available->completion = completion;
    available->completion_state = completion_state;
    const auto native_context = ToExecutionContext(*caller);
    const auto status = self.core_.rpc().CallAsync(
        native_descriptor, {reinterpret_cast<const std::byte*>(request), request_size}, deadline_ns,
        CompleteRpc, available, native_context);
    if (!IsOk(status) && available->in_use.load(std::memory_order_acquire)) {
      available->completion = nullptr;
      available->completion_state = nullptr;
      available->in_use.store(false, std::memory_order_release);
    }
    return ToAbiStatus(status);
  }

  static AsterStatusV1 ParameterGet(void* context, AsterStringViewV1 name, AsterStringViewV1 type,
                                    uint8_t* output, size_t output_capacity,
                                    size_t* written) noexcept {
    if (context == nullptr || written == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    *written = 0;
    if (!ValidView(name) || !ValidView(type) || !ValidBuffer(output, output_capacity)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    std::size_t native_written{};
    const auto status = self.core_.parameter().Get(
        ToView(name), ToView(type), {reinterpret_cast<std::byte*>(output), output_capacity},
        native_written);
    if (native_written > output_capacity && status != Status::kCapacityExceeded) {
      return ASTER_STATUS_INTERNAL_V1;
    }
    *written = native_written;
    return ToAbiStatus(status);
  }

  static AsterStatusV1 ParameterSet(void* context, AsterStringViewV1 name, AsterStringViewV1 type,
                                    const uint8_t* value, size_t value_size,
                                    const AsterExecutionContextV1* caller) noexcept {
    if (context == nullptr || !ValidView(name) || !ValidView(type) ||
        !ValidBuffer(value, value_size)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    if (caller == nullptr || caller->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
        caller->struct_size < sizeof(AsterExecutionContextV1)) {
      return ASTER_STATUS_VERSION_MISMATCH_V1;
    }
    if (!ValidExecutionContext(caller)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    const auto native_context = ToExecutionContext(*caller);
    return ToAbiStatus(self.core_.parameter().Set(
        ToView(name), ToView(type), {reinterpret_cast<const std::byte*>(value), value_size},
        native_context));
  }

  static AsterStatusV1 ClockGetDomain(void* context, uint32_t* domain) noexcept {
    if (context == nullptr || domain == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    if (!self.core_.clock()) {
      return ASTER_STATUS_UNAVAILABLE_V1;
    }
    *domain = static_cast<std::uint32_t>(self.core_.clock().domain());
    return ASTER_STATUS_OK_V1;
  }

  static AsterStatusV1 ClockNowNs(void* context, uint64_t* now_ns) noexcept {
    if (context == nullptr || now_ns == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    if (!self.core_.clock()) {
      return ASTER_STATUS_UNAVAILABLE_V1;
    }
    *now_ns = self.core_.clock().NowNs();
    return ASTER_STATUS_OK_V1;
  }

  static AsterStatusV1 AllocatorAllocate(void* context, size_t size, size_t alignment,
                                         void** memory) noexcept {
    if (context == nullptr || memory == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    *memory = nullptr;
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1U)) != 0) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    *memory = self.core_.allocator().Allocate(size, alignment);
    return *memory == nullptr ? ASTER_STATUS_CAPACITY_EXCEEDED_V1 : ASTER_STATUS_OK_V1;
  }

  static AsterStatusV1 AllocatorDeallocate(void* context, void* memory, size_t size,
                                           size_t alignment) noexcept {
    if (context == nullptr || memory == nullptr || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1U)) != 0) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    if (!self.core_.allocator()) {
      return ASTER_STATUS_UNAVAILABLE_V1;
    }
    self.core_.allocator().Deallocate(memory, size, alignment);
    return ASTER_STATUS_OK_V1;
  }

  static AsterStatusV1 HardwareResolve(void* context, AsterStringViewV1 name,
                                       AsterStringViewV1 type, void** device) noexcept {
    if (context == nullptr || device == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    *device = nullptr;
    if (!ValidView(name) || !ValidView(type)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    void* native_device{};
    const auto status = self.core_.hardware().Resolve(ToView(name), ToView(type), native_device);
    if (IsOk(status)) {
      *device = native_device;
    }
    return ToAbiStatus(status);
  }

  static const void* QueryService(void* context, AsterStringViewV1 service_name,
                                  uint32_t service_version,
                                  uint32_t* service_struct_size) noexcept {
    if (service_struct_size == nullptr) {
      return nullptr;
    }
    *service_struct_size = 0;
    if (context == nullptr || !ValidView(service_name)) {
      return nullptr;
    }
    auto& self = *static_cast<CAbiModule*>(context);
    const auto name = ToView(service_name);
    if (name == ASTER_CORE_SERVICE_CONFIGURATOR_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_CONFIGURATOR_VERSION_V1 &&
        self.core_.configurator()) {
      *service_struct_size = sizeof(AsterConfiguratorServiceV1);
      return &self.configurator_service_;
    }
    if (name == ASTER_CORE_SERVICE_LOGGER_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_LOGGER_VERSION_V1 && self.core_.logger()) {
      *service_struct_size = sizeof(AsterLoggerServiceV1);
      return &self.logger_service_;
    }
    if (name == ASTER_CORE_SERVICE_EXECUTOR_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_EXECUTOR_VERSION_V1 && self.core_.executor()) {
      *service_struct_size = sizeof(AsterExecutorServiceV1);
      return &self.executor_service_;
    }
    if (name == ASTER_CORE_SERVICE_CHANNEL_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_CHANNEL_VERSION_V1 && self.core_.channel()) {
      *service_struct_size = sizeof(AsterChannelServiceV1);
      return &self.channel_service_;
    }
    if (name == ASTER_CORE_SERVICE_RPC_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_RPC_VERSION_V1 && self.core_.rpc()) {
      *service_struct_size = sizeof(AsterRpcServiceV1);
      return &self.rpc_service_;
    }
    if (name == ASTER_CORE_SERVICE_PARAMETER_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_PARAMETER_VERSION_V1 && self.core_.parameter()) {
      *service_struct_size = sizeof(AsterParameterServiceV1);
      return &self.parameter_service_;
    }
    if (name == ASTER_CORE_SERVICE_CLOCK_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_CLOCK_VERSION_V1 && self.core_.clock()) {
      *service_struct_size = sizeof(AsterClockServiceV1);
      return &self.clock_service_;
    }
    if (name == ASTER_CORE_SERVICE_ALLOCATOR_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_ALLOCATOR_VERSION_V1 && self.core_.allocator()) {
      *service_struct_size = sizeof(AsterAllocatorServiceV1);
      return &self.allocator_service_;
    }
    if (name == ASTER_CORE_SERVICE_HARDWARE_MANAGER_NAME_V1 &&
        service_version == ASTER_CORE_SERVICE_HARDWARE_MANAGER_VERSION_V1 &&
        self.core_.hardware()) {
      *service_struct_size = sizeof(AsterHardwareManagerServiceV1);
      return &self.hardware_service_;
    }
    return nullptr;
  }

  struct WorkSlot {
    CAbiModule* owner{};
    AsterWorkCallbackV1 callback{};
    void* callback_state{};
    std::atomic<bool> in_use{};
  };

  struct ChannelSlot {
    CAbiModule* owner{};
    AsterChannelCallbackV1 callback{};
    void* callback_state{};
    bool in_use{};
  };

  struct RpcServerSlot {
    CAbiModule* owner{};
    AsterRpcHandlerV1 handler{};
    void* handler_state{};
    bool in_use{};
  };

  struct RpcCompletionSlot {
    CAbiModule* owner{};
    AsterRpcCompletionV1 completion{};
    void* completion_state{};
    std::atomic<bool> in_use{};
  };

  static Status DispatchChannel(void* state, std::span<const std::byte> message,
                                const MessageInfo& info, const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<ChannelSlot*>(state);
    if (!slot.in_use || slot.callback == nullptr) {
      return Status::kInvalidState;
    }
    const auto abi_info = ToAbiMessageInfo(info);
    const auto abi_context = ToAbiExecutionContext(context);
    try {
      return FromAbiStatus(slot.callback(slot.callback_state,
                                         reinterpret_cast<const uint8_t*>(message.data()),
                                         message.size(), &abi_info, &abi_context));
    } catch (...) {
      return Status::kInternal;
    }
  }

  static Status DispatchRpc(void* state, std::span<const std::byte> request,
                            std::span<std::byte> response, std::size_t& response_size,
                            const RpcCallInfo& info, const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<RpcServerSlot*>(state);
    response_size = 0;
    if (!slot.in_use || slot.handler == nullptr) {
      return Status::kInvalidState;
    }
    const auto abi_info = ToAbiRpcCallInfo(info);
    const auto abi_context = ToAbiExecutionContext(context);
    AsterStatusV1 abi_status{ASTER_STATUS_INTERNAL_V1};
    try {
      abi_status =
          slot.handler(slot.handler_state, reinterpret_cast<const uint8_t*>(request.data()),
                       request.size(), reinterpret_cast<uint8_t*>(response.data()), response.size(),
                       &response_size, &abi_info, &abi_context);
    } catch (...) {
      return Status::kInternal;
    }
    if (response_size > response.size()) {
      response_size = 0;
      return Status::kInternal;
    }
    const auto status = FromAbiStatus(abi_status);
    if (!IsOk(status)) {
      response_size = 0;
    }
    return status;
  }

  static void CompleteRpc(void* state, Status status, std::span<const std::byte> response,
                          const RpcCallInfo& info, const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<RpcCompletionSlot*>(state);
    if (!slot.in_use.load(std::memory_order_acquire) || slot.completion == nullptr) {
      return;
    }
    const auto completion = slot.completion;
    auto* const completion_state = slot.completion_state;
    slot.completion = nullptr;
    slot.completion_state = nullptr;
    slot.in_use.store(false, std::memory_order_release);
    const auto abi_info = ToAbiRpcCallInfo(info);
    const auto abi_context = ToAbiExecutionContext(context);
    try {
      completion(completion_state, ToAbiStatus(status),
                 reinterpret_cast<const uint8_t*>(response.data()), response.size(), &abi_info,
                 &abi_context);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Completion has no error return; keep a foreign exception inside the ABI boundary.
    }
  }

  static void RunWork(void* state, const ExecutionContext& context) noexcept {
    auto& slot = *static_cast<WorkSlot*>(state);
    const auto callback = slot.callback;
    auto* const callback_state = slot.callback_state;
    slot.callback = nullptr;
    slot.callback_state = nullptr;
    slot.in_use.store(false, std::memory_order_release);
    if (callback == nullptr) {
      return;
    }
    const auto abi_context = ToAbiExecutionContext(context);
    try {
      callback(callback_state, &abi_context);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Work callbacks have no error return; keep a foreign exception inside the boundary.
    }
  }

  AsterStatusV1 Post(uint64_t timestamp_ns, bool scheduled, AsterWorkCallbackV1 callback,
                     void* callback_state, const AsterExecutionContextV1* caller) noexcept {
    if (callback == nullptr) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    if (caller == nullptr || caller->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
        caller->struct_size < sizeof(AsterExecutionContextV1)) {
      return ASTER_STATUS_VERSION_MISMATCH_V1;
    }
    if (!ValidExecutionContext(caller)) {
      return ASTER_STATUS_INVALID_ARGUMENT_V1;
    }
    WorkSlot* available{};
    for (auto& slot : work_slots_) {
      bool expected = false;
      if (slot.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        available = &slot;
        break;
      }
    }
    if (available == nullptr) {
      return ASTER_STATUS_CAPACITY_EXCEEDED_V1;
    }
    available->callback = callback;
    available->callback_state = callback_state;
    const auto native_context = ToExecutionContext(*caller);
    const WorkItem work{RunWork, available};
    const auto status = scheduled ? core_.executor().TryPostAt(timestamp_ns, work, native_context)
                                  : core_.executor().TryPost(work, native_context);
    if (!IsOk(status) && available->in_use.load(std::memory_order_acquire)) {
      available->callback = nullptr;
      available->callback_state = nullptr;
      available->in_use.store(false, std::memory_order_release);
    }
    return ToAbiStatus(status);
  }

  const AsterModuleV1* module_{};
  CoreRef core_;
  AsterCoreRefV1 abi_core_{};
  AsterConfiguratorServiceV1 configurator_service_{};
  AsterLoggerServiceV1 logger_service_{};
  AsterExecutorServiceV1 executor_service_{};
  AsterChannelServiceV1 channel_service_{};
  AsterRpcServiceV1 rpc_service_{};
  AsterParameterServiceV1 parameter_service_{};
  AsterClockServiceV1 clock_service_{};
  AsterAllocatorServiceV1 allocator_service_{};
  AsterHardwareManagerServiceV1 hardware_service_{};
  std::array<WorkSlot, 8> work_slots_{};
  std::array<ChannelSlot, 8> channel_slots_{};
  std::array<RpcServerSlot, 8> rpc_server_slots_{};
  std::array<RpcCompletionSlot, 8> rpc_completion_slots_{};
  bool initialized_{};
  bool started_{};
};

PluginLoader::PluginLoader() noexcept = default;

PluginLoader::~PluginLoader() { Close(); }

Status PluginLoader::Open(std::string_view path, CoreRef core) noexcept {
  if (is_open()) {
    return Status::kInvalidState;
  }
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return Status::kInvalidArgument;
  }

  try {
    return OpenImpl(path, core);
  } catch (const std::bad_alloc&) {
    Close();
    return Status::kCapacityExceeded;
  } catch (...) {
    Close();
    return Status::kInternal;
  }
}

Status PluginLoader::OpenImpl(std::string_view path, CoreRef core) {
  const std::string path_string(path);
  handle_ = dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    return Status::kNotFound;
  }

  void* symbol = dlsym(handle_, ASTER_MODULE_BUNDLE_ENTRYPOINT_V1);
  if (symbol == nullptr) {
    Close();
    return Status::kNotFound;
  }
  static_assert(sizeof(AsterModuleBundleEntrypointV1) == sizeof(symbol));
  const auto entrypoint = std::bit_cast<AsterModuleBundleEntrypointV1>(symbol);
  const auto* candidate = entrypoint();
  if (candidate == nullptr || candidate->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      candidate->struct_size < sizeof(AsterModuleBundlePluginV1)) {
    Close();
    return Status::kVersionMismatch;
  }
  bundle_plugin_ = candidate;

  if (!ValidView(bundle_plugin_->name) || !ValidView(bundle_plugin_->version) ||
      bundle_plugin_->create_module_bundle == nullptr) {
    Close();
    return Status::kInvalidArgument;
  }
  name_ = ToView(bundle_plugin_->name);
  version_ = ToView(bundle_plugin_->version);

  const auto create_status =
      FromAbiStatus(bundle_plugin_->create_module_bundle(bundle_plugin_->plugin_state, &bundle_));
  if (!IsOk(create_status)) {
    Close();
    return create_status;
  }
  if (bundle_.abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      bundle_.struct_size < sizeof(AsterModuleBundleV1)) {
    bundle_ = {};
    Close();
    return Status::kVersionMismatch;
  }
  if ((bundle_.module_count != 0 && (bundle_.modules == nullptr || bundle_.release == nullptr)) ||
      (bundle_.module_count == 0 && bundle_.modules != nullptr) ||
      (bundle_.owner != nullptr && bundle_.release == nullptr)) {
    Close();
    return Status::kInvalidArgument;
  }

  adapters_.reserve(bundle_.module_count);
  slots_.reserve(bundle_.module_count);
  for (std::size_t index = 0; index < bundle_.module_count; ++index) {
    if (!ValidModule(bundle_.modules[index])) {
      Close();
      return Status::kInvalidArgument;
    }
    const auto name = ToView(bundle_.modules[index].info.name);
    for (const auto& adapter : adapters_) {
      if (adapter->Info().name == name) {
        Close();
        return Status::kAlreadyExists;
      }
    }
    adapters_.push_back(std::make_unique<CAbiModule>(bundle_.modules[index]));
  }
  for (const auto& adapter : adapters_) {
    slots_.push_back({adapter.get(), core, adapter->Info().name});
  }
  return Status::kOk;
}

void PluginLoader::Close() noexcept {
  slots_.clear();
  adapters_.clear();
  if (bundle_.release != nullptr) {
    try {
      bundle_.release(bundle_.owner, bundle_.modules, bundle_.module_count);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Bundle release is best-effort and cannot report failure through this C ABI.
    }
  }
  bundle_ = {};
  const auto release = bundle_plugin_ != nullptr ? bundle_plugin_->release : nullptr;
  const auto state = bundle_plugin_ != nullptr ? bundle_plugin_->plugin_state : nullptr;
  if (release != nullptr) {
    try {
      release(state);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Plugin release is best-effort and cannot report failure through this C ABI.
    }
  }
  bundle_plugin_ = nullptr;
  name_ = {};
  version_ = {};
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

CorePluginLoader::CorePluginLoader() noexcept = default;

CorePluginLoader::~CorePluginLoader() { Close(); }

Status CorePluginLoader::Open(std::string_view path) noexcept {
  if (is_open()) {
    return Status::kInvalidState;
  }
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return Status::kInvalidArgument;
  }

  try {
    return OpenImpl(path);
  } catch (const std::bad_alloc&) {
    Close();
    return Status::kCapacityExceeded;
  } catch (...) {
    Close();
    return Status::kInternal;
  }
}

Status CorePluginLoader::OpenImpl(std::string_view path) {
  const std::string path_string(path);
  handle_ = dlopen(path_string.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    return Status::kNotFound;
  }

  void* symbol = dlsym(handle_, ASTER_CORE_PLUGIN_ENTRYPOINT_V1);
  if (symbol == nullptr) {
    Close();
    return Status::kNotFound;
  }
  static_assert(sizeof(AsterCorePluginEntrypointV1) == sizeof(symbol));
  const auto entrypoint = std::bit_cast<AsterCorePluginEntrypointV1>(symbol);
  const auto* candidate = entrypoint();
  if (candidate == nullptr || candidate->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      candidate->struct_size < sizeof(AsterCorePluginV1)) {
    Close();
    return Status::kVersionMismatch;
  }
  plugin_ = candidate;
  if (!ValidView(plugin_->name) || !ValidView(plugin_->version) ||
      plugin_->query_interface == nullptr) {
    Close();
    return Status::kInvalidArgument;
  }
  name_ = ToView(plugin_->name);
  version_ = ToView(plugin_->version);
  return Status::kOk;
}

Status CorePluginLoader::QueryInterface(std::string_view name, std::uint32_t version,
                                        std::uint32_t minimum_struct_size,
                                        const void*& interface_table) const noexcept {
  interface_table = nullptr;
  if (plugin_ == nullptr) {
    return Status::kInvalidState;
  }
  if (name.empty() || version == 0 || minimum_struct_size < sizeof(AsterInterfaceHeaderV1)) {
    return Status::kInvalidArgument;
  }

  std::uint32_t struct_size{};
  AsterStatusV1 result{};
  try {
    result = plugin_->query_interface(plugin_->plugin_state, {name.data(), name.size()}, version,
                                      &interface_table, &struct_size);
  } catch (...) {
    interface_table = nullptr;
    return Status::kInternal;
  }
  const auto status = FromAbiStatus(result);
  if (!IsOk(status)) {
    interface_table = nullptr;
    return status;
  }
  if (interface_table == nullptr) {
    return Status::kInternal;
  }
  AsterInterfaceHeaderV1 header{};
  std::memcpy(&header, interface_table, sizeof(header));
  if (header.interface_version != version || header.struct_size != struct_size ||
      struct_size < minimum_struct_size) {
    interface_table = nullptr;
    return Status::kVersionMismatch;
  }
  return Status::kOk;
}

void CorePluginLoader::Close() noexcept {
  if (plugin_ != nullptr && plugin_->release != nullptr) {
    try {
      plugin_->release(plugin_->plugin_state);
    } catch (...) {  // NOLINT(bugprone-empty-catch)
      // Plugin release is best-effort and cannot report failure through this C ABI.
    }
  }
  plugin_ = nullptr;
  name_ = {};
  version_ = {};
  if (handle_ != nullptr) {
    dlclose(handle_);
    handle_ = nullptr;
  }
}

}  // namespace aster::platform::linux
