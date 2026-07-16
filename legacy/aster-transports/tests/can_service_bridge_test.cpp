#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "allocation_tracker.hpp"
#include "aster/runtime/cooperative_executor.hpp"
#include "aster/runtime/service.hpp"
#include "aster/transport/can/service_bridge.hpp"

namespace test {

struct AddRequest {
  std::int16_t left{};
  std::int16_t right{};
};

struct AddResponse {
  std::int32_t sum{};
};

struct Add {};

}  // namespace test

namespace aster::runtime {

template <>
struct TypeSupport<test::AddRequest> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.srv.Add.Request", SchemaHash{{std::byte{0x11}}}, 4};
  }
  static Status Encode(const test::AddRequest& request,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 4) {
      return Status::kCapacityExceeded;
    }
    for (const auto value : {request.left, request.right}) {
      const auto bits = static_cast<std::uint16_t>(value);
      output[written++] = static_cast<std::byte>(bits & 0xffU);
      output[written++] = static_cast<std::byte>(bits >> 8U);
    }
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::AddRequest& request) noexcept {
    if (input.size() != 4) {
      return Status::kInvalidArgument;
    }
    request.left = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(input[0]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(input[1]) << 8U));
    request.right = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(input[2]) |
        static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(input[3]) << 8U));
    return Status::kOk;
  }
};

template <>
struct TypeSupport<test::AddResponse> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.srv.Add.Response", SchemaHash{{std::byte{0x22}}}, 4};
  }
  static Status Encode(const test::AddResponse& response,
                       std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 4) {
      return Status::kCapacityExceeded;
    }
    const auto bits = static_cast<std::uint32_t>(response.sum);
    for (std::size_t index = 0; index < 4; ++index) {
      output[written++] = static_cast<std::byte>(bits >> (index * 8U));
    }
    return Status::kOk;
  }
  static Status Decode(std::span<const std::byte> input,
                       test::AddResponse& response) noexcept {
    if (input.size() != 4) {
      return Status::kInvalidArgument;
    }
    std::uint32_t bits{};
    for (std::size_t index = 0; index < 4; ++index) {
      bits |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    response.sum = static_cast<std::int32_t>(bits);
    return Status::kOk;
  }
};

template <>
struct ServiceTypeSupport<test::Add> {
  using Request = test::AddRequest;
  using Response = test::AddResponse;
  static constexpr ServiceDescriptor descriptor() noexcept {
    return {"test.srv.Add", SchemaHash{{std::byte{0x33}}}};
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
using namespace aster::transport::can;

Status Add(void*, const test::AddRequest& request, test::AddResponse& response,
           const ServiceCallInfo&,
           const ExecutionContext&) noexcept {
  response.sum = request.left + request.right;
  return Status::kOk;
}

struct Completion {
  bool called{};
  Status status{Status::kInternal};
  std::int32_t sum{};
};

void Complete(void* state, Status status, const test::AddResponse& response,
              const ServiceCallInfo&,
              const ExecutionContext&) noexcept {
  auto& completion = *static_cast<Completion*>(state);
  completion.called = true;
  completion.status = status;
  completion.sum = response.sum;
}

struct Clock {
  std::uint64_t now_ns{1'000};
};

std::uint64_t ReadClock(void* state) noexcept {
  return static_cast<Clock*>(state)->now_ns;
}

struct ServiceBus {
  CanServiceClient<test::Add>* client{};
  CanServiceServer<test::Add>* server{};
  const ExecutionContext* client_context{};
  const ExecutionContext* server_context{};
  Clock* clock{};
  std::array<CanFrame, 16> client_to_server{};
  std::array<CanFrame, 16> server_to_client{};
  std::size_t client_to_server_size{};
  std::size_t server_to_client_size{};
  std::size_t dropped_client_frame{static_cast<std::size_t>(-1)};
  std::size_t client_frames_seen{};

  Status QueueClientFrame(const CanFrame& frame) noexcept {
    if (client_to_server_size == client_to_server.size()) {
      return Status::kCapacityExceeded;
    }
    client_to_server[client_to_server_size++] = frame;
    return Status::kOk;
  }

  Status QueueServerFrame(const CanFrame& frame) noexcept {
    if (server_to_client_size == server_to_client.size()) {
      return Status::kCapacityExceeded;
    }
    server_to_client[server_to_client_size++] = frame;
    return Status::kOk;
  }

  void Pump() noexcept {
    while (client_to_server_size != 0 || server_to_client_size != 0) {
      const auto client_count = client_to_server_size;
      client_to_server_size = 0;
      for (std::size_t index = 0; index < client_count; ++index) {
        const auto seen = client_frames_seen++;
        if (seen != dropped_client_frame) {
          server->Accept(client_to_server[index], clock->now_ns,
                         *server_context);
        }
      }
      const auto server_count = server_to_client_size;
      server_to_client_size = 0;
      for (std::size_t index = 0; index < server_count; ++index) {
        client->Accept(server_to_client[index], clock->now_ns,
                       *client_context);
      }
    }
  }
};

Status ClientToServer(void* state, const CanFrame& frame,
                      const ExecutionContext&) noexcept {
  return static_cast<ServiceBus*>(state)->QueueClientFrame(frame);
}

Status ServerToClient(void* state, const CanFrame& frame,
                      const ExecutionContext&) noexcept {
  return static_cast<ServiceBus*>(state)->QueueServerFrame(frame);
}

void ServiceUsesTheSameClientInterfaceAcrossCan() {
  CooperativeExecutor<2> server_executor("server", 4);
  StaticService<test::Add, 1> local_service("add", server_executor, Add,
                                            nullptr);
  Clock clock;
  const ExecutionContext client_context("client", ExecutionKind::kThread, 4);
  const ExecutionContext server_context("server", ExecutionKind::kThread, 4);
  ServiceBus bus;
  CanServiceClient<test::Add> remote_client(
      18, CanPriority::kBackground, {ClientToServer, &bus},
      {ReadClock, &clock});
  CanServiceServer<test::Add> remote_server(
      18, CanPriority::kBackground, local_service.client(),
      {ServerToClient, &bus}, {ReadClock, &clock});
  bus = {&remote_client, &remote_server, &client_context, &server_context,
         &clock};
  Completion completion;

  assert(server_executor.Initialize() == Status::kOk);
  assert(server_executor.Start() == Status::kOk);
  assert(remote_client.client().CallAsync({20, 22}, Complete, &completion,
                                          client_context) == Status::kOk);
  assert(!completion.called);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(completion.called);
  assert(completion.status == Status::kOk);
  assert(completion.sum == 42);
  assert(remote_client.stats().completed == 1);
  assert(remote_server.stats().requests == 1);
}

void ServiceRetriesAfterAFrameIsLost() {
  CooperativeExecutor<2> server_executor("server", 4);
  StaticService<test::Add, 1> local_service("add", server_executor, Add,
                                            nullptr);
  Clock clock;
  const ExecutionContext client_context("client", ExecutionKind::kThread, 4);
  const ExecutionContext server_context("server", ExecutionKind::kThread, 4);
  ServiceBus bus;
  CanServiceClient<test::Add> remote_client(
      18, CanPriority::kBackground, {ClientToServer, &bus},
      {ReadClock, &clock}, 100, 2);
  CanServiceServer<test::Add> remote_server(
      18, CanPriority::kBackground, local_service.client(),
      {ServerToClient, &bus}, {ReadClock, &clock}, 100, 2);
  bus.client = &remote_client;
  bus.server = &remote_server;
  bus.client_context = &client_context;
  bus.server_context = &server_context;
  bus.clock = &clock;
  bus.dropped_client_frame = 0;
  Completion completion;

  assert(server_executor.Initialize() == Status::kOk);
  assert(server_executor.Start() == Status::kOk);
  assert(remote_client.client().CallAsync({10, 32}, Complete, &completion,
                                          client_context) == Status::kOk);
  bus.Pump();
  assert(server_executor.pending() == 0);
  clock.now_ns += 100;
  assert(remote_client.Poll(clock.now_ns, client_context) == Status::kOk);
  bus.Pump();
  assert(server_executor.RunOne() == Status::kOk);
  bus.Pump();
  assert(completion.called);
  assert(completion.sum == 42);
}

}  // namespace

int main() {
  const auto allocations = aster_test::AllocationCount();
  ServiceUsesTheSameClientInterfaceAcrossCan();
  ServiceRetriesAfterAFrameIsLost();
  assert(aster_test::AllocationCount() == allocations);
  return 0;
}
