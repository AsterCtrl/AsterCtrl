#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "aster/channel.hpp"
#include "aster/core_ref.hpp"
#include "aster/rpc.hpp"
#include "aster/runtime.hpp"
#include "aster/static_hardware.hpp"

namespace aster::platform::zephyr {

class UptimeClock final : public Clock {
 public:
  [[nodiscard]] ClockDomain domain() const noexcept override { return ClockDomain::kMonotonic; }

  [[nodiscard]] std::uint64_t NowNs() const noexcept override {
    return k_ticks_to_ns_floor64(k_uptime_ticks());
  }
};

class PrintkLogger final : public Logger {
 public:
  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) noexcept override {
    if (message.size() > static_cast<std::size_t>(INT_MAX) ||
        caller.executor_name().size() > static_cast<std::size_t>(INT_MAX)) {
      return Status::kCapacityExceeded;
    }
    printk("%s [%.*s] %.*s\n", Prefix(level), static_cast<int>(caller.executor_name().size()),
           caller.executor_name().data(), static_cast<int>(message.size()), message.data());
    return Status::kOk;
  }

 private:
  [[nodiscard]] static constexpr const char* Prefix(LogLevel level) noexcept {
    switch (level) {
      case LogLevel::kTrace:
        return "TRACE";
      case LogLevel::kDebug:
        return "DEBUG";
      case LogLevel::kInfo:
        return "INFO";
      case LogLevel::kWarning:
        return "WARN";
      case LogLevel::kError:
        return "ERROR";
      case LogLevel::kCritical:
        return "CRITICAL";
    }
    return "UNKNOWN";
  }
};

template <std::size_t Capacity>
class FixedArenaAllocator final : public Allocator {
 public:
  static_assert(Capacity > 0);

  void* Allocate(std::size_t size, std::size_t alignment) noexcept override {
    if (size == 0 || alignment == 0 || alignment > alignof(std::max_align_t) ||
        (alignment & (alignment - 1U)) != 0) {
      return nullptr;
    }
    const auto aligned = (offset_ + alignment - 1U) & ~(alignment - 1U);
    if (aligned > storage_.size() || size > storage_.size() - aligned) {
      return nullptr;
    }
    auto* result = storage_.data() + aligned;
    offset_ = aligned + size;
    return result;
  }

  void Deallocate(void*, std::size_t, std::size_t) noexcept override {}

  void Reset() noexcept { offset_ = 0; }
  [[nodiscard]] std::size_t used() const noexcept { return offset_; }

 private:
  alignas(std::max_align_t) std::array<std::byte, Capacity> storage_{};
  std::size_t offset_{};
};

template <std::size_t MaxCapabilities>
using StaticHardwareManager = ::aster::StaticHardwareManager<MaxCapabilities>;

template <std::size_t Capacity>
class ThreadExecutor final : public Executor {
 public:
  static_assert(Capacity > 0);

  ThreadExecutor(std::string_view name, Clock& clock, k_thread_stack_t* stack,
                 std::size_t stack_size, int priority) noexcept
      : name_(name), clock_(clock), stack_(stack), stack_size_(stack_size), priority_(priority) {
    k_msgq_init(&queue_, queue_storage_.data(), sizeof(Entry), Capacity);
    k_sem_init(&run_gate_, 0, 1);
    k_sem_init(&delay_gate_, 0, 1);
  }

  Status Prepare() noexcept {
    if (name_.empty() || stack_ == nullptr || stack_size_ == 0) {
      return Status::kInvalidState;
    }

    auto key = k_spin_lock(&state_lock_);
    if (state_ != State::kConstructed) {
      k_spin_unlock(&state_lock_, key);
      return Status::kInvalidState;
    }
    state_ = State::kPrepared;
    k_spin_unlock(&state_lock_, key);

    thread_id_ = k_thread_create(&thread_, stack_, stack_size_, EntryPoint, this, nullptr, nullptr,
                                 priority_, 0, K_NO_WAIT);
    if (thread_id_ == nullptr) {
      key = k_spin_lock(&state_lock_);
      state_ = State::kFailed;
      k_spin_unlock(&state_lock_, key);
      return Status::kUnavailable;
    }
    return Status::kOk;
  }

  Status Activate() noexcept {
    const auto key = k_spin_lock(&state_lock_);
    if (state_ != State::kPrepared) {
      k_spin_unlock(&state_lock_, key);
      return Status::kInvalidState;
    }
    state_ = State::kRunning;
    k_spin_unlock(&state_lock_, key);
    k_sem_give(&run_gate_);
    return Status::kOk;
  }

  void Shutdown() noexcept {
    if (k_is_in_isr() || (thread_id_ != nullptr && k_current_get() == thread_id_)) {
      k_panic();
      return;
    }
    auto key = k_spin_lock(&state_lock_);
    if (state_ == State::kStopped || state_ == State::kFailed || state_ == State::kQuiescing) {
      k_spin_unlock(&state_lock_, key);
      return;
    }
    if (state_ == State::kConstructed) {
      state_ = State::kStopped;
      k_spin_unlock(&state_lock_, key);
      return;
    }
    state_ = State::kQuiescing;
    k_spin_unlock(&state_lock_, key);

    k_msgq_purge(&queue_);
    const Entry stop{};
    static_cast<void>(k_msgq_put(&queue_, &stop, K_NO_WAIT));
    k_sem_give(&run_gate_);
    k_sem_give(&delay_gate_);
    if (thread_id_ != nullptr) {
      if (k_thread_join(thread_id_, K_FOREVER) != 0) {
        k_panic();
        return;
      }
      thread_id_ = nullptr;
    }
    k_msgq_purge(&queue_);

    key = k_spin_lock(&state_lock_);
    state_ = State::kStopped;
    k_spin_unlock(&state_lock_, key);
  }

  [[nodiscard]] std::string_view Name() const noexcept override { return name_; }

  Status TryPost(WorkItem work, const ExecutionContext& caller) noexcept override {
    return TryPostAt(clock_.NowNs(), work, caller);
  }

  Status TryPostAt(std::uint64_t timestamp_ns, WorkItem work,
                   const ExecutionContext&) noexcept override {
    if (!work) {
      return Status::kInvalidArgument;
    }

    const auto key = k_spin_lock(&state_lock_);
    if (state_ != State::kPrepared && state_ != State::kRunning) {
      k_spin_unlock(&state_lock_, key);
      return Status::kInvalidState;
    }
    const Entry entry{timestamp_ns, work};
    const auto status =
        k_msgq_put(&queue_, &entry, K_NO_WAIT) == 0 ? Status::kOk : Status::kCapacityExceeded;
    k_spin_unlock(&state_lock_, key);
    return status;
  }

 private:
  enum class State : std::uint8_t {
    kConstructed,
    kPrepared,
    kRunning,
    kQuiescing,
    kStopped,
    kFailed,
  };

  struct Entry {
    std::uint64_t timestamp_ns{};
    WorkItem work{};
  };

  static void EntryPoint(void* self, void*, void*) noexcept {
    static_cast<ThreadExecutor*>(self)->Run();
  }

  [[nodiscard]] bool IsRunning() noexcept {
    const auto key = k_spin_lock(&state_lock_);
    const auto running = state_ == State::kRunning;
    k_spin_unlock(&state_lock_, key);
    return running;
  }

  void Run() noexcept {
    static_cast<void>(k_sem_take(&run_gate_, K_FOREVER));
    while (true) {
      Entry entry;
      if (k_msgq_get(&queue_, &entry, K_FOREVER) != 0) {
        continue;
      }
      if (!entry.work || !IsRunning()) {
        return;
      }
      const auto now = clock_.NowNs();
      if (entry.timestamp_ns > now) {
        const auto delay_ns = entry.timestamp_ns - now;
        if (k_sem_take(&delay_gate_, K_NSEC(delay_ns)) == 0 || !IsRunning()) {
          return;
        }
      }
      if (!IsRunning()) {
        return;
      }
      entry.work.Run(ExecutionContext(name_, ExecutionKind::kThread, clock_.NowNs()));
    }
  }

  std::string_view name_;
  Clock& clock_;
  k_thread_stack_t* stack_{};
  std::size_t stack_size_{};
  int priority_{};
  struct k_thread thread_ {};
  k_tid_t thread_id_{};
  struct k_spinlock state_lock_ {};
  struct k_sem run_gate_ {};
  struct k_sem delay_gate_ {};
  struct k_msgq queue_ {};
  alignas(4) std::array<char, Capacity * sizeof(Entry)> queue_storage_{};
  State state_{State::kConstructed};
};

template <typename Composition, std::size_t MaxModules, std::size_t MaxTopics,
          std::size_t MaxSubscribersPerTopic, std::size_t MaximumMessageSize,
          std::size_t MaxRpcServices, std::size_t MaxPendingRpc, std::size_t ArenaBytes,
          std::size_t MaxHardwareCapabilities, std::size_t ExecutorQueueDepth>
class StaticNodeRuntime final {
 public:
  static_assert(MaxModules > 0);
  static_assert(MaxHardwareCapabilities > 0);

  StaticNodeRuntime(k_thread_stack_t* executor_stack, std::size_t executor_stack_size,
                    std::string_view executor_name, int executor_priority) noexcept
      : executor_(executor_name, clock_, executor_stack, executor_stack_size, executor_priority),
        rpc_(ExecutorRef(executor_)),
        core_(CoreHandles{
            .configurator = {},
            .logger = LoggerRef(logger_),
            .executor = ExecutorRef(executor_),
            .channel = ChannelRef(channel_),
            .rpc = RpcRef(rpc_),
            .parameter = {},
            .clock = ClockRef(clock_),
            .allocator = AllocatorRef(allocator_),
            .hardware = HardwareManagerRef(hardware_),
        }),
        composition_(core_) {
    for (const auto& slot : composition_.Modules()) {
      Record(AddModuleSlot(slot));
    }
    Record(AddRegistrySlot(channel_));
    Record(AddRegistrySlot(rpc_));
    Record(AddRegistrySlot(hardware_));
    for (const auto& slot : composition_.Registries()) {
      if (slot.registry == nullptr) {
        Record(Status::kInvalidArgument);
      } else {
        Record(AddRegistrySlot(*slot.registry));
      }
    }
  }

  ~StaticNodeRuntime() { Shutdown(); }

  StaticNodeRuntime(const StaticNodeRuntime&) = delete;
  StaticNodeRuntime& operator=(const StaticNodeRuntime&) = delete;
  StaticNodeRuntime(StaticNodeRuntime&&) = delete;
  StaticNodeRuntime& operator=(StaticNodeRuntime&&) = delete;

  Status AddInfrastructureModule(Module& module, std::string_view instance_name) noexcept {
    if (runtime_.has_value() || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    if (module_count_ == module_slots_.size()) {
      Record(Status::kCapacityExceeded);
      return Status::kCapacityExceeded;
    }
    for (std::size_t index = module_count_; index > infrastructure_module_count_; --index) {
      module_slots_[index] = module_slots_[index - 1U];
    }
    module_slots_[infrastructure_module_count_] = {&module, core_, instance_name};
    ++infrastructure_module_count_;
    ++module_count_;
    return Status::kOk;
  }

  Status AddRegistry(Registry& registry) noexcept {
    if (runtime_.has_value() || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    return AddRegistrySlot(registry);
  }

  Status RegisterHardware(std::string_view name, std::string_view type, void* device) noexcept {
    if (runtime_.has_value() || !IsOk(setup_status_)) {
      return Status::kInvalidState;
    }
    const auto status = hardware_.Register(name, type, device);
    Record(status);
    return status;
  }

  Status Initialize() noexcept {
    if (!IsOk(setup_status_)) {
      return setup_status_;
    }
    if (runtime_.has_value() || module_count_ == 0) {
      return runtime_.has_value() ? Status::kInvalidState : Status::kUnavailable;
    }
    auto status = executor_.Prepare();
    if (!IsOk(status)) {
      setup_status_ = status;
      return status;
    }
    runtime_.emplace(std::span<ModuleSlot>(module_slots_).first(module_count_),
                     std::span<RegistrySlot>(registry_slots_).first(registry_count_),
                     RuntimeHooks{QuiesceExecutor, &executor_});
    status = runtime_->Initialize();
    if (!IsOk(status)) {
      executor_.Shutdown();
    }
    return status;
  }

  Status Start() noexcept {
    if (!runtime_.has_value()) {
      return Status::kInvalidState;
    }
    auto status = runtime_->Start();
    if (IsOk(status)) {
      status = executor_.Activate();
      if (!IsOk(status)) {
        runtime_->Shutdown();
      }
    }
    return status;
  }

  void Shutdown() noexcept {
    if (runtime_.has_value()) {
      runtime_->Shutdown();
    }
    // Runtime invokes the hook before stopping Modules. This second call also
    // covers a StaticNodeRuntime that never reached Runtime construction.
    executor_.Shutdown();
    allocator_.Reset();
  }

  [[nodiscard]] RuntimeState state() const noexcept {
    return runtime_.has_value()  ? runtime_->state()
           : IsOk(setup_status_) ? RuntimeState::kConstructed
                                 : RuntimeState::kFailed;
  }
  [[nodiscard]] Status setup_status() const noexcept { return setup_status_; }
  [[nodiscard]] CoreRef core() const noexcept { return core_; }
  [[nodiscard]] Composition& composition() noexcept { return composition_; }

 private:
  static constexpr std::size_t kMaximumRegistries = MaxModules + 3U;

  static void QuiesceExecutor(void* executor) noexcept {
    static_cast<ThreadExecutor<ExecutorQueueDepth>*>(executor)->Shutdown();
  }

  Status AddModuleSlot(const ModuleSlot& slot) noexcept {
    if (slot.module == nullptr) {
      return Status::kInvalidArgument;
    }
    if (module_count_ == module_slots_.size()) {
      return Status::kCapacityExceeded;
    }
    module_slots_[module_count_++] = slot;
    return Status::kOk;
  }

  Status AddRegistrySlot(Registry& registry) noexcept {
    for (std::size_t index = 0; index < registry_count_; ++index) {
      if (registry_slots_[index].registry == &registry) {
        return Status::kAlreadyExists;
      }
    }
    if (registry_count_ == registry_slots_.size()) {
      Record(Status::kCapacityExceeded);
      return Status::kCapacityExceeded;
    }
    registry_slots_[registry_count_++] = {&registry};
    return Status::kOk;
  }

  void Record(Status status) noexcept {
    if (IsOk(setup_status_) && !IsOk(status)) {
      setup_status_ = status;
    }
  }

  UptimeClock clock_;
  PrintkLogger logger_;
  ThreadExecutor<ExecutorQueueDepth> executor_;
  LocalChannel<MaxTopics, MaxSubscribersPerTopic, MaximumMessageSize> channel_;
  LocalRpc<MaxRpcServices, MaximumMessageSize, MaximumMessageSize, MaxPendingRpc> rpc_;
  FixedArenaAllocator<ArenaBytes> allocator_;
  StaticHardwareManager<MaxHardwareCapabilities> hardware_;
  CoreRef core_;
  Composition composition_;
  std::array<ModuleSlot, MaxModules> module_slots_{};
  std::array<RegistrySlot, kMaximumRegistries> registry_slots_{};
  std::optional<Runtime> runtime_;
  std::size_t module_count_{};
  std::size_t infrastructure_module_count_{};
  std::size_t registry_count_{};
  Status setup_status_{Status::kOk};
};

template <typename Composition>
using ConfiguredStaticNodeRuntime =
    StaticNodeRuntime<Composition, CONFIG_ASTERCTRL_MAX_MODULES, CONFIG_ASTERCTRL_MAX_CHANNELS,
                      CONFIG_ASTERCTRL_MAX_SUBSCRIBERS_PER_CHANNEL,
                      CONFIG_ASTERCTRL_MAX_MESSAGE_BYTES, CONFIG_ASTERCTRL_MAX_RPC_SERVICES,
                      CONFIG_ASTERCTRL_MAX_RPC_SERVICES, CONFIG_ASTERCTRL_ARENA_BYTES,
                      CONFIG_ASTERCTRL_MAX_HARDWARE_CAPABILITIES,
                      CONFIG_ASTERCTRL_EXECUTOR_QUEUE_DEPTH>;

}  // namespace aster::platform::zephyr
