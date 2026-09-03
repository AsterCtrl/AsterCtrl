#pragma once

#include <zephyr/device.h>

#include <cstddef>
#include <span>

#include "aster/status.hpp"
#include "aster/transport/usb/byte_stream.hpp"

namespace aster::platform::zephyr {

class CdcAcmByteStream final : public transport::usb::ByteStream {
 public:
  explicit CdcAcmByteStream(const device& uart) noexcept : uart_(uart) {}

  [[nodiscard]] Status Ready() const noexcept;
  Status Write(std::span<const std::byte> input, std::size_t& written) noexcept override;
  Status Read(std::span<std::byte> output, std::size_t& read) noexcept override;
  void Close() noexcept override {}

 private:
  const device& uart_;
};

}  // namespace aster::platform::zephyr
