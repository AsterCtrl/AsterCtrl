#include <array>
#include <cassert>
#include <span>
#include <string_view>

#include "aster/runtime/executor.hpp"
#include "aster/runtime/module.hpp"
#include "aster/runtime/runtime.hpp"

namespace {

enum class EventType {
  kExecutorInitialize,
  kExecutorStart,
  kExecutorShutdown,
  kInitialize,
  kStart,
  kShutdown,
};

struct Event {
  std::string_view module;
  EventType type;
};

class EventLog {
 public:
  void Push(Event event) {
    assert(size_ < events_.size());
    events_[size_++] = event;
  }

  std::span<const Event> Events() const { return {events_.data(), size_}; }

 private:
  std::array<Event, 32> events_{};
  std::size_t size_{};
};

class RecordingExecutor final : public aster::runtime::Executor {
 public:
  RecordingExecutor(std::string_view name, EventLog& log,
                    aster::runtime::Status start_status =
                        aster::runtime::Status::kOk)
      : name_(name),
        log_(log),
        context_(name, aster::runtime::ExecutionKind::kThread, 4),
        start_status_(start_status) {}

  std::string_view Name() const noexcept override { return name_; }
  const aster::runtime::ExecutionContext& context() const noexcept override {
    return context_;
  }
  aster::runtime::ExecutorState state() const noexcept override {
    return state_;
  }
  aster::runtime::ExecutorStats stats() const noexcept override { return {}; }

  aster::runtime::Status Initialize() noexcept override {
    log_.Push({name_, EventType::kExecutorInitialize});
    state_ = aster::runtime::ExecutorState::kInitialized;
    return aster::runtime::Status::kOk;
  }

  aster::runtime::Status Start() noexcept override {
    log_.Push({name_, EventType::kExecutorStart});
    state_ = aster::runtime::IsOk(start_status_)
                 ? aster::runtime::ExecutorState::kRunning
                 : aster::runtime::ExecutorState::kFailed;
    return start_status_;
  }

  aster::runtime::Status TryPost(
      aster::runtime::WorkItem,
      const aster::runtime::ExecutionContext&) noexcept override {
    return aster::runtime::Status::kUnavailable;
  }

  void Shutdown() noexcept override {
    log_.Push({name_, EventType::kExecutorShutdown});
    state_ = aster::runtime::ExecutorState::kStopped;
  }

 private:
  std::string_view name_;
  EventLog& log_;
  aster::runtime::ExecutionContext context_;
  aster::runtime::Status start_status_;
  aster::runtime::ExecutorState state_{
      aster::runtime::ExecutorState::kConstructed};
};

class RecordingModule final : public aster::runtime::Module {
 public:
  RecordingModule(std::string_view name, EventLog& log,
                  aster::runtime::Status initialize_status =
                      aster::runtime::Status::kOk,
                  aster::runtime::Status start_status =
                      aster::runtime::Status::kOk)
      : name_(name),
        log_(log),
        initialize_status_(initialize_status),
        start_status_(start_status) {}

  std::string_view Name() const noexcept override { return name_; }

  aster::runtime::Status Initialize(
      aster::runtime::ModuleContext& context) noexcept override {
    assert(context.module_name() == name_);
    log_.Push({name_, EventType::kInitialize});
    return initialize_status_;
  }

  aster::runtime::Status Start() noexcept override {
    log_.Push({name_, EventType::kStart});
    return start_status_;
  }

  void Shutdown() noexcept override { log_.Push({name_, EventType::kShutdown}); }

 private:
  std::string_view name_;
  EventLog& log_;
  aster::runtime::Status initialize_status_;
  aster::runtime::Status start_status_;
};

void AssertEvent(const Event& event, std::string_view module, EventType type) {
  assert(event.module == module);
  assert(event.type == type);
}

void SuccessfulLifecycleRunsInDeterministicOrder() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log);
  aster::runtime::ModuleContext first_context("node", "first");
  aster::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      aster::runtime::ModuleSlot{&first, &first_context},
      aster::runtime::ModuleSlot{&second, &second_context},
  };
  aster::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == aster::runtime::Status::kOk);
  assert(runtime.Start() == aster::runtime::Status::kOk);
  runtime.Shutdown();

  const auto events = log.Events();
  assert(events.size() == 6);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "first", EventType::kStart);
  AssertEvent(events[3], "second", EventType::kStart);
  AssertEvent(events[4], "second", EventType::kShutdown);
  AssertEvent(events[5], "first", EventType::kShutdown);
  assert(runtime.state() == aster::runtime::RuntimeState::kStopped);
}

void DuplicateModuleNamesFailBeforeInitialization() {
  EventLog log;
  RecordingModule first("duplicate", log);
  RecordingModule second("duplicate", log);
  aster::runtime::ModuleContext first_context("node", "duplicate");
  aster::runtime::ModuleContext second_context("node", "duplicate");
  std::array slots{
      aster::runtime::ModuleSlot{&first, &first_context},
      aster::runtime::ModuleSlot{&second, &second_context},
  };
  aster::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == aster::runtime::Status::kInvalidArgument);
  assert(log.Events().empty());
  assert(runtime.state() == aster::runtime::RuntimeState::kFailed);
  assert(runtime.failure().has_value());
  assert(runtime.failure()->subject ==
         aster::runtime::LifecycleSubject::kModule);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->name == "duplicate");
  assert(runtime.failure()->operation ==
         aster::runtime::LifecycleOperation::kValidation);
}

void InitializeFailureRollsBackInReverseOrder() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log, aster::runtime::Status::kInternal);
  aster::runtime::ModuleContext first_context("node", "first");
  aster::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      aster::runtime::ModuleSlot{&first, &first_context},
      aster::runtime::ModuleSlot{&second, &second_context},
  };
  aster::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == aster::runtime::Status::kInternal);

  const auto events = log.Events();
  assert(events.size() == 4);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "second", EventType::kShutdown);
  AssertEvent(events[3], "first", EventType::kShutdown);
  assert(first_context.phase() == aster::runtime::ModulePhase::kStopped);
  assert(second_context.phase() == aster::runtime::ModulePhase::kStopped);
  assert(runtime.state() == aster::runtime::RuntimeState::kFailed);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->operation ==
         aster::runtime::LifecycleOperation::kInitialize);
  assert(runtime.failure()->status == aster::runtime::Status::kInternal);
}

void StartFailureShutsDownEveryInitializedModule() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log, aster::runtime::Status::kOk,
                         aster::runtime::Status::kUnavailable);
  aster::runtime::ModuleContext first_context("node", "first");
  aster::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      aster::runtime::ModuleSlot{&first, &first_context},
      aster::runtime::ModuleSlot{&second, &second_context},
  };
  aster::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == aster::runtime::Status::kOk);
  assert(runtime.Start() == aster::runtime::Status::kUnavailable);

  const auto events = log.Events();
  assert(events.size() == 6);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "first", EventType::kStart);
  AssertEvent(events[3], "second", EventType::kStart);
  AssertEvent(events[4], "second", EventType::kShutdown);
  AssertEvent(events[5], "first", EventType::kShutdown);
  assert(runtime.state() == aster::runtime::RuntimeState::kFailed);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->operation ==
         aster::runtime::LifecycleOperation::kStart);
  assert(runtime.failure()->status == aster::runtime::Status::kUnavailable);

  runtime.Shutdown();
  assert(log.Events().size() == 6);
}

void RuntimeOwnsExecutorLifecycle() {
  EventLog log;
  RecordingExecutor executor("control", log);
  RecordingModule module("module", log);
  aster::runtime::ModuleContext context("node", "module", executor);
  std::array executors{aster::runtime::ExecutorSlot{&executor}};
  std::array modules{aster::runtime::ModuleSlot{&module, &context}};
  aster::runtime::Runtime runtime(executors, modules);

  assert(runtime.Initialize() == aster::runtime::Status::kOk);
  assert(runtime.Start() == aster::runtime::Status::kOk);
  runtime.Shutdown();

  const auto events = log.Events();
  assert(events.size() == 6);
  AssertEvent(events[0], "control", EventType::kExecutorInitialize);
  AssertEvent(events[1], "module", EventType::kInitialize);
  AssertEvent(events[2], "control", EventType::kExecutorStart);
  AssertEvent(events[3], "module", EventType::kStart);
  AssertEvent(events[4], "module", EventType::kShutdown);
  AssertEvent(events[5], "control", EventType::kExecutorShutdown);
  assert(context.executor() == &executor);
}

void ExecutorStartFailureRollsBackModulesAndExecutors() {
  EventLog log;
  RecordingExecutor executor("control", log,
                             aster::runtime::Status::kInternal);
  RecordingModule module("module", log);
  aster::runtime::ModuleContext context("node", "module", executor);
  std::array executors{aster::runtime::ExecutorSlot{&executor}};
  std::array modules{aster::runtime::ModuleSlot{&module, &context}};
  aster::runtime::Runtime runtime(executors, modules);

  assert(runtime.Initialize() == aster::runtime::Status::kOk);
  assert(runtime.Start() == aster::runtime::Status::kInternal);

  const auto events = log.Events();
  assert(events.size() == 5);
  AssertEvent(events[0], "control", EventType::kExecutorInitialize);
  AssertEvent(events[1], "module", EventType::kInitialize);
  AssertEvent(events[2], "control", EventType::kExecutorStart);
  AssertEvent(events[3], "module", EventType::kShutdown);
  AssertEvent(events[4], "control", EventType::kExecutorShutdown);
  assert(runtime.failure()->subject ==
         aster::runtime::LifecycleSubject::kExecutor);
  assert(runtime.failure()->name == "control");
}

}  // namespace

int main() {
  SuccessfulLifecycleRunsInDeterministicOrder();
  DuplicateModuleNamesFailBeforeInitialization();
  InitializeFailureRollsBackInReverseOrder();
  StartFailureShutsDownEveryInitializedModule();
  RuntimeOwnsExecutorLifecycle();
  ExecutorStartFailureRollsBackModulesAndExecutors();
  return 0;
}
