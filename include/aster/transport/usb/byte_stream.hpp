#pragma once

#include <cstddef>
#include <span>

#include "aster/status.hpp"

namespace aster::transport::usb {

class ByteStream {
 public:
  virtual ~ByteStream() = default;
  virtual Status Write(std::span<const std::byte> input, std::size_t& written) noexcept = 0;
  virtual Status Read(std::span<std::byte> output, std::size_t& read) noexcept = 0;
  virtual void Close() noexcept = 0;
};

}  // namespace aster::transport::usb
