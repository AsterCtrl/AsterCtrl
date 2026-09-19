#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string_view>
#include <thread>

#include "aster_pkg_c_interface/pkg_main.h"

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
  const aster_executor_base_t* executor{};
  const aster_channel_base_t* channel{};
  const aster_rpc_base_t* rpc{};
  std::atomic<uint32_t> channel_calls{};
  std::atomic<uint32_t> rpc_completions{};
  std::atomic<uint32_t> rpc_result{};
  std::atomic<uint32_t> work_completions{};
  std::atomic<uint32_t> concurrent_failures{};
};

struct ModuleState;
Instance& State(void* state) noexcept;

const aster_channel_descriptor_t kChannelDescriptor{
    {"plugin.tick", 11},
    {{"test.Tick", 9}, {{0x21}}, 1},
};

const aster_service_descriptor_t kRpcDescriptor{
    {"plugin.increment", 16},
    {{0x31}},
    {{"test.Increment.Request", 22}, {{0x32}}, 4},
    {{"test.Increment.Response", 23}, {{0x33}}, 4},
    {},
};

aster_status_t ReceiveChannel(void* state, const uint8_t* message, size_t message_size,
                              const aster_message_info_t* info,
                              const aster_execution_context_t* caller) {
  if (state == nullptr || message == nullptr || message_size != 1 || message[0] != 42 ||
      info == nullptr || caller == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }
  State(state).channel_calls.fetch_add(1, std::memory_order_relaxed);
  return ASTER_STATUS_OK;
}

aster_status_t Increment(void*, const uint8_t* request, size_t request_size, uint8_t* response,
                         size_t response_capacity, size_t* response_size,
                         const aster_rpc_call_info_t* info,
                         const aster_execution_context_t* caller) {
  if (request == nullptr || request_size != 4 || response == nullptr || response_capacity < 4 ||
      response_size == nullptr || info == nullptr || info->request_id == 0 || caller == nullptr) {
    return ASTER_STATUS_INTERNAL;
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
  return ASTER_STATUS_OK;
}

void CompleteRpc(void* state, aster_status_t status, const uint8_t* response, size_t response_size,
                 const aster_rpc_call_info_t* info, const aster_execution_context_t* context) {
  if (state == nullptr || status != ASTER_STATUS_OK || response == nullptr || response_size != 4 ||
      info == nullptr || info->request_id == 0 || context == nullptr) {
    return;
  }
  auto& instance = State(state);
  uint32_t result{};
  for (size_t index = 0; index < 4; ++index) {
    result |= static_cast<uint32_t>(response[index]) << (index * 8U);
  }
  instance.rpc_result.store(result, std::memory_order_relaxed);
  instance.rpc_completions.fetch_add(1, std::memory_order_relaxed);
}

void CompleteWork(void* state, const aster_execution_context_t*) {
  State(state).work_completions.fetch_add(1, std::memory_order_relaxed);
}

aster_status_t Initialize(void* state, const aster_core_base_t* core) {
  if (core == nullptr || core->abi_version != ASTER_ABI_VERSION ||
      core->struct_size < sizeof(aster_core_base_t) || core->logger == nullptr ||
      core->configurator == nullptr || core->clock == nullptr || core->executor == nullptr ||
      core->parameter == nullptr || core->allocator == nullptr || core->hardware == nullptr ||
      core->channel == nullptr || core->rpc == nullptr) {
    return ASTER_STATUS_VERSION_MISMATCH;
  }
  const auto* logger = core->logger(core->impl);
  if (logger == nullptr || logger->struct_size < sizeof(aster_logger_base_t) ||
      logger->write == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }
  const aster_execution_context_t context{{"plugin", 6}, ASTER_EXECUTION_KIND_THREAD, 123};
  if (logger->write(logger->impl, ASTER_LOG_LEVEL_INFO, {"plugin initialized", 18}, &context) !=
      ASTER_STATUS_OK) {
    return ASTER_STATUS_INTERNAL;
  }

  const auto* configurator = core->configurator(core->impl);
  aster_value_t answer{};
  if (configurator == nullptr || configurator->struct_size < sizeof(aster_configurator_base_t) ||
      configurator->get == nullptr ||
      configurator->get(configurator->impl, {"answer", 6}, &answer) != ASTER_STATUS_OK ||
      answer.kind != ASTER_VALUE_UINT64 || answer.data.unsigned_integer != 42) {
    return ASTER_STATUS_INTERNAL;
  }

  const auto* clock = core->clock(core->impl);
  uint32_t domain{};
  uint64_t now_ns{};
  if (clock == nullptr || clock->struct_size < sizeof(aster_clock_base_t) ||
      clock->get_domain == nullptr || clock->now_ns == nullptr ||
      clock->get_domain(clock->impl, &domain) != ASTER_STATUS_OK ||
      domain != ASTER_CLOCK_DOMAIN_SIMULATED ||
      clock->now_ns(clock->impl, &now_ns) != ASTER_STATUS_OK || now_ns != 123) {
    return ASTER_STATUS_INTERNAL;
  }

  auto& instance = State(state);
  instance.executor = core->executor(core->impl);
  if (instance.executor == nullptr ||
      instance.executor->struct_size < sizeof(aster_executor_base_t) ||
      instance.executor->get_name == nullptr || instance.executor->try_post == nullptr ||
      instance.executor->try_post_at == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }

  const auto* parameter = core->parameter(core->impl);
  if (parameter == nullptr || parameter->struct_size < sizeof(aster_parameter_base_t) ||
      parameter->get == nullptr || parameter->set == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }

  const auto* allocator = core->allocator(core->impl);
  if (allocator == nullptr || allocator->struct_size < sizeof(aster_allocator_base_t) ||
      allocator->allocate == nullptr || allocator->deallocate == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }

  const auto* hardware = core->hardware(core->impl);
  if (hardware == nullptr || hardware->struct_size < sizeof(aster_hardware_manager_base_t) ||
      hardware->resolve == nullptr) {
    return ASTER_STATUS_INTERNAL;
  }

  instance.channel = core->channel(core->impl);
  auto invalid_channel_descriptor = kChannelDescriptor;
  invalid_channel_descriptor.name = {};
  if (instance.channel == nullptr || instance.channel->struct_size < sizeof(aster_channel_base_t) ||
      instance.channel->register_publisher == nullptr ||
      instance.channel->register_subscriber == nullptr || instance.channel->publish == nullptr ||
      instance.channel->register_publisher(instance.channel->impl, &invalid_channel_descriptor) !=
          ASTER_STATUS_INVALID_ARGUMENT ||
      instance.channel->register_publisher(instance.channel->impl, &kChannelDescriptor) !=
          ASTER_STATUS_OK ||
      instance.channel->register_subscriber(instance.channel->impl, &kChannelDescriptor,
                                            ReceiveChannel, state) != ASTER_STATUS_OK) {
    return ASTER_STATUS_INTERNAL;
  }

  instance.rpc = core->rpc(core->impl);
  auto invalid_rpc_descriptor = kRpcDescriptor;
  invalid_rpc_descriptor.request_type.name = {};
  if (instance.rpc == nullptr || instance.rpc->struct_size < sizeof(aster_rpc_base_t) ||
      instance.rpc->register_client == nullptr || instance.rpc->register_server == nullptr ||
      instance.rpc->call_async == nullptr ||
      instance.rpc->register_client(instance.rpc->impl, &invalid_rpc_descriptor) !=
          ASTER_STATUS_INVALID_ARGUMENT ||
      instance.rpc->register_server(instance.rpc->impl, &kRpcDescriptor, Increment, state) !=
          ASTER_STATUS_OK ||
      instance.rpc->register_client(instance.rpc->impl, &kRpcDescriptor) != ASTER_STATUS_OK) {
    return ASTER_STATUS_INTERNAL;
  }
  Trace('I');
  return ASTER_STATUS_OK;
}

aster_status_t Start(void* state) {
  auto& instance = State(state);
  const aster_execution_context_t context{{"plugin", 6}, ASTER_EXECUTION_KIND_THREAD, 123};
  const uint8_t message = 42;
  if (instance.channel->publish(instance.channel->impl, &kChannelDescriptor, &message, 1, 123,
                                &context) != ASTER_STATUS_OK ||
      instance.channel_calls.load(std::memory_order_relaxed) != 1) {
    return ASTER_STATUS_INTERNAL;
  }
  const uint8_t request[4]{41, 0, 0, 0};
  if (instance.rpc->call_async(instance.rpc->impl, &kRpcDescriptor, request, sizeof(request), 1'000,
                               CompleteRpc, state, &context) != ASTER_STATUS_OK ||
      instance.rpc_completions.load(std::memory_order_relaxed) != 1 ||
      instance.rpc_result.load(std::memory_order_relaxed) != 42) {
    return ASTER_STATUS_INTERNAL;
  }

  constexpr uint32_t kThreadCount = 4;
  constexpr uint32_t kIterations = 250;
  std::array<std::thread, kThreadCount> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      for (uint32_t iteration = 0; iteration < kIterations; ++iteration) {
        if (instance.executor->try_post(instance.executor->impl, CompleteWork, state, &context) !=
                ASTER_STATUS_OK ||
            instance.channel->publish(instance.channel->impl, &kChannelDescriptor, &message, 1, 123,
                                      &context) != ASTER_STATUS_OK ||
            instance.rpc->call_async(instance.rpc->impl, &kRpcDescriptor, request, sizeof(request),
                                     1'000, CompleteRpc, state, &context) != ASTER_STATUS_OK) {
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
    return ASTER_STATUS_INTERNAL;
  }
  Trace('T');
  return ASTER_STATUS_OK;
}

void Shutdown(void*) { Trace('S'); }

aster_module_info_t Info(void*) {
  return {{"loaded", 6}, {"test.Module", 11}, {"test-plugin", 11}, {1, 0, 0}};
}

struct ModuleState {
  Instance instance;
  aster_module_base_t module;

  ModuleState()
      : module{
            ASTER_ABI_VERSION, sizeof(aster_module_base_t), this, Info, Initialize, Start, Shutdown,
        } {}
};

Instance& State(void* state) noexcept { return static_cast<ModuleState*>(state)->instance; }

const aster_string_view_t kModuleNames[]{{"test.Module", 11}};

}  // namespace

extern "C" {

uint32_t AsterDynlibGetAbiVersion() { return ASTER_ABI_VERSION; }

aster_string_view_t AsterDynlibGetPackageName() { return {"test-plugin", 11}; }

aster_string_view_t AsterDynlibGetPackageVersion() { return {"1.0.0", 5}; }

size_t AsterDynlibGetModuleNum() { return 1; }

const aster_string_view_t* AsterDynlibGetModuleNameList() { return kModuleNames; }

const aster_module_base_t* AsterDynlibCreateModule(aster_string_view_t module_name) {
  if (module_name.data == nullptr || module_name.size != kModuleNames[0].size ||
      std::string_view(module_name.data, module_name.size) !=
          std::string_view(kModuleNames[0].data, kModuleNames[0].size)) {
    return nullptr;
  }
  auto* state = new (std::nothrow) ModuleState;
  return state == nullptr ? nullptr : &state->module;
}

void AsterDynlibDestroyModule(const aster_module_base_t* module) {
  if (module != nullptr) {
    Trace('B');
    delete static_cast<ModuleState*>(module->impl);
  }
}
}
