#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

#include "aster/plugin.h"

namespace {

void Trace(char event) noexcept {
  const char* path = std::getenv("ASTER_TEST_PLUGIN_TRACE");
  if (path == nullptr) {
    return;
  }
  if (auto* file = std::fopen(path, "ab"); file != nullptr) {
    std::fwrite(&event, 1, 1, file);
    std::fclose(file);
  }
}

struct Instance {
  const AsterExecutorServiceV1* executor{};
  const AsterChannelServiceV1* channel{};
  const AsterRpcServiceV1* rpc{};
  std::atomic<uint32_t> channel_calls{};
  std::atomic<uint32_t> rpc_completions{};
  std::atomic<uint32_t> rpc_result{};
  std::atomic<uint32_t> work_completions{};
  std::atomic<uint32_t> concurrent_failures{};
};

const AsterChannelDescriptorV1 kChannelDescriptor{
    ASTER_CORE_ABI_VERSION_V1,
    sizeof(AsterChannelDescriptorV1),
    {"plugin.tick", 11},
    {ASTER_CORE_ABI_VERSION_V1, sizeof(AsterTypeDescriptorV1), {"test.Tick", 9}, {{0x21}}, 1},
};

const AsterServiceDescriptorV1 kRpcDescriptor{
    ASTER_CORE_ABI_VERSION_V1,
    sizeof(AsterServiceDescriptorV1),
    {"plugin.increment", 16},
    {{0x31}},
    {ASTER_CORE_ABI_VERSION_V1,
     sizeof(AsterTypeDescriptorV1),
     {"test.Increment.Request", 22},
     {{0x32}},
     4},
    {ASTER_CORE_ABI_VERSION_V1,
     sizeof(AsterTypeDescriptorV1),
     {"test.Increment.Response", 23},
     {{0x33}},
     4},
};

AsterStatusV1 ReceiveChannel(void* state, const uint8_t* message, size_t message_size,
                             const AsterMessageInfoV1* info,
                             const AsterExecutionContextV1* caller) {
  if (state == nullptr || message == nullptr || message_size != 1 || message[0] != 42 ||
      info == nullptr || info->struct_size < sizeof(AsterMessageInfoV1) || caller == nullptr ||
      caller->struct_size < sizeof(AsterExecutionContextV1)) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  static_cast<Instance*>(state)->channel_calls.fetch_add(1, std::memory_order_relaxed);
  return ASTER_STATUS_OK_V1;
}

AsterStatusV1 Increment(void*, const uint8_t* request, size_t request_size, uint8_t* response,
                        size_t response_capacity, size_t* response_size,
                        const AsterRpcCallInfoV1* info, const AsterExecutionContextV1* caller) {
  if (request == nullptr || request_size != 4 || response == nullptr || response_capacity < 4 ||
      response_size == nullptr || info == nullptr || info->request_id == 0 || caller == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  uint32_t value{};
  for (size_t index = 0; index < 4; ++index) {
    value |= static_cast<uint32_t>(request[index]) << (index * 8U);
  }
  ++value;
  for (size_t index = 0; index < 4; ++index) {
    response[index] = static_cast<uint8_t>(value >> (index * 8U));
  }
  *response_size = 4;
  return ASTER_STATUS_OK_V1;
}

void CompleteRpc(void* state, AsterStatusV1 status, const uint8_t* response, size_t response_size,
                 const AsterRpcCallInfoV1* info, const AsterExecutionContextV1* context) {
  if (state == nullptr || status != ASTER_STATUS_OK_V1 || response == nullptr ||
      response_size != 4 || info == nullptr || info->request_id == 0 || context == nullptr) {
    return;
  }
  auto& instance = *static_cast<Instance*>(state);
  uint32_t result{};
  for (size_t index = 0; index < 4; ++index) {
    result |= static_cast<uint32_t>(response[index]) << (index * 8U);
  }
  instance.rpc_result.store(result, std::memory_order_relaxed);
  instance.rpc_completions.fetch_add(1, std::memory_order_relaxed);
}

void CompleteWork(void* state, const AsterExecutionContextV1*) {
  static_cast<Instance*>(state)->work_completions.fetch_add(1, std::memory_order_relaxed);
}

AsterStatusV1 Initialize(void* state, const AsterCoreRefV1* core) {
  if (core == nullptr || core->abi_version != ASTER_CORE_ABI_VERSION_V1 ||
      core->struct_size < sizeof(AsterCoreRefV1) || core->query_service == nullptr) {
    return ASTER_STATUS_VERSION_MISMATCH_V1;
  }
  uint32_t size{123};
  const AsterStringViewV1 unknown{"unknown", 7};
  if (core->query_service(core->context, unknown, 1, &size) != nullptr || size != 0) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  const AsterStringViewV1 logger_name{ASTER_CORE_SERVICE_LOGGER_NAME_V1,
                                      sizeof(ASTER_CORE_SERVICE_LOGGER_NAME_V1) - 1};
  size = 123;
  if (core->query_service(core->context, logger_name, 99, &size) != nullptr || size != 0) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  const auto* logger = static_cast<const AsterLoggerServiceV1*>(
      core->query_service(core->context, logger_name, ASTER_CORE_SERVICE_LOGGER_VERSION_V1, &size));
  if (logger == nullptr || size < sizeof(AsterLoggerServiceV1) ||
      logger->service_version != ASTER_CORE_SERVICE_LOGGER_VERSION_V1 ||
      logger->struct_size < sizeof(AsterLoggerServiceV1) || logger->write == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  const AsterExecutionContextV1 small_context{
      ASTER_CORE_ABI_VERSION_V1,
      static_cast<uint32_t>(offsetof(AsterExecutionContextV1, timestamp_ns)),
      {"plugin", 6},
      ASTER_EXECUTION_KIND_THREAD_V1,
      123};
  if (logger->write(logger->context, ASTER_LOG_LEVEL_INFO_V1, {"must-not-log", 12},
                    &small_context) != ASTER_STATUS_VERSION_MISMATCH_V1) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  const AsterExecutionContextV1 context{ASTER_CORE_ABI_VERSION_V1,
                                        sizeof(AsterExecutionContextV1),
                                        {"plugin", 6},
                                        ASTER_EXECUTION_KIND_THREAD_V1,
                                        123};
  if (logger->write(logger->context, ASTER_LOG_LEVEL_INFO_V1, {"plugin initialized", 18},
                    &context) != ASTER_STATUS_OK_V1) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 configurator_name{ASTER_CORE_SERVICE_CONFIGURATOR_NAME_V1,
                                            sizeof(ASTER_CORE_SERVICE_CONFIGURATOR_NAME_V1) - 1};
  const auto* configurator = static_cast<const AsterConfiguratorServiceV1*>(core->query_service(
      core->context, configurator_name, ASTER_CORE_SERVICE_CONFIGURATOR_VERSION_V1, &size));
  uint32_t answer{};
  size_t written{};
  if (configurator == nullptr || configurator->get == nullptr ||
      configurator->get(configurator->context, {"answer", 6}, reinterpret_cast<uint8_t*>(&answer),
                        sizeof(answer), &written) != ASTER_STATUS_OK_V1 ||
      written != sizeof(answer) || answer != 42) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 clock_name{ASTER_CORE_SERVICE_CLOCK_NAME_V1,
                                     sizeof(ASTER_CORE_SERVICE_CLOCK_NAME_V1) - 1};
  const auto* clock = static_cast<const AsterClockServiceV1*>(
      core->query_service(core->context, clock_name, ASTER_CORE_SERVICE_CLOCK_VERSION_V1, &size));
  uint32_t domain{};
  uint64_t now_ns{};
  if (clock == nullptr || clock->get_domain == nullptr || clock->now_ns == nullptr ||
      clock->get_domain(clock->context, &domain) != ASTER_STATUS_OK_V1 ||
      domain != ASTER_CLOCK_DOMAIN_SIMULATED_V1 ||
      clock->now_ns(clock->context, &now_ns) != ASTER_STATUS_OK_V1 || now_ns != 123) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 executor_name{ASTER_CORE_SERVICE_EXECUTOR_NAME_V1,
                                        sizeof(ASTER_CORE_SERVICE_EXECUTOR_NAME_V1) - 1};
  auto& instance = *static_cast<Instance*>(state);
  instance.executor = static_cast<const AsterExecutorServiceV1*>(core->query_service(
      core->context, executor_name, ASTER_CORE_SERVICE_EXECUTOR_VERSION_V1, &size));
  if (instance.executor == nullptr || size < sizeof(AsterExecutorServiceV1) ||
      instance.executor->service_version != ASTER_CORE_SERVICE_EXECUTOR_VERSION_V1 ||
      instance.executor->struct_size < sizeof(AsterExecutorServiceV1) ||
      instance.executor->get_name == nullptr || instance.executor->try_post == nullptr ||
      instance.executor->try_post_at == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 parameter_name{ASTER_CORE_SERVICE_PARAMETER_NAME_V1,
                                         sizeof(ASTER_CORE_SERVICE_PARAMETER_NAME_V1) - 1};
  const auto* parameter = static_cast<const AsterParameterServiceV1*>(core->query_service(
      core->context, parameter_name, ASTER_CORE_SERVICE_PARAMETER_VERSION_V1, &size));
  if (parameter == nullptr || size < sizeof(AsterParameterServiceV1) || parameter->get == nullptr ||
      parameter->set == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 allocator_name{ASTER_CORE_SERVICE_ALLOCATOR_NAME_V1,
                                         sizeof(ASTER_CORE_SERVICE_ALLOCATOR_NAME_V1) - 1};
  const auto* allocator = static_cast<const AsterAllocatorServiceV1*>(core->query_service(
      core->context, allocator_name, ASTER_CORE_SERVICE_ALLOCATOR_VERSION_V1, &size));
  if (allocator == nullptr || size < sizeof(AsterAllocatorServiceV1) ||
      allocator->allocate == nullptr || allocator->deallocate == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 hardware_name{ASTER_CORE_SERVICE_HARDWARE_MANAGER_NAME_V1,
                                        sizeof(ASTER_CORE_SERVICE_HARDWARE_MANAGER_NAME_V1) - 1};
  const auto* hardware = static_cast<const AsterHardwareManagerServiceV1*>(core->query_service(
      core->context, hardware_name, ASTER_CORE_SERVICE_HARDWARE_MANAGER_VERSION_V1, &size));
  if (hardware == nullptr || size < sizeof(AsterHardwareManagerServiceV1) ||
      hardware->resolve == nullptr) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 channel_name{ASTER_CORE_SERVICE_CHANNEL_NAME_V1,
                                       sizeof(ASTER_CORE_SERVICE_CHANNEL_NAME_V1) - 1};
  instance.channel = static_cast<const AsterChannelServiceV1*>(core->query_service(
      core->context, channel_name, ASTER_CORE_SERVICE_CHANNEL_VERSION_V1, &size));
  auto small_channel_descriptor = kChannelDescriptor;
  small_channel_descriptor.struct_size =
      static_cast<uint32_t>(offsetof(AsterChannelDescriptorV1, message_type));
  if (instance.channel == nullptr || size < sizeof(AsterChannelServiceV1) ||
      instance.channel->register_publisher == nullptr ||
      instance.channel->register_subscriber == nullptr || instance.channel->publish == nullptr ||
      instance.channel->register_publisher(instance.channel->context, &small_channel_descriptor) !=
          ASTER_STATUS_VERSION_MISMATCH_V1 ||
      instance.channel->register_publisher(instance.channel->context, &kChannelDescriptor) !=
          ASTER_STATUS_OK_V1 ||
      instance.channel->register_subscriber(instance.channel->context, &kChannelDescriptor,
                                            ReceiveChannel, &instance) != ASTER_STATUS_OK_V1) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  const AsterStringViewV1 rpc_name{ASTER_CORE_SERVICE_RPC_NAME_V1,
                                   sizeof(ASTER_CORE_SERVICE_RPC_NAME_V1) - 1};
  instance.rpc = static_cast<const AsterRpcServiceV1*>(
      core->query_service(core->context, rpc_name, ASTER_CORE_SERVICE_RPC_VERSION_V1, &size));
  auto small_rpc_descriptor = kRpcDescriptor;
  small_rpc_descriptor.request_type.struct_size =
      static_cast<uint32_t>(offsetof(AsterTypeDescriptorV1, schema_hash));
  if (instance.rpc == nullptr || size < sizeof(AsterRpcServiceV1) ||
      instance.rpc->register_client == nullptr || instance.rpc->register_server == nullptr ||
      instance.rpc->call_async == nullptr ||
      instance.rpc->register_client(instance.rpc->context, &small_rpc_descriptor) !=
          ASTER_STATUS_VERSION_MISMATCH_V1 ||
      instance.rpc->register_server(instance.rpc->context, &kRpcDescriptor, Increment, &instance) !=
          ASTER_STATUS_OK_V1 ||
      instance.rpc->register_client(instance.rpc->context, &kRpcDescriptor) != ASTER_STATUS_OK_V1) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  Trace('I');
  return ASTER_STATUS_OK_V1;
}

AsterStatusV1 Start(void* state) {
  auto& instance = *static_cast<Instance*>(state);
  const AsterExecutionContextV1 context{ASTER_CORE_ABI_VERSION_V1,
                                        sizeof(AsterExecutionContextV1),
                                        {"plugin", 6},
                                        ASTER_EXECUTION_KIND_THREAD_V1,
                                        123};
  const uint8_t message = 42;
  if (instance.channel->publish(instance.channel->context, &kChannelDescriptor, &message, 1, 123,
                                &context) != ASTER_STATUS_OK_V1 ||
      instance.channel_calls.load(std::memory_order_relaxed) != 1) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  const uint8_t request[4]{41, 0, 0, 0};
  if (instance.rpc->call_async(instance.rpc->context, &kRpcDescriptor, request, sizeof(request),
                               1'000, CompleteRpc, &instance, &context) != ASTER_STATUS_OK_V1 ||
      instance.rpc_completions.load(std::memory_order_relaxed) != 1 ||
      instance.rpc_result.load(std::memory_order_relaxed) != 42) {
    return ASTER_STATUS_INTERNAL_V1;
  }

  constexpr uint32_t kThreadCount = 4;
  constexpr uint32_t kIterations = 250;
  std::array<std::thread, kThreadCount> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        if (instance.executor->try_post(instance.executor->context, CompleteWork, &instance,
                                        &context) != ASTER_STATUS_OK_V1 ||
            instance.channel->publish(instance.channel->context, &kChannelDescriptor, &message, 1,
                                      123, &context) != ASTER_STATUS_OK_V1 ||
            instance.rpc->call_async(instance.rpc->context, &kRpcDescriptor, request,
                                     sizeof(request), 1'000, CompleteRpc, &instance,
                                     &context) != ASTER_STATUS_OK_V1) {
          instance.concurrent_failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  constexpr auto kConcurrentCalls = kThreadCount * kIterations;
  if (instance.concurrent_failures.load(std::memory_order_relaxed) != 0 ||
      instance.work_completions.load(std::memory_order_relaxed) != kConcurrentCalls ||
      instance.channel_calls.load(std::memory_order_relaxed) != kConcurrentCalls + 1 ||
      instance.rpc_completions.load(std::memory_order_relaxed) != kConcurrentCalls + 1 ||
      instance.rpc_result.load(std::memory_order_relaxed) != 42) {
    return ASTER_STATUS_INTERNAL_V1;
  }
  Trace('T');
  return ASTER_STATUS_OK_V1;
}

void Shutdown(void*) { Trace('S'); }

struct BundleOwner {
  Instance instance;
  AsterModuleV1 module;

  BundleOwner()
      : module{
            ASTER_CORE_ABI_VERSION_V1,
            sizeof(AsterModuleV1),
            {{"loaded", 6}, {"test.Module", 11}, {"test-plugin", 11}, {1, 0, 0}},
            &instance,
            Initialize,
            Start,
            Shutdown,
        } {}
};

AsterStatusV1 CreateBundle(void*, AsterModuleBundleV1* bundle) {
  if (bundle == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT_V1;
  }
  auto* owner = new (std::nothrow) BundleOwner;
  if (owner == nullptr) {
    return ASTER_STATUS_CAPACITY_EXCEEDED_V1;
  }
  *bundle = {
      ASTER_CORE_ABI_VERSION_V1,
      sizeof(AsterModuleBundleV1),
      &owner->module,
      1,
      owner,
      [](void* state, const AsterModuleV1*, size_t) {
        Trace('B');
        delete static_cast<BundleOwner*>(state);
      },
  };
  return ASTER_STATUS_OK_V1;
}

void ReleasePlugin(void*) { Trace('P'); }

const AsterModuleBundlePluginV1 kPlugin{
    ASTER_CORE_ABI_VERSION_V1,
    sizeof(AsterModuleBundlePluginV1),
    {"test-plugin", 11},
    {"1.0.0", 5},
    nullptr,
    CreateBundle,
    ReleasePlugin,
};

}  // namespace

extern "C" const AsterModuleBundlePluginV1* aster_module_bundle_v1() { return &kPlugin; }
