#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>

#include "aster_module_c_interface/logger/logger_base.h"
#include "aster_module_cpp_interface/execution.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster {

enum class LogLevel : std::uint8_t {
  kTrace,
  kDebug,
  kInfo,
  kWarning,
  kError,
  kCritical,
};

class LoggerRef {
 public:
  constexpr LoggerRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_logger_base_t*>;
    }
  explicit LoggerRef(Backend& value) noexcept : LoggerRef(value.NativeHandle()) {}
  constexpr explicit LoggerRef(const aster_logger_base_t* logger) noexcept
      : abi_(logger != nullptr && logger->struct_size >= sizeof(*logger) ? logger : nullptr) {}

  Status Write(LogLevel level, std::string_view message) const noexcept {
    return abi_ == nullptr || abi_->write == nullptr
               ? Status::kUnavailable
               : FromAbiStatus(abi_->write(abi_->impl, static_cast<std::uint32_t>(level),
                                           ToAbiString(message), nullptr));
  }

  Status Write(LogLevel level, std::string_view message,
               const ExecutionContext& caller) const noexcept {
    if (abi_ == nullptr || abi_->write == nullptr) {
      return Status::kUnavailable;
    }
    const auto context = ToAbiExecutionContext(caller);
    return FromAbiStatus(
        abi_->write(abi_->impl, static_cast<std::uint32_t>(level), ToAbiString(message), &context));
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  [[nodiscard]] constexpr const aster_logger_base_t* NativeHandle() const noexcept { return abi_; }

 private:
  const aster_logger_base_t* abi_{};
};

}  // namespace aster
