#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>

#include "aster/runtime/cooperative_executor.hpp"
#include "aster/runtime/service.hpp"

namespace test {

struct SetModeRequest {
  std::uint8_t mode{};
};

struct SetModeResponse {
  bool accepted{};
};

struct SetMode {};

}  // namespace test

namespace aster::runtime {

template <>
struct TypeSupport<test::SetModeRequest> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.srv.SetMode.Request",
            SchemaHash{{std::byte{0x11}, std::byte{0x22}}}, 1};
  }
  static Status Encode(const test::SetModeRequest& message,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(message.mode);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::SetModeRequest& message) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    message.mode = static_cast<std::uint8_t>(input[0]);
    return Status::kOk;
  }
};

template <>
struct TypeSupport<test::SetModeResponse> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.srv.SetMode.Response",
            SchemaHash{{std::byte{0x33}, std::byte{0x44}}}, 1};
  }
  static Status Encode(const test::SetModeResponse& message,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    if (output.empty()) {
      written = 0;
      return Status::kCapacityExceeded;
    }
    output[0] = static_cast<std::byte>(message.accepted ? 1 : 0);
    written = 1;
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::SetModeResponse& message) noexcept {
    if (input.size() != 1) {
      return Status::kInvalidArgument;
    }
    message.accepted = input[0] != std::byte{0};
    return Status::kOk;
  }
};

template <>
struct ServiceTypeSupport<test::SetMode> {
  using Request = test::SetModeRequest;
  using Response = test::SetModeResponse;

  static constexpr ServiceDescriptor descriptor() noexcept {
    return {"test.srv.SetMode",
            SchemaHash{{std::byte{0xaa}, std::byte{0xbb}}}};
  }
};

}  // namespace aster::runtime

namespace {

using aster::runtime::CooperativeExecutor;
using aster::runtime::ExecutionContext;
using aster::runtime::ExecutionKind;
using aster::runtime::ServiceCallInfo;
using aster::runtime::StaticService;
using aster::runtime::Status;

struct ServerState {
  std::uint8_t last_mode{};
  Status result{Status::kOk};
};

struct ClientState {
  bool called{};
  bool accepted{};
  Status status{Status::kInternal};
  std::uint32_t request_id{};
  ExecutionKind context{ExecutionKind::kInterrupt};
};

Status HandleSetMode(void* state, const test::SetModeRequest& request,
                     test::SetModeResponse& response,
                     const ServiceCallInfo&,
                     const ExecutionContext&) noexcept {
  auto& server = *static_cast<ServerState*>(state);
  server.last_mode = request.mode;
  response.accepted = request.mode <= 3;
  return server.result;
}

void CompleteSetMode(void* state, Status status,
                     const test::SetModeResponse& response,
                     const ServiceCallInfo& info,
                     const ExecutionContext& context) noexcept {
  auto& client = *static_cast<ClientState*>(state);
  client.called = true;
  client.accepted = response.accepted;
  client.status = status;
  client.request_id = info.request_id;
  client.context = context.kind();
}

void ServiceCallsAreAsynchronousAndBounded() {
  CooperativeExecutor<2> executor("service", 4);
  ServerState server;
  ClientState first_client;
  ClientState second_client;
  StaticService<test::SetMode, 1> service("system/set-mode", executor,
                                         HandleSetMode, &server);
  const ExecutionContext caller("control", ExecutionKind::kThread, 5);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  const auto client = service.client();
  assert(client.CallAsync({2}, CompleteSetMode, &first_client, caller) ==
         Status::kOk);
  assert(client.CallAsync({3}, CompleteSetMode, &second_client, caller) ==
         Status::kCapacityExceeded);
  assert(!first_client.called);

  assert(executor.RunOne() == Status::kOk);
  assert(first_client.called);
  assert(first_client.accepted);
  assert(first_client.status == Status::kOk);
  assert(first_client.request_id == 1);
  assert(first_client.context == ExecutionKind::kThread);
  assert(server.last_mode == 2);
  assert(service.stats().accepted == 1);
  assert(service.stats().rejected == 1);
  assert(service.stats().completed == 1);
}

void HandlerErrorsReachTheCompletionCallback() {
  CooperativeExecutor<1> executor("service", 4);
  ServerState server{0, Status::kUnavailable};
  ClientState client_state;
  StaticService<test::SetMode, 1> service("system/set-mode", executor,
                                         HandleSetMode, &server);
  const ExecutionContext caller("control", ExecutionKind::kThread, 5);

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(service.client().CallAsync({9}, CompleteSetMode, &client_state,
                                    caller) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(client_state.called);
  assert(client_state.status == Status::kUnavailable);
  assert(!client_state.accepted);
}

void EmptyClientIsUnavailable() {
  const aster::runtime::ServiceClient<test::SetMode> client;
  ClientState state;
  const ExecutionContext caller("control", ExecutionKind::kThread, 5);

  assert(client.CallAsync({1}, CompleteSetMode, &state, caller) ==
         Status::kUnavailable);
}

void UnboundServiceStartsEmptyInNonzeroStorage() {
  using Service = StaticService<test::SetMode, 1>;
  alignas(Service) std::array<std::byte, sizeof(Service)> storage;
  storage.fill(std::byte{0xa5});
  CooperativeExecutor<1> executor("service", 4);
  ServerState server;
  auto* const service = ::new (storage.data()) Service("system/set-mode", executor);

  assert(service->server().BindHandler(HandleSetMode, &server) == Status::kOk);
  service->~Service();
}

}  // namespace

int main() {
  static_assert(aster::runtime::ServiceType<test::SetMode>);
  ServiceCallsAreAsynchronousAndBounded();
  HandlerErrorsReachTheCompletionCallback();
  EmptyClientIsUnavailable();
  UnboundServiceStartsEmptyInNonzeroStorage();
  return 0;
}
