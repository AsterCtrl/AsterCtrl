#pragma once

#include "aster_module_cpp_interface/clock/clock.hpp"

namespace aster {

class Clock {
 public:
  Clock() noexcept;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  [[nodiscard]] const aster_clock_base_t* NativeHandle() const noexcept { return &native_; }
  virtual ~Clock() = default;
  [[nodiscard]] virtual ClockDomain domain() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t NowNs() const noexcept = 0;

 private:
  static aster_status_t ClockGetDomain(void* context, std::uint32_t* domain) noexcept;
  static aster_status_t ClockNowNs(void* context, std::uint64_t* now_ns) noexcept;
  aster_clock_base_t native_;
};

}  // namespace aster
