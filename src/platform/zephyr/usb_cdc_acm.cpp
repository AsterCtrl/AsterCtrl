#include "aster/platform/zephyr/usb_cdc_acm.hpp"

#include <zephyr/drivers/uart.h>

#include <cstddef>
#include <cstdint>

namespace aster::platform::zephyr {

Status CdcAcmByteStream::Ready() const noexcept {
  return device_is_ready(&uart_) ? Status::kOk : Status::kUnavailable;
}

Status CdcAcmByteStream::Write(std::span<const std::byte> input, std::size_t& written) noexcept {
  written = 0;
  if (!device_is_ready(&uart_)) {
    return Status::kUnavailable;
  }
  for (const auto value : input) {
    uart_poll_out(&uart_, std::to_integer<unsigned char>(value));
    ++written;
  }
  return Status::kOk;
}

Status CdcAcmByteStream::Read(std::span<std::byte> output, std::size_t& read) noexcept {
  read = 0;
  if (!device_is_ready(&uart_)) {
    return Status::kUnavailable;
  }
  while (read < output.size()) {
    unsigned char value{};
    if (uart_poll_in(&uart_, &value) != 0) {
      break;
    }
    output[read++] = static_cast<std::byte>(value);
  }
  return read == 0 ? Status::kUnavailable : Status::kOk;
}

}  // namespace aster::platform::zephyr
