#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "aster/channel.hpp"
#include "aster/executor.hpp"
#include "aster/rpc.hpp"
#include "aster/status.hpp"

namespace aster {

class Configurator {
 public:
  virtual ~Configurator() = default;
  virtual Status Get(std::string_view key, std::span<std::byte> output,
                     std::size_t& written) const noexcept = 0;
};

class ConfiguratorRef {
 public:
  constexpr ConfiguratorRef() noexcept = default;
  constexpr explicit ConfiguratorRef(Configurator& configurator) noexcept
      : configurator_(&configurator) {}

  Status Get(std::string_view key, std::span<std::byte> output,
             std::size_t& written) const noexcept {
    written = 0;
    return configurator_ == nullptr ? Status::kUnavailable
                                    : configurator_->Get(key, output, written);
  }

  template <typename Value>
    requires std::is_trivially_copyable_v<Value>
  Status Get(std::string_view key, Value& value) const noexcept {
    std::size_t written{};
    auto bytes = std::as_writable_bytes(std::span<Value>(&value, 1));
    const auto status = Get(key, bytes, written);
    return IsOk(status) && written != sizeof(Value) ? Status::kTypeMismatch : status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return configurator_ != nullptr;
  }

 private:
  Configurator* configurator_{};
};

enum class LogLevel : std::uint8_t {
  kTrace,
  kDebug,
  kInfo,
  kWarning,
  kError,
  kCritical,
};

class Logger {
 public:
  virtual ~Logger() = default;
  virtual Status Write(LogLevel level, std::string_view message,
                       const ExecutionContext& caller) noexcept = 0;
};

class LoggerRef {
 public:
  constexpr LoggerRef() noexcept = default;
  constexpr explicit LoggerRef(Logger& logger) noexcept : logger_(&logger) {}

  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) const noexcept {
    return logger_ == nullptr ? Status::kUnavailable : logger_->Write(level, message, caller);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return logger_ != nullptr; }

 private:
  Logger* logger_{};
};

class ParameterStore {
 public:
  virtual ~ParameterStore() = default;
  virtual Status Get(std::string_view name, std::string_view type, std::span<std::byte> output,
                     std::size_t& written) const noexcept = 0;
  virtual Status Set(std::string_view name, std::string_view type, std::span<const std::byte> value,
                     const ExecutionContext& caller) noexcept = 0;
};

class ParameterRef {
 public:
  constexpr ParameterRef() noexcept = default;
  constexpr explicit ParameterRef(ParameterStore& parameters) noexcept : parameters_(&parameters) {}

  Status Get(std::string_view name, std::string_view type, std::span<std::byte> output,
             std::size_t& written) const noexcept {
    written = 0;
    return parameters_ == nullptr ? Status::kUnavailable
                                  : parameters_->Get(name, type, output, written);
  }
  Status Set(std::string_view name, std::string_view type, std::span<const std::byte> value,
             const ExecutionContext& caller) const noexcept {
    return parameters_ == nullptr ? Status::kUnavailable
                                  : parameters_->Set(name, type, value, caller);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return parameters_ != nullptr; }

 private:
  ParameterStore* parameters_{};
};

enum class ClockDomain : std::uint8_t {
  kMonotonic,
  kSynchronized,
  kSimulated,
  kReplay,
};

class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual ClockDomain domain() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t NowNs() const noexcept = 0;
};

class ClockRef {
 public:
  constexpr ClockRef() noexcept = default;
  constexpr explicit ClockRef(Clock& clock) noexcept : clock_(&clock) {}

  [[nodiscard]] ClockDomain domain() const noexcept {
    return clock_ == nullptr ? ClockDomain::kMonotonic : clock_->domain();
  }
  [[nodiscard]] std::uint64_t NowNs() const noexcept {
    return clock_ == nullptr ? 0 : clock_->NowNs();
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return clock_ != nullptr; }

 private:
  Clock* clock_{};
};

class Allocator {
 public:
  virtual ~Allocator() = default;
  virtual void* Allocate(std::size_t size, std::size_t alignment) noexcept = 0;
  virtual void Deallocate(void* memory, std::size_t size, std::size_t alignment) noexcept = 0;
};

class AllocatorRef {
 public:
  constexpr AllocatorRef() noexcept = default;
  constexpr explicit AllocatorRef(Allocator& allocator) noexcept : allocator_(&allocator) {}

  void* Allocate(std::size_t size, std::size_t alignment) const noexcept {
    return allocator_ == nullptr ? nullptr : allocator_->Allocate(size, alignment);
  }
  void Deallocate(void* memory, std::size_t size, std::size_t alignment) const noexcept {
    if (allocator_ != nullptr) {
      allocator_->Deallocate(memory, size, alignment);
    }
  }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return allocator_ != nullptr; }

 private:
  Allocator* allocator_{};
};

class HardwareManager {
 public:
  virtual ~HardwareManager() = default;
  virtual Status Resolve(std::string_view name, std::string_view type, void*& device) noexcept = 0;
};

class HardwareManagerRef {
 public:
  constexpr HardwareManagerRef() noexcept = default;
  constexpr explicit HardwareManagerRef(HardwareManager& hardware) noexcept
      : hardware_(&hardware) {}

  Status Resolve(std::string_view name, std::string_view type, void*& device) const noexcept {
    device = nullptr;
    return hardware_ == nullptr ? Status::kUnavailable : hardware_->Resolve(name, type, device);
  }

  template <typename Device>
  Status Resolve(std::string_view name, Device*& device) const noexcept
    requires requires {
      { Device::TypeName() } -> std::convertible_to<std::string_view>;
    }
  {
    void* resolved{};
    const auto status = Resolve(name, Device::TypeName(), resolved);
    device = IsOk(status) ? static_cast<Device*>(resolved) : nullptr;
    return status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return hardware_ != nullptr; }

 private:
  HardwareManager* hardware_{};
};

struct CoreHandles {
  ConfiguratorRef configurator;
  LoggerRef logger;
  ExecutorRef executor;
  ChannelRef channel;
  RpcRef rpc;
  ParameterRef parameter;
  ClockRef clock;
  AllocatorRef allocator;
  HardwareManagerRef hardware;
};

class CoreRef {
 public:
  constexpr CoreRef() noexcept = default;
  constexpr explicit CoreRef(CoreHandles handles) noexcept : handles_(handles) {}

  [[nodiscard]] constexpr ConfiguratorRef configurator() const noexcept {
    return handles_.configurator;
  }
  [[nodiscard]] constexpr LoggerRef logger() const noexcept { return handles_.logger; }
  [[nodiscard]] constexpr ExecutorRef executor() const noexcept { return handles_.executor; }
  [[nodiscard]] constexpr ChannelRef channel() const noexcept { return handles_.channel; }
  [[nodiscard]] constexpr RpcRef rpc() const noexcept { return handles_.rpc; }
  [[nodiscard]] constexpr ParameterRef parameter() const noexcept { return handles_.parameter; }
  [[nodiscard]] constexpr ClockRef clock() const noexcept { return handles_.clock; }
  [[nodiscard]] constexpr AllocatorRef allocator() const noexcept { return handles_.allocator; }
  [[nodiscard]] constexpr HardwareManagerRef hardware() const noexcept { return handles_.hardware; }

 private:
  CoreHandles handles_{};
};

}  // namespace aster
