#include <array>
#include <cassert>
#include <span>
#include <string_view>

#include "xrobot/runtime/executor.hpp"
#include "xrobot/runtime/module.hpp"
#include "xrobot/runtime/runtime.hpp"

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

class RecordingExecutor final : public xrobot::runtime::Executor {
 public:
  RecordingExecutor(std::string_view name, EventLog& log,
                    xrobot::runtime::Status start_status =
                        xrobot::runtime::Status::kOk)
      : name_(name),
        log_(log),
        context_(name, xrobot::runtime::ExecutionKind::kThread, 4),
        start_status_(start_status) {}

  std::string_view Name() const noexcept override { return name_; }
  const xrobot::runtime::ExecutionContext& context() const noexcept override {
    return context_;
  }
  xrobot::runtime::ExecutorState state() const noexcept override {
    return state_;
  }
  xrobot::runtime::ExecutorStats stats() const noexcept override { return {}; }

  xrobot::runtime::Status Initialize() noexcept override {
    log_.Push({name_, EventType::kExecutorInitialize});
    state_ = xrobot::runtime::ExecutorState::kInitialized;
    return xrobot::runtime::Status::kOk;
  }

  xrobot::runtime::Status Start() noexcept override {
    log_.Push({name_, EventType::kExecutorStart});
    state_ = xrobot::runtime::IsOk(start_status_)
                 ? xrobot::runtime::ExecutorState::kRunning
                 : xrobot::runtime::ExecutorState::kFailed;
    return start_status_;
  }

  xrobot::runtime::Status TryPost(
      xrobot::runtime::WorkItem,
      const xrobot::runtime::ExecutionContext&) noexcept override {
    return xrobot::runtime::Status::kUnavailable;
  }

  void Shutdown() noexcept override {
    log_.Push({name_, EventType::kExecutorShutdown});
    state_ = xrobot::runtime::ExecutorState::kStopped;
  }

 private:
  std::string_view name_;
  EventLog& log_;
  xrobot::runtime::ExecutionContext context_;
  xrobot::runtime::Status start_status_;
  xrobot::runtime::ExecutorState state_{
      xrobot::runtime::ExecutorState::kConstructed};
};

class RecordingModule final : public xrobot::runtime::Module {
 public:
  RecordingModule(std::string_view name, EventLog& log,
                  xrobot::runtime::Status initialize_status =
                      xrobot::runtime::Status::kOk,
                  xrobot::runtime::Status start_status =
                      xrobot::runtime::Status::kOk)
      : name_(name),
        log_(log),
        initialize_status_(initialize_status),
        start_status_(start_status) {}

  std::string_view Name() const noexcept override { return name_; }

  xrobot::runtime::Status Initialize(
      xrobot::runtime::ModuleContext& context) noexcept override {
    assert(context.module_name() == name_);
    log_.Push({name_, EventType::kInitialize});
    return initialize_status_;
  }

  xrobot::runtime::Status Start() noexcept override {
    log_.Push({name_, EventType::kStart});
    return start_status_;
  }

  void Shutdown() noexcept override { log_.Push({name_, EventType::kShutdown}); }

 private:
  std::string_view name_;
  EventLog& log_;
  xrobot::runtime::Status initialize_status_;
  xrobot::runtime::Status start_status_;
};

void AssertEvent(const Event& event, std::string_view module, EventType type) {
  assert(event.module == module);
  assert(event.type == type);
}

void SuccessfulLifecycleRunsInDeterministicOrder() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log);
  xrobot::runtime::ModuleContext first_context("node", "first");
  xrobot::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      xrobot::runtime::ModuleSlot{&first, &first_context},
      xrobot::runtime::ModuleSlot{&second, &second_context},
  };
  xrobot::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == xrobot::runtime::Status::kOk);
  assert(runtime.Start() == xrobot::runtime::Status::kOk);
  runtime.Shutdown();

  const auto events = log.Events();
  assert(events.size() == 6);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "first", EventType::kStart);
  AssertEvent(events[3], "second", EventType::kStart);
  AssertEvent(events[4], "second", EventType::kShutdown);
  AssertEvent(events[5], "first", EventType::kShutdown);
  assert(runtime.state() == xrobot::runtime::RuntimeState::kStopped);
}

void DuplicateModuleNamesFailBeforeInitialization() {
  EventLog log;
  RecordingModule first("duplicate", log);
  RecordingModule second("duplicate", log);
  xrobot::runtime::ModuleContext first_context("node", "duplicate");
  xrobot::runtime::ModuleContext second_context("node", "duplicate");
  std::array slots{
      xrobot::runtime::ModuleSlot{&first, &first_context},
      xrobot::runtime::ModuleSlot{&second, &second_context},
  };
  xrobot::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == xrobot::runtime::Status::kInvalidArgument);
  assert(log.Events().empty());
  assert(runtime.state() == xrobot::runtime::RuntimeState::kFailed);
  assert(runtime.failure().has_value());
  assert(runtime.failure()->subject ==
         xrobot::runtime::LifecycleSubject::kModule);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->name == "duplicate");
  assert(runtime.failure()->operation ==
         xrobot::runtime::LifecycleOperation::kValidation);
}

void InitializeFailureRollsBackInReverseOrder() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log, xrobot::runtime::Status::kInternal);
  xrobot::runtime::ModuleContext first_context("node", "first");
  xrobot::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      xrobot::runtime::ModuleSlot{&first, &first_context},
      xrobot::runtime::ModuleSlot{&second, &second_context},
  };
  xrobot::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == xrobot::runtime::Status::kInternal);

  const auto events = log.Events();
  assert(events.size() == 4);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "second", EventType::kShutdown);
  AssertEvent(events[3], "first", EventType::kShutdown);
  assert(first_context.phase() == xrobot::runtime::ModulePhase::kStopped);
  assert(second_context.phase() == xrobot::runtime::ModulePhase::kStopped);
  assert(runtime.state() == xrobot::runtime::RuntimeState::kFailed);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->operation ==
         xrobot::runtime::LifecycleOperation::kInitialize);
  assert(runtime.failure()->status == xrobot::runtime::Status::kInternal);
}

void StartFailureShutsDownEveryInitializedModule() {
  EventLog log;
  RecordingModule first("first", log);
  RecordingModule second("second", log, xrobot::runtime::Status::kOk,
                         xrobot::runtime::Status::kUnavailable);
  xrobot::runtime::ModuleContext first_context("node", "first");
  xrobot::runtime::ModuleContext second_context("node", "second");
  std::array slots{
      xrobot::runtime::ModuleSlot{&first, &first_context},
      xrobot::runtime::ModuleSlot{&second, &second_context},
  };
  xrobot::runtime::Runtime runtime(slots);

  assert(runtime.Initialize() == xrobot::runtime::Status::kOk);
  assert(runtime.Start() == xrobot::runtime::Status::kUnavailable);

  const auto events = log.Events();
  assert(events.size() == 6);
  AssertEvent(events[0], "first", EventType::kInitialize);
  AssertEvent(events[1], "second", EventType::kInitialize);
  AssertEvent(events[2], "first", EventType::kStart);
  AssertEvent(events[3], "second", EventType::kStart);
  AssertEvent(events[4], "second", EventType::kShutdown);
  AssertEvent(events[5], "first", EventType::kShutdown);
  assert(runtime.state() == xrobot::runtime::RuntimeState::kFailed);
  assert(runtime.failure()->index == 1);
  assert(runtime.failure()->operation ==
         xrobot::runtime::LifecycleOperation::kStart);
  assert(runtime.failure()->status == xrobot::runtime::Status::kUnavailable);

  runtime.Shutdown();
  assert(log.Events().size() == 6);
}

void RuntimeOwnsExecutorLifecycle() {
  EventLog log;
  RecordingExecutor executor("control", log);
  RecordingModule module("module", log);
  xrobot::runtime::ModuleContext context("node", "module", executor);
  std::array executors{xrobot::runtime::ExecutorSlot{&executor}};
  std::array modules{xrobot::runtime::ModuleSlot{&module, &context}};
  xrobot::runtime::Runtime runtime(executors, modules);

  assert(runtime.Initialize() == xrobot::runtime::Status::kOk);
  assert(runtime.Start() == xrobot::runtime::Status::kOk);
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
                             xrobot::runtime::Status::kInternal);
  RecordingModule module("module", log);
  xrobot::runtime::ModuleContext context("node", "module", executor);
  std::array executors{xrobot::runtime::ExecutorSlot{&executor}};
  std::array modules{xrobot::runtime::ModuleSlot{&module, &context}};
  xrobot::runtime::Runtime runtime(executors, modules);

  assert(runtime.Initialize() == xrobot::runtime::Status::kOk);
  assert(runtime.Start() == xrobot::runtime::Status::kInternal);

  const auto events = log.Events();
  assert(events.size() == 5);
  AssertEvent(events[0], "control", EventType::kExecutorInitialize);
  AssertEvent(events[1], "module", EventType::kInitialize);
  AssertEvent(events[2], "control", EventType::kExecutorStart);
  AssertEvent(events[3], "module", EventType::kShutdown);
  AssertEvent(events[4], "control", EventType::kExecutorShutdown);
  assert(runtime.failure()->subject ==
         xrobot::runtime::LifecycleSubject::kExecutor);
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
