#pragma once

#include <cstddef>
#include <cstdint>

#include "aster/core_ref.hpp"
#include "aster/static_hardware.hpp"

namespace aster::sim {

class ManualClock final : public Clock {
 public:
  [[nodiscard]] ClockDomain domain() const noexcept override { return ClockDomain::kSimulated; }

  [[nodiscard]] std::uint64_t NowNs() const noexcept override { return now_ns_; }

  Status Set(std::uint64_t value) noexcept {
    if (value < now_ns_) {
      return Status::kInvalidArgument;
    }
    now_ns_ = value;
    return Status::kOk;
  }

  Status Advance(std::uint64_t delta_ns) noexcept {
    if (delta_ns > UINT64_MAX - now_ns_) {
      return Status::kCapacityExceeded;
    }
    now_ns_ += delta_ns;
    return Status::kOk;
  }

 private:
  std::uint64_t now_ns_{};
};

template <std::size_t MaxDevices>
using FakeHardwareManager = ::aster::StaticHardwareManager<MaxDevices>;

}  // namespace aster::sim
