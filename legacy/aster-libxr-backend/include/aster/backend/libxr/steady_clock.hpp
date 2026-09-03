#pragma once

#include <cstdint>

#include "timebase.hpp"
#include "aster/runtime/runtime_services.hpp"
#include "aster/transport/can/link.hpp"

namespace aster::backend::libxr {

class SteadyClock final : public aster::runtime::SteadyClock {
 public:
  std::uint64_t NowNs() const noexcept override {
    return static_cast<std::uint64_t>(LibXR::Timebase::GetMicroseconds()) *
           1000ULL;
  }

  aster::transport::can::CanClockReader reader() noexcept {
    return {Read, this};
  }

 private:
  static std::uint64_t Read(void* state) noexcept {
    return static_cast<SteadyClock*>(state)->NowNs();
  }
};

}  // namespace aster::backend::libxr
