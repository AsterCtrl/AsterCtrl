#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include "xrobot/runtime/cooperative_executor.hpp"

namespace {

using xrobot::runtime::CooperativeExecutor;
using xrobot::runtime::ExecutionContext;
using xrobot::runtime::ExecutionKind;
using xrobot::runtime::Status;
using xrobot::runtime::WorkItem;

struct Recorder {
  std::array<int, 4> values{};
  std::size_t size{};
  ExecutionKind observed_kind{ExecutionKind::kInterrupt};
  std::string_view observed_executor;
};

struct Payload {
  Recorder* recorder;
  int value;
};

void Record(void* state, const ExecutionContext& context) noexcept {
  auto& payload = *static_cast<Payload*>(state);
  payload.recorder->values[payload.recorder->size++] = payload.value;
  payload.recorder->observed_kind = context.kind();
  payload.recorder->observed_executor = context.executor_name();
}

void ExecutionContextMakesBlockingPolicyExplicit() {
  const ExecutionContext thread("control", ExecutionKind::kThread, 4);
  const ExecutionContext callback("callback", ExecutionKind::kCallback, 6);
  const ExecutionContext interrupt("can-rx", ExecutionKind::kInterrupt, 8);

  assert(thread.blocking_allowed());
  assert(!callback.blocking_allowed());
  assert(!interrupt.blocking_allowed());
  assert(thread.priority() == 4);
}

void ExecutorIsBoundedAndRunsTasksInOrder() {
  CooperativeExecutor<2> executor("control", 5);
  const ExecutionContext caller("main", ExecutionKind::kThread, 1);
  Recorder recorder;
  Payload first{&recorder, 10};
  Payload second{&recorder, 20};
  Payload rejected{&recorder, 30};

  assert(executor.TryPost({Record, &first}, caller) == Status::kInvalidState);
  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(executor.TryPost({Record, &first}, caller) == Status::kOk);
  assert(executor.TryPost({Record, &second}, caller) == Status::kOk);
  assert(executor.TryPost({Record, &rejected}, caller) ==
         Status::kCapacityExceeded);
  assert(executor.pending() == 2);

  assert(executor.RunOne() == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(executor.RunOne() == Status::kUnavailable);
  assert(recorder.size == 2);
  assert(recorder.values[0] == 10);
  assert(recorder.values[1] == 20);
  assert(recorder.observed_kind == ExecutionKind::kThread);
  assert(recorder.observed_executor == "control");

  const auto stats = executor.stats();
  assert(stats.accepted == 2);
  assert(stats.executed == 2);
  assert(stats.rejected == 1);
  assert(stats.high_watermark == 2);
}

void ExecutorRejectsInvalidAndInterruptSubmissions() {
  CooperativeExecutor<1> executor("control", 5);
  const ExecutionContext thread("main", ExecutionKind::kThread, 1);
  const ExecutionContext interrupt("can-rx", ExecutionKind::kInterrupt, 8);
  Recorder recorder;
  Payload payload{&recorder, 10};

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(executor.TryPost(WorkItem{}, thread) == Status::kInvalidArgument);
  assert(executor.TryPost({Record, &payload}, interrupt) ==
         Status::kInvalidArgument);
}

void ShutdownDiscardsQueuedWork() {
  CooperativeExecutor<1> executor("control", 5);
  const ExecutionContext caller("main", ExecutionKind::kThread, 1);
  Recorder recorder;
  Payload payload{&recorder, 10};

  assert(executor.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(executor.TryPost({Record, &payload}, caller) == Status::kOk);
  executor.Shutdown();

  assert(executor.pending() == 0);
  assert(executor.RunOne() == Status::kInvalidState);
  assert(recorder.size == 0);
}

}  // namespace

int main() {
  ExecutionContextMakesBlockingPolicyExplicit();
  ExecutorIsBoundedAndRunsTasksInOrder();
  ExecutorRejectsInvalidAndInterruptSubmissions();
  ShutdownDiscardsQueuedWork();
  return 0;
}
