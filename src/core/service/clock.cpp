#include "aster_runtime/core/clock.hpp"

#include "validation.hpp"

namespace aster {

Clock::Clock() noexcept : native_{sizeof(aster_clock_base_t), this, ClockGetDomain, ClockNowNs} {}

aster_status_t Clock::ClockGetDomain(void* context, std::uint32_t* domain) noexcept {
  if (context == nullptr || domain == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  auto& clock = *static_cast<Clock*>(context);
  *domain = static_cast<std::uint32_t>(clock.domain());
  return ASTER_STATUS_OK;
}

aster_status_t Clock::ClockNowNs(void* context, std::uint64_t* now_ns) noexcept {
  if (context == nullptr || now_ns == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  auto& clock = *static_cast<Clock*>(context);
  *now_ns = clock.NowNs();
  return ASTER_STATUS_OK;
}

}  // namespace aster
