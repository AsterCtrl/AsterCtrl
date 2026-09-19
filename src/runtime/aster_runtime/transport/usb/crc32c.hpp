#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace aster::transport::usb {

inline std::uint32_t Crc32c(std::span<const std::byte> input) noexcept {
  std::uint32_t crc = 0xffffffffU;
  for (const auto value : input) {
    crc ^= std::to_integer<std::uint8_t>(value);
    for (std::uint8_t bit = 0; bit < 8; ++bit) {
      const auto mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0x82f63b78U & mask);
    }
  }
  return ~crc;
}

}  // namespace aster::transport::usb
