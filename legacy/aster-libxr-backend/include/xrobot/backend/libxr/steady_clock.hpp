#pragma once

#include <cstdint>

#include "timebase.hpp"
#include "xrobot/runtime/runtime_services.hpp"
#include "xrobot/transport/can/link.hpp"

namespace xrobot::backend::libxr {

class SteadyClock final : public xrobot::runtime::SteadyClock {
 public:
  std::uint64_t NowNs() const noexcept override {
    return static_cast<std::uint64_t>(LibXR::Timebase::GetMicroseconds()) *
           1000ULL;
  }

  xrobot::transport::can::CanClockReader reader() noexcept {
    return {Read, this};
  }

 private:
  static std::uint64_t Read(void* state) noexcept {
    return static_cast<SteadyClock*>(state)->NowNs();
  }
};

}  // namespace xrobot::backend::libxr
