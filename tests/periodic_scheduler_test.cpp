#include <array>
#include <cassert>
#include <cstdint>

#include "aster/runtime/cooperative_executor.hpp"
#include "aster/runtime/module.hpp"
#include "aster/runtime/periodic_scheduler.hpp"
#include "aster/runtime/runtime.hpp"

namespace {

using aster::runtime::CooperativeExecutor;
using aster::runtime::ExecutionContext;
using aster::runtime::ExecutionKind;
using aster::runtime::Module;
using aster::runtime::ModuleContext;
using aster::runtime::ModuleServices;
using aster::runtime::Status;
using aster::runtime::StaticPeriodicScheduler;

void Increment(void* state, const ExecutionContext&) noexcept {
  ++*static_cast<std::uint32_t*>(state);
}

void RequiresEveryTaskToBeBound() {
  CooperativeExecutor<1> executor("control", 5);
  StaticPeriodicScheduler<1> scheduler("periodic");
  assert(scheduler.AddTask("module", "control", 1'000'000, executor) ==
         Status::kOk);
  assert(scheduler.Initialize() == Status::kInvalidState);
  assert(scheduler.state() ==
         aster::runtime::PeriodicSchedulerState::kFailed);
}

void SkipsMissedPeriodsAndPreventsReentry() {
  CooperativeExecutor<2> executor("control", 5);
  StaticPeriodicScheduler<1> scheduler("periodic");
  const ExecutionContext caller("runtime", ExecutionKind::kThread, 9);
  std::uint32_t calls{};

  assert(scheduler.AddTask("module", "control", 10, executor) == Status::kOk);
  assert(scheduler.BindPeriodicTask("module", "control",
                                    {Increment, &calls}) == Status::kOk);
  assert(executor.Initialize() == Status::kOk);
  assert(scheduler.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(scheduler.Start() == Status::kOk);

  assert(scheduler.Poll(100, caller) == Status::kOk);
  assert(scheduler.Poll(125, caller) == Status::kOk);
  auto stats = scheduler.task_stats(0);
  assert(stats.releases == 1);
  assert(stats.skipped == 2);
  assert(stats.pending);

  assert(executor.RunOne() == Status::kOk);
  assert(calls == 1);
  assert(scheduler.Poll(130, caller) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(calls == 2);

  stats = scheduler.task_stats(0);
  assert(stats.releases == 2);
  assert(stats.completed == 2);
  assert(stats.skipped == 2);
  assert(!stats.pending);
  assert(scheduler.Poll(129, caller) == Status::kInvalidArgument);
}

void ReportsExecutorBackpressureAndRejectsInterruptPolling() {
  CooperativeExecutor<1> executor("control", 5);
  StaticPeriodicScheduler<1> scheduler("periodic");
  const ExecutionContext caller("runtime", ExecutionKind::kThread, 9);
  const ExecutionContext interrupt("timer-isr", ExecutionKind::kInterrupt, 9);
  std::uint32_t calls{};
  std::uint32_t occupying_calls{};

  assert(scheduler.AddTask("module", "control", 10, executor) == Status::kOk);
  assert(scheduler.BindPeriodicTask("module", "control",
                                    {Increment, &calls}) == Status::kOk);
  assert(executor.Initialize() == Status::kOk);
  assert(scheduler.Initialize() == Status::kOk);
  assert(executor.Start() == Status::kOk);
  assert(scheduler.Start() == Status::kOk);
  assert(executor.TryPost({Increment, &occupying_calls}, caller) == Status::kOk);

  assert(scheduler.Poll(100, interrupt) == Status::kInvalidArgument);
  assert(scheduler.Poll(100, caller) == Status::kOk);
  auto stats = scheduler.task_stats(0);
  assert(stats.releases == 0);
  assert(stats.schedule_failures == 1);
  assert(!stats.pending);

  assert(executor.RunOne() == Status::kOk);
  assert(occupying_calls == 1);
  assert(scheduler.Poll(110, caller) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(calls == 1);
}

class PeriodicModule final : public Module {
 public:
  std::string_view Name() const noexcept override { return "module"; }

  Status Initialize(ModuleContext& context) noexcept override {
    return context.BindPeriodicTask("control", {Increment, &calls_});
  }

  Status Start() noexcept override { return Status::kOk; }
  void Shutdown() noexcept override {}

  std::uint32_t calls() const noexcept { return calls_; }

 private:
  std::uint32_t calls_{};
};

void RuntimeOwnsPeriodicSchedulingLifecycle() {
  CooperativeExecutor<2> executor("control", 5);
  StaticPeriodicScheduler<1> scheduler("periodic");
  PeriodicModule module;
  ModuleContext context(
      "node", "module",
      ModuleServices{.executor = &executor, .periodic_tasks = &scheduler});
  std::array executors{aster::runtime::ExecutorSlot{&executor}};
  std::array modules{aster::runtime::ModuleSlot{&module, &context}};
  aster::runtime::Runtime runtime(executors, modules, scheduler);
  const ExecutionContext caller("runtime", ExecutionKind::kThread, 9);

  assert(scheduler.AddTask("module", "control", 1'000'000, executor) ==
         Status::kOk);
  assert(runtime.Initialize() == Status::kOk);
  assert(runtime.Start() == Status::kOk);
  assert(runtime.Poll(1'000'000, caller) == Status::kOk);
  assert(executor.RunOne() == Status::kOk);
  assert(module.calls() == 1);

  runtime.Shutdown();
  assert(scheduler.state() ==
         aster::runtime::PeriodicSchedulerState::kStopped);
  assert(runtime.Poll(2'000'000, caller) == Status::kInvalidState);
}

void RuntimeRejectsTasksOnUnmanagedExecutors() {
  CooperativeExecutor<1> managed("managed", 5);
  CooperativeExecutor<1> unmanaged("unmanaged", 5);
  StaticPeriodicScheduler<1> scheduler("periodic");
  PeriodicModule module;
  ModuleContext context(
      "node", "module",
      ModuleServices{.executor = &managed, .periodic_tasks = &scheduler});
  std::array executors{aster::runtime::ExecutorSlot{&managed}};
  std::array modules{aster::runtime::ModuleSlot{&module, &context}};
  aster::runtime::Runtime runtime(executors, modules, scheduler);

  assert(scheduler.AddTask("module", "control", 1'000'000, unmanaged) ==
         Status::kOk);
  assert(runtime.Initialize() == Status::kInvalidArgument);
  assert(runtime.failure().has_value());
  assert(runtime.failure()->subject ==
         aster::runtime::LifecycleSubject::kScheduler);
  assert(runtime.failure()->operation ==
         aster::runtime::LifecycleOperation::kValidation);
}

}  // namespace

int main() {
  RequiresEveryTaskToBeBound();
  SkipsMissedPeriodsAndPreventsReentry();
  ReportsExecutorBackpressureAndRejectsInterruptPolling();
  RuntimeOwnsPeriodicSchedulingLifecycle();
  RuntimeRejectsTasksOnUnmanagedExecutors();
  return 0;
}
