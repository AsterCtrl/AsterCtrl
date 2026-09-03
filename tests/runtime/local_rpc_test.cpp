#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "aster/rpc.hpp"
#include "test_types.hpp"

namespace {

class ManualExecutor final : public aster::Executor {
 public:
  [[nodiscard]] std::string_view Name() const noexcept override { return "manual"; }

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
  std::array<aster::WorkItem, 2> queue_{};
  std::size_t size_{};
};

aster::Status Add(void* state, const test::AddRequest& request, test::AddResponse& response,
                  const aster::RpcCallInfo& info, const aster::ExecutionContext&) noexcept {
  assert(info.request_id != 0);
  ++*static_cast<int*>(state);
  response.sum = request.left + request.right;
  return aster::Status::kOk;
}

struct CompletionResult {
  bool called{};
  aster::Status status{aster::Status::kInternal};
  std::uint32_t sum{};
  std::uint32_t request_id{};
};

struct ReentrantResult {
  aster::RpcClient<test::AddService>* client{};
  aster::RpcCompletion<test::AddService>* completion{};
  const aster::ExecutionContext* caller{};
  std::array<std::uint32_t, 2> sums{};
  std::size_t count{};
  aster::Status second_call_status{aster::Status::kInternal};
};

void Complete(void* state, aster::Status status, const test::AddResponse& response,
              const aster::RpcCallInfo& info, const aster::ExecutionContext& context) noexcept {
  auto& result = *static_cast<CompletionResult*>(state);
  result.called = true;
  result.status = status;
  result.sum = response.sum;
  result.request_id = info.request_id;
  assert(context.executor_name() == "manual");
}

void CompleteReentrantly(void* state, aster::Status status, const test::AddResponse& response,
                         const aster::RpcCallInfo&, const aster::ExecutionContext&) noexcept {
  auto& result = *static_cast<ReentrantResult*>(state);
  assert(status == aster::Status::kOk);
  result.sums[result.count++] = response.sum;
  if (result.count == 1) {
    result.second_call_status = result.client->CallAsync(
        {3, 4}, 300, *result.completion, CompleteReentrantly, &result, *result.caller);
  }
}

}  // namespace

int main() {
  ManualExecutor executor;
  aster::LocalRpc<2, 16, 16, 1> rpc{aster::ExecutorRef(executor)};
  aster::RpcServer<test::AddService> server;
  aster::RpcClient<test::AddService> client;
  int handler_calls{};
  assert(server.Bind(aster::RpcRef(rpc), Add, &handler_calls) == aster::Status::kOk);
  assert(client.Bind(aster::RpcRef(rpc)) == aster::Status::kOk);
  assert(rpc.Seal() == aster::Status::kOk);

  const aster::ExecutionContext caller("test", aster::ExecutionKind::kThread, 100);
  aster::RpcCompletion<test::AddService> first;
  CompletionResult first_result;
  assert(client.CallAsync({20, 22}, 200, first, Complete, &first_result, caller) ==
         aster::Status::kOk);
  assert(first.pending());
  assert(!first_result.called);
  assert(rpc.pending_count() == 1);

  aster::RpcCompletion<test::AddService> capacity_limited;
  CompletionResult capacity_result;
  assert(client.CallAsync({1, 1}, 200, capacity_limited, Complete, &capacity_result, caller) ==
         aster::Status::kCapacityExceeded);
  assert(!capacity_limited.pending());
  assert(!capacity_result.called);

  executor.RunNext(150);
  assert(first_result.called);
  assert(first_result.status == aster::Status::kOk);
  assert(first_result.sum == 42);
  assert(first_result.request_id != 0);
  assert(!first.pending());
  assert(rpc.pending_count() == 0);
  assert(handler_calls == 1);

  aster::RpcCompletion<test::AddService> expires_in_queue;
  CompletionResult timeout_result;
  assert(client.CallAsync({1, 1}, 150, expires_in_queue, Complete, &timeout_result, caller) ==
         aster::Status::kOk);
  executor.RunNext(150);
  assert(timeout_result.called);
  assert(timeout_result.status == aster::Status::kTimeout);
  assert(!expires_in_queue.pending());
  assert(handler_calls == 1);

  aster::RpcCompletion<test::AddService> already_expired;
  CompletionResult expired_result;
  assert(client.CallAsync({1, 1}, 100, already_expired, Complete, &expired_result, caller) ==
         aster::Status::kTimeout);
  assert(!already_expired.pending());
  assert(!expired_result.called);

  assert(rpc.stats().calls == 2);
  assert(rpc.stats().completed == 2);
  assert(rpc.stats().failures == 1);
  assert(rpc.stats().timeouts == 1);
  assert(rpc.stats().pending_high_watermark == 1);

  aster::RpcCompletion<test::AddService> reentrant;
  ReentrantResult reentrant_result{&client, &reentrant, &caller};
  assert(client.CallAsync({1, 2}, 300, reentrant, CompleteReentrantly, &reentrant_result, caller) ==
         aster::Status::kOk);
  executor.RunNext(200);
  assert(reentrant_result.second_call_status == aster::Status::kOk);
  assert(reentrant_result.count == 1);
  assert(reentrant_result.sums[0] == 3);
  assert(reentrant.pending());
  assert(rpc.pending_count() == 1);
  executor.RunNext(200);
  assert(reentrant_result.count == 2);
  assert(reentrant_result.sums[1] == 7);
  assert(!reentrant.pending());
  assert(rpc.pending_count() == 0);

  aster::LocalRpc<1, 16, 16> missing_server{aster::ExecutorRef(executor)};
  aster::RpcClient<test::AddService> orphan;
  assert(orphan.Bind(aster::RpcRef(missing_server)) == aster::Status::kOk);
  assert(missing_server.Seal() == aster::Status::kUnavailable);
}
