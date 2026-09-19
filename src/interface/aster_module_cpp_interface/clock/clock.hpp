#pragma once

#include <concepts>
#include <cstdint>

#include "aster_module_c_interface/clock/clock_base.h"
#include "aster_module_cpp_interface/status.hpp"

namespace aster {

enum class ClockDomain : std::uint8_t {
  kMonotonic,
  kSynchronized,
  kSimulated,
  kReplay,
};

class ClockRef {
 public:
  constexpr ClockRef() noexcept = default;
  template <typename Backend>
    requires requires(Backend& value) {
      { value.NativeHandle() } -> std::same_as<const aster_clock_base_t*>;
    }
  explicit ClockRef(Backend& value) noexcept : ClockRef(value.NativeHandle()) {}
  constexpr explicit ClockRef(const aster_clock_base_t* clock) noexcept
      : abi_(clock != nullptr && clock->struct_size >= sizeof(*clock) ? clock : nullptr) {}

  [[nodiscard]] Status GetDomain(ClockDomain& output) const noexcept {
    if (abi_ == nullptr || abi_->get_domain == nullptr) return Status::kUnavailable;
    std::uint32_t domain{};
    const auto status = FromAbiStatus(abi_->get_domain(abi_->impl, &domain));
    if (!IsOk(status)) return status;
    if (domain > ASTER_CLOCK_DOMAIN_REPLAY) return Status::kProtocolError;
    output = static_cast<ClockDomain>(domain);
    return Status::kOk;
  }

  [[nodiscard]] Status NowNs(std::uint64_t& output) const noexcept {
    if (abi_ == nullptr || abi_->now_ns == nullptr) return Status::kUnavailable;
    std::uint64_t now{};
    const auto status = FromAbiStatus(abi_->now_ns(abi_->impl, &now));
    if (IsOk(status)) output = now;
    return status;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return abi_ != nullptr; }

  [[nodiscard]] constexpr const aster_clock_base_t* NativeHandle() const noexcept { return abi_; }

 private:
  const aster_clock_base_t* abi_{};
};

}  // namespace aster
