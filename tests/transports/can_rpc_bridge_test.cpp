#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "allocation_tracker.hpp"
#include "aster/rpc.hpp"
#include "aster/transport/can/rpc_bridge.hpp"
#include "test_types.hpp"

namespace {

class ManualExecutor final : public aster::Executor {
 public:
  [[nodiscard]] std::string_view Name() const noexcept override { return "rpc-server"; }

  aster::Status TryPost(aster::WorkItem work, const aster::ExecutionContext&) noexcept override {
    if (!work) {
      return aster::Status::kInvalidArgument;
    }
    if (size_ == queue_.size()) {
      return aster::Status::kCapacityExceeded;
    }
    queue_[size_++] = work;
    return aster::Status::kOk;
  }

  aster::Status TryPostAt(std::uint64_t, aster::WorkItem work,
                          const aster::ExecutionContext& caller) noexcept override {
    return TryPost(work, caller);
  }

  [[nodiscard]] std::size_t pending() const noexcept { return size_; }

  void RunNext(std::uint64_t timestamp_ns) noexcept {
    assert(size_ != 0);
    const auto work = queue_[0];
    for (std::size_t index = 1; index < size_; ++index) {
      queue_[index - 1] = queue_[index];
    }
    --size_;
    work.Run({Name(), aster::ExecutionKind::kThread, timestamp_ns});
  }

 private:
  std::array<aster::WorkItem, 4> queue_{};
  std::size_t size_{};
};

aster::Status Add(void*, const test::AddRequest& request, test::AddResponse& response,
                  const aster::RpcCallInfo&, const aster::ExecutionContext&) noexcept {
  response.sum = request.left + request.right;
  return aster::Status::kOk;
}

struct CompletionResult {
  bool called{};
  aster::Status status{aster::Status::kInternal};
  std::uint32_t sum{};
  std::uint32_t request_id{};
  std::uint64_t deadline_ns{};
};

void Complete(void* state, aster::Status status, const test::AddResponse& response,
              const aster::RpcCallInfo& info, const aster::ExecutionContext&) noexcept {
  auto& result = *static_cast<CompletionResult*>(state);
  result.called = true;
  result.status = status;
  result.sum = response.sum;
  result.request_id = info.request_id;
  result.deadline_ns = info.deadline_ns;
}

struct Clock {
  std::uint64_t now_ns{1'000};

  static std::uint64_t Read(void* state) noexcept { return static_cast<Clock*>(state)->now_ns; }
};

using RpcClientBridge = aster::transport::can::CanRpcClient<test::AddService>;
using RpcServerBridge = aster::transport::can::CanRpcServer<test::AddService>;
using CanFrame = aster::transport::can::CanFrame;

void EveryStatusHasAStableCanWireCode() {
  constexpr std::array statuses{
      aster::Status::kOk,
      aster::Status::kInvalidArgument,
      aster::Status::kInvalidState,
      aster::Status::kCapacityExceeded,
      aster::Status::kUnavailable,
      aster::Status::kTimeout,
      aster::Status::kCancelled,
      aster::Status::kTypeMismatch,
      aster::Status::kNotFound,
      aster::Status::kAlreadyExists,
      aster::Status::kVersionMismatch,
      aster::Status::kInternal,
      aster::Status::kProtocolError,
  };
  for (std::size_t index = 0; index < statuses.size(); ++index) {
    std::byte encoded{};
    assert(aster::transport::can::rpc_detail::EncodeStatus(statuses[index], encoded));
    assert(std::to_integer<std::size_t>(encoded) == index);
    aster::Status decoded{aster::Status::kInternal};
    assert(aster::transport::can::rpc_detail::DecodeStatus(encoded, decoded));
    assert(decoded == statuses[index]);
  }

  aster::Status decoded{aster::Status::kOk};
  assert(!aster::transport::can::rpc_detail::DecodeStatus(std::byte{0xff}, decoded));
  assert(decoded == aster::Status::kInternal);
}

struct Bus {
  RpcClientBridge* client{};
  RpcServerBridge* server{};
  Clock* clock{};
  const aster::ExecutionContext* client_context{};
  const aster::ExecutionContext* server_context{};
  std::array<CanFrame, 16> client_to_server{};
  std::array<CanFrame, 16> server_to_client{};
  std::size_t client_to_server_size{};
  std::size_t server_to_client_size{};
  std::size_t dropped_client_frame{static_cast<std::size_t>(-1)};
  std::size_t client_frames_seen{};

  static aster::Status WriteClient(void* state, const CanFrame& frame,
                                   const aster::ExecutionContext&) noexcept {
    auto& bus = *static_cast<Bus*>(state);
    if (bus.client_to_server_size == bus.client_to_server.size()) {
      return aster::Status::kCapacityExceeded;
    }
    bus.client_to_server[bus.client_to_server_size++] = frame;
    return aster::Status::kOk;
  }

  static aster::Status WriteServer(void* state, const CanFrame& frame,
                                   const aster::ExecutionContext&) noexcept {
    auto& bus = *static_cast<Bus*>(state);
    if (bus.server_to_client_size == bus.server_to_client.size()) {
      return aster::Status::kCapacityExceeded;
    }
    bus.server_to_client[bus.server_to_client_size++] = frame;
    return aster::Status::kOk;
  }

  void Pump() noexcept {
    while (client_to_server_size != 0 || server_to_client_size != 0) {
      const auto request_frame_count = client_to_server_size;
      client_to_server_size = 0;
      for (std::size_t index = 0; index < request_frame_count; ++index) {
        const auto frame_number = client_frames_seen++;
        if (frame_number == dropped_client_frame) {
          continue;
        }
        const auto status = server->Accept(client_to_server[index], clock->now_ns, *server_context);
        assert(status == aster::Status::kOk || status == aster::Status::kUnavailable);
      }

      const auto response_frame_count = server_to_client_size;
      server_to_client_size = 0;
      for (std::size_t index = 0; index < response_frame_count; ++index) {
        const auto status = client->Accept(server_to_client[index], clock->now_ns, *client_context);
        assert(status == aster::Status::kOk || status == aster::Status::kUnavailable);
      }
    }
  }
};

void RpcUsesTheSameAsyncInterfaceAcrossCan() {
  ManualExecutor server_executor;
  aster::LocalRpc<1, 8, 4, 1> server_rpc{aster::ExecutorRef(server_executor)};
  aster::RpcServer<test::AddService> service;
  Clock clock;
  const aster::ExecutionContext client_context("can-client", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  const aster::ExecutionContext server_context("can-server", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  Bus bus;
  RpcClientBridge client_bridge(18, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteClient, &bus}, {Clock::Read, &clock});
  RpcServerBridge server_bridge(18, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteServer, &bus}, {Clock::Read, &clock});
  bus = {&client_bridge, &server_bridge, &clock, &client_context, &server_context};
  aster::RpcClient<test::AddService> client;
  aster::RpcCompletion<test::AddService> completion;
  CompletionResult result;

  assert(service.Bind(aster::RpcRef(server_rpc), Add, nullptr) == aster::Status::kOk);
  assert(server_bridge.Bind(aster::RpcRef(server_rpc)) == aster::Status::kOk);
  assert(client.Bind(aster::RpcRef(client_bridge)) == aster::Status::kOk);
  assert(server_rpc.Seal() == aster::Status::kOk);
  assert(client_bridge.Seal() == aster::Status::kOk);

  constexpr std::uint64_t deadline_ns = 2'000;
  assert(client.CallAsync({20, 22}, deadline_ns, completion, Complete, &result, client_context) ==
         aster::Status::kOk);
  assert(completion.pending());
  bus.Pump();
  assert(server_executor.pending() == 1);
  assert(!result.called);

  clock.now_ns = 1'500;
  server_executor.RunNext(clock.now_ns);
  bus.Pump();
  assert(result.called);
  assert(result.status == aster::Status::kOk);
  assert(result.sum == 42);
  assert(result.request_id != 0);
  assert(result.deadline_ns == deadline_ns);
  assert(!completion.pending());
  assert(client_bridge.stats().completed == 1);
  assert(server_bridge.stats().requests == 1);
}

void RpcRetriesAndBoundsPendingCalls() {
  ManualExecutor server_executor;
  aster::LocalRpc<1, 8, 4, 1> server_rpc{aster::ExecutorRef(server_executor)};
  aster::RpcServer<test::AddService> service;
  Clock clock;
  const aster::ExecutionContext client_context("can-client", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  const aster::ExecutionContext server_context("can-server", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  Bus bus;
  RpcClientBridge client_bridge(19, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteClient, &bus}, {Clock::Read, &clock}, 100, 2);
  RpcServerBridge server_bridge(19, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteServer, &bus}, {Clock::Read, &clock}, 100, 2);
  bus = {&client_bridge, &server_bridge, &clock, &client_context, &server_context};
  bus.dropped_client_frame = 0;
  aster::RpcClient<test::AddService> client;
  aster::RpcCompletion<test::AddService> first;
  aster::RpcCompletion<test::AddService> over_capacity;
  CompletionResult first_result;
  CompletionResult capacity_result;

  assert(service.Bind(aster::RpcRef(server_rpc), Add, nullptr) == aster::Status::kOk);
  assert(server_bridge.Bind(aster::RpcRef(server_rpc)) == aster::Status::kOk);
  assert(client.Bind(aster::RpcRef(client_bridge)) == aster::Status::kOk);
  assert(server_rpc.Seal() == aster::Status::kOk);
  assert(client_bridge.Seal() == aster::Status::kOk);

  assert(client.CallAsync({10, 32}, 2'000, first, Complete, &first_result, client_context) ==
         aster::Status::kOk);
  assert(client.CallAsync({1, 1}, 2'000, over_capacity, Complete, &capacity_result,
                          client_context) == aster::Status::kCapacityExceeded);
  assert(!over_capacity.pending());
  assert(!capacity_result.called);
  bus.Pump();
  assert(server_executor.pending() == 0);

  clock.now_ns += 100;
  assert(client_bridge.Poll(clock.now_ns, client_context) == aster::Status::kOk);
  bus.Pump();
  assert(server_executor.pending() == 1);
  server_executor.RunNext(clock.now_ns);
  bus.Pump();
  assert(first_result.called);
  assert(first_result.status == aster::Status::kOk);
  assert(first_result.sum == 42);
  assert(client_bridge.reliable_stats().retries == 1);
  assert(client_bridge.stats().rejected == 1);
  assert(server_bridge.stats().requests == 1);
}

void RpcHonorsTheWireDeadlineAndIgnoresALateResponse() {
  ManualExecutor server_executor;
  aster::LocalRpc<1, 8, 4, 1> server_rpc{aster::ExecutorRef(server_executor)};
  aster::RpcServer<test::AddService> service;
  Clock clock;
  const aster::ExecutionContext client_context("can-client", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  const aster::ExecutionContext server_context("can-server", aster::ExecutionKind::kThread,
                                               clock.now_ns);
  Bus bus;
  RpcClientBridge client_bridge(20, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteClient, &bus}, {Clock::Read, &clock}, 100, 2);
  RpcServerBridge server_bridge(20, aster::transport::can::CanPriority::kBackground,
                                {Bus::WriteServer, &bus}, {Clock::Read, &clock}, 100, 2);
  bus = {&client_bridge, &server_bridge, &clock, &client_context, &server_context};
  aster::RpcClient<test::AddService> client;
  aster::RpcCompletion<test::AddService> completion;
  CompletionResult result;

  assert(service.Bind(aster::RpcRef(server_rpc), Add, nullptr) == aster::Status::kOk);
  assert(server_bridge.Bind(aster::RpcRef(server_rpc)) == aster::Status::kOk);
  assert(client.Bind(aster::RpcRef(client_bridge)) == aster::Status::kOk);
  assert(server_rpc.Seal() == aster::Status::kOk);
  assert(client_bridge.Seal() == aster::Status::kOk);

  constexpr std::uint64_t deadline_ns = 1'100;
  assert(client.CallAsync({20, 22}, deadline_ns, completion, Complete, &result, client_context) ==
         aster::Status::kOk);
  bus.Pump();
  assert(server_executor.pending() == 1);

  clock.now_ns = deadline_ns;
  assert(client_bridge.Poll(clock.now_ns, client_context) == aster::Status::kTimeout);
  assert(result.called);
  assert(result.status == aster::Status::kTimeout);
  assert(result.deadline_ns == deadline_ns);
  assert(!completion.pending());

  server_executor.RunNext(clock.now_ns);
  assert(bus.server_to_client_size != 0);
  assert(bus.server_to_client[0].data[2] == std::byte{5});
  bus.Pump();
  assert(result.status == aster::Status::kTimeout);
  assert(client_bridge.stats().completed == 1);
  assert(client_bridge.stats().timeouts == 1);
  assert(server_bridge.stats().responses == 1);
}

}  // namespace

int main() {
  const auto allocations = aster_test::AllocationCount();
  EveryStatusHasAStableCanWireCode();
  RpcUsesTheSameAsyncInterfaceAcrossCan();
  RpcRetriesAndBoundsPendingCalls();
  RpcHonorsTheWireDeadlineAndIgnoresALateResponse();
  assert(aster_test::AllocationCount() == allocations);
  return 0;
}
