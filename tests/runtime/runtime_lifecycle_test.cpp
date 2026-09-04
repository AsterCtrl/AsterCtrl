#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include "aster/runtime.hpp"

namespace {

struct EventLog {
  std::array<char, 32> values{};
  std::size_t size{};

  void Add(char value) noexcept { values[size++] = value; }
};

void RecordQuiesce(void* state) noexcept { static_cast<EventLog*>(state)->Add('Q'); }

class RecordingModule final : public aster::Module {
 public:
  RecordingModule(std::string_view name, char id, EventLog& log,
                  aster::Status initialize = aster::Status::kOk,
                  aster::Status start = aster::Status::kOk) noexcept
      : name_(name), id_(id), log_(log), initialize_(initialize), start_(start) {}

  aster::ModuleInfo Info() const noexcept override {
    return {name_, "test.Module", "test-package", {1, 0, 0}};
  }
  aster::Status Initialize(aster::CoreRef) noexcept override {
    log_.Add(static_cast<char>(id_ + 'I' - 'A'));
    return initialize_;
  }
  aster::Status Start() noexcept override {
    log_.Add(static_cast<char>(id_ + 'T' - 'A'));
    return start_;
  }
  void Shutdown() noexcept override { log_.Add(static_cast<char>(id_ + 'S' - 'A')); }

 private:
  std::string_view name_;
  char id_;
  EventLog& log_;
  aster::Status initialize_;
  aster::Status start_;
};

class RecordingRegistry final : public aster::Registry {
 public:
  RecordingRegistry(EventLog& log, aster::Status result = aster::Status::kOk) noexcept
      : log_(log), result_(result) {}

  aster::Status Seal() noexcept override {
    log_.Add('R');
    sealed_ = aster::IsOk(result_);
    return result_;
  }
  bool sealed() const noexcept override { return sealed_; }

 private:
  EventLog& log_;
  aster::Status result_;
  bool sealed_{};
};

}  // namespace

int main() {
  {
    EventLog log;
    RecordingModule first("first", 'A', log);
    RecordingModule second("second", 'B', log);
    RecordingRegistry registry(log);
    std::array<aster::ModuleSlot, 2> modules{{{&first, {}, "first"}, {&second, {}, "second"}}};
    std::array<aster::RegistrySlot, 1> registries{{{&registry}}};
    aster::Runtime runtime(modules, registries, aster::RuntimeHooks{RecordQuiesce, &log});
    assert(runtime.Initialize() == aster::Status::kOk);
    assert(runtime.Start() == aster::Status::kOk);
    runtime.Shutdown();
    const std::array<char, 8> expected{'I', 'J', 'R', 'T', 'U', 'Q', 'T', 'S'};
    assert(log.size == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      assert(log.values[index] == expected[index]);
    }
    assert(runtime.state() == aster::RuntimeState::kStopped);
  }

  {
    EventLog log;
    RecordingModule first("first", 'A', log);
    RecordingModule second("second", 'B', log, aster::Status::kOk, aster::Status::kUnavailable);
    std::array<aster::ModuleSlot, 2> modules{{{&first, {}, "first"}, {&second, {}, "second"}}};
    aster::Runtime runtime(modules, {}, aster::RuntimeHooks{RecordQuiesce, &log});
    assert(runtime.Initialize() == aster::Status::kOk);
    assert(runtime.Start() == aster::Status::kUnavailable);
    const std::array<char, 7> expected{'I', 'J', 'T', 'U', 'Q', 'T', 'S'};
    assert(log.size == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      assert(log.values[index] == expected[index]);
    }
    assert(runtime.failure()->operation == aster::LifecycleOperation::kStart);
  }

  {
    EventLog log;
    RecordingModule first("first", 'A', log);
    RecordingModule second("second", 'B', log, aster::Status::kUnavailable);
    std::array<aster::ModuleSlot, 2> modules{{{&first, {}, "first"}, {&second, {}, "second"}}};
    aster::Runtime runtime(modules);
    assert(runtime.Initialize() == aster::Status::kUnavailable);
    assert(log.values[2] == 'T');
    assert(log.values[3] == 'S');
    assert(runtime.failure()->operation == aster::LifecycleOperation::kInitialize);
  }

  {
    EventLog log;
    RecordingModule first("first", 'A', log);
    RecordingRegistry registry(log, aster::Status::kTypeMismatch);
    std::array<aster::ModuleSlot, 1> modules{{{&first, {}, "first"}}};
    std::array<aster::RegistrySlot, 1> registries{{{&registry}}};
    aster::Runtime runtime(modules, registries);
    assert(runtime.Initialize() == aster::Status::kTypeMismatch);
    assert(log.values[0] == 'I');
    assert(log.values[1] == 'R');
    assert(log.values[2] == 'S');
  }

  {
    EventLog log;
    RecordingModule first("duplicate", 'A', log);
    RecordingModule second("duplicate", 'B', log);
    std::array<aster::ModuleSlot, 2> modules{{{&first, {}, {}}, {&second, {}, {}}}};
    aster::Runtime runtime(modules);
    assert(runtime.Initialize() == aster::Status::kAlreadyExists);
    assert(runtime.failure()->operation == aster::LifecycleOperation::kValidation);
  }

  {
    EventLog log;
    RecordingModule first("shared-type", 'A', log);
    RecordingModule second("shared-type", 'B', log);
    std::array<aster::ModuleSlot, 2> modules{{
        {&first, {}, "first-instance"},
        {&second, {}, "second-instance"},
    }};
    aster::Runtime runtime(modules);
    assert(runtime.Initialize() == aster::Status::kOk);
    assert(runtime.Start() == aster::Status::kOk);
    runtime.Shutdown();
  }
}
