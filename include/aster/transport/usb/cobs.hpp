#pragma once

#include <cstddef>
#include <span>

#include "aster/status.hpp"

namespace aster::transport::usb {

inline Status CobsEncode(std::span<const std::byte> input, std::span<std::byte> output,
                         std::size_t& written) noexcept {
  written = 0;
  if (output.empty()) {
    return Status::kCapacityExceeded;
  }

  std::size_t code_index = 0;
  std::size_t output_index = 1;
  std::byte code{1};
  for (const auto value : input) {
    if (value == std::byte{0}) {
      if (code_index >= output.size()) {
        return Status::kCapacityExceeded;
      }
      output[code_index] = code;
      code_index = output_index++;
      code = std::byte{1};
      if (output_index > output.size()) {
        return Status::kCapacityExceeded;
      }
      continue;
    }
    if (output_index >= output.size()) {
      return Status::kCapacityExceeded;
    }
    output[output_index++] = value;
    code = static_cast<std::byte>(std::to_integer<unsigned int>(code) + 1U);
    if (code == std::byte{0xff}) {
      output[code_index] = code;
      code_index = output_index++;
      code = std::byte{1};
      if (output_index > output.size()) {
        return Status::kCapacityExceeded;
      }
    }
  }
  output[code_index] = code;
  written = output_index;
  return Status::kOk;
}

inline Status CobsDecode(std::span<const std::byte> input, std::span<std::byte> output,
                         std::size_t& written) noexcept {
  written = 0;
  if (input.empty()) {
    return Status::kProtocolError;
  }
  std::size_t input_index = 0;
  while (input_index < input.size()) {
    const auto code = std::to_integer<std::size_t>(input[input_index++]);
    if (code == 0 || input_index + code - 1U > input.size()) {
      return Status::kProtocolError;
    }
    for (std::size_t offset = 1; offset < code; ++offset) {
      if (written == output.size() || input[input_index] == std::byte{0}) {
        return written == output.size() ? Status::kCapacityExceeded : Status::kProtocolError;
      }
      output[written++] = input[input_index++];
    }
    if (code != 0xff && input_index < input.size()) {
      if (written == output.size()) {
        return Status::kCapacityExceeded;
      }
      output[written++] = std::byte{0};
    }
  }
  return Status::kOk;
}

}  // namespace aster::transport::usb
