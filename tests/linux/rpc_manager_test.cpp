#include "aster_runtime/platform/linux/rpc_manager.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "test_types.hpp"

namespace {
struct Result {
  std::mutex mutex;
  std::condition_variable ready;
  bool called{};
  aster::Status status{aster::Status::kInternal};
  std::uint32_t sum{};
};
aster::Status Add(void* state, const test::AddRequest& request, test::AddResponse& response,
                  const aster::RpcCallInfo&, const aster::ExecutionContext& context) noexcept {
  assert(context.executor_name() == "server");
  response.sum = request.left + request.right + *static_cast<std::uint32_t*>(state);
  return aster::Status::kOk;
}
void Complete(void* state, aster::Status status, const test::AddResponse& response,
              const aster::RpcCallInfo&, const aster::ExecutionContext& context) noexcept {
  assert(context.executor_name() == "client" || context.executor_name() == "shutdown");
  auto& result = *static_cast<Result*>(state);
  const std::lock_guard lock(result.mutex);
  assert(!result.called);
  result.called = true;
  result.status = status;
  result.sum = response.sum;
  result.ready.notify_all();
}
void Wait(Result& result, aster::Status status) {
  std::unique_lock lock(result.mutex);
  assert(result.ready.wait_for(lock, std::chrono::seconds(2), [&] { return result.called; }));
  assert(result.status == status);
}
}  // namespace

int main() {
  using namespace aster::platform::linux;
  using aster::Status;
  SteadyClock clock;
  ThreadExecutor<0> server_executor("server", clock, 8, 1);
  ThreadExecutor<0> client_executor("client", clock, 1, 1);
  assert(server_executor.Prepare() == Status::kOk);
  assert(client_executor.Prepare() == Status::kOk);
  RpcManager manager{aster::ClockRef(clock)};
  ModuleConfig left;
  left.name = "left";
  ModuleConfig right;
  right.name = "right";
  ModuleConfig caller;
  caller.name = "caller";
  auto left_endpoint = manager.CreateEndpoint(left, server_executor);
  auto right_endpoint = manager.CreateEndpoint(right, server_executor);
  auto client_endpoint = manager.CreateEndpoint(caller, client_executor);
  aster::RpcServer<test::AddService> left_server;
  aster::RpcServer<test::AddService> right_server;
  aster::RpcClient<test::AddService> client;
  std::uint32_t left_offset = 10;
  std::uint32_t right_offset = 20;
  assert(left_server.Bind(aster::RpcRef(*left_endpoint), Add, &left_offset) == Status::kOk);
  assert(right_server.Bind(aster::RpcRef(*right_endpoint), Add, &right_offset) == Status::kOk);
  assert(client.Bind(aster::RpcRef(*client_endpoint), "right") == Status::kOk);
  assert(manager.Seal() == Status::kOk);
  aster::RpcCompletion<test::AddService> completion;
  aster::RpcCompletion<test::AddService> rejected;
  Result result;
  Result rejected_result;
  test::AddRequest request{1, 2};
  assert(client.CallAsync(request, 0, completion, Complete, &result) == Status::kOk);
  request = {99, 99};
  assert(client.CallAsync(request, 0, rejected, Complete, &rejected_result) ==
         Status::kCapacityExceeded);
  assert(!rejected.pending());
  assert(server_executor.Activate() == Status::kOk);
  assert(client_executor.Activate() == Status::kOk);
  Wait(result, Status::kOk);
  assert(result.sum == 23);
  manager.Close();
  server_executor.Shutdown();
  client_executor.Shutdown();
  manager.CancelPending();
  assert(!rejected_result.called);

  ThreadExecutor<0> delayed_server("server", clock, 8, 1);
  ThreadExecutor<0> active_client("client", clock, 2, 1);
  assert(delayed_server.Prepare() == Status::kOk);
  assert(active_client.Start() == Status::kOk);
  RpcManager deadlines{aster::ClockRef(clock)};
  auto server_endpoint = deadlines.CreateEndpoint(right, delayed_server);
  auto caller_endpoint = deadlines.CreateEndpoint(caller, active_client);
  aster::RpcServer<test::AddService> server;
  aster::RpcClient<test::AddService> deadline_client;
  assert(server.Bind(aster::RpcRef(*server_endpoint), Add, &right_offset) == Status::kOk);
  assert(deadline_client.Bind(aster::RpcRef(*caller_endpoint), "right") == Status::kOk);
  assert(deadlines.Seal() == Status::kOk);
  Result timed_out;
  assert(deadline_client.CallAsync({1, 2}, clock.NowNs() + 20'000'000, completion, Complete,
                                   &timed_out) == Status::kOk);
  Wait(timed_out, Status::kTimeout);  // Server executor never activated.
  Result cancelled;
  aster::RpcCompletion<test::AddService> pending;
  assert(deadline_client.CallAsync({1, 2}, 0, pending, Complete, &cancelled) == Status::kOk);
  deadlines.Close();
  delayed_server.Shutdown();
  active_client.Shutdown();
  deadlines.CancelPending();
  assert(cancelled.called && cancelled.status == Status::kCancelled && !pending.pending());
}
