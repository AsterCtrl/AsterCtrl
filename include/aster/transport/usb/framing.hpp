#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/status.hpp"
#include "aster/transport/transport.hpp"
#include "aster/transport/usb/cobs.hpp"
#include "aster/transport/usb/crc32c.hpp"

namespace aster::transport::usb {

inline constexpr std::uint16_t kMagic = 0xa57eU;
inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 44;
inline constexpr std::size_t kChecksumSize = 4;

namespace detail {

template <typename Integer>
void WriteLittleEndian(Integer value, std::span<std::byte> output) noexcept {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = static_cast<std::byte>(static_cast<std::uint64_t>(value) >> (index * 8U));
  }
}

template <typename Integer>
Integer ReadLittleEndian(std::span<const std::byte> input) noexcept {
  std::uint64_t value{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[index]))
             << (index * 8U);
  }
  return static_cast<Integer>(value);
}

}  // namespace detail

template <std::size_t MaxPayload>
class PacketCodec {
 public:
  static_assert(MaxPayload > 0);
  static constexpr std::size_t kRawCapacity = kHeaderSize + MaxPayload + kChecksumSize;
  static constexpr std::size_t kEncodedCapacity = kRawCapacity + (kRawCapacity / 254U) + 2U;

  Status Encode(const PacketView& packet, std::span<std::byte> output,
                std::size_t& written) noexcept {
    written = 0;
    if (packet.header.route_id == 0 || packet.payload.size() > MaxPayload ||
        packet.header.kind < PacketKind::kChannel || packet.header.kind > PacketKind::kControl) {
      return Status::kInvalidArgument;
    }
    const auto raw_size = kHeaderSize + packet.payload.size() + kChecksumSize;
    detail::WriteLittleEndian(kMagic, std::span(raw_).subspan(0, 2));
    raw_[2] = static_cast<std::byte>(kProtocolVersion);
    raw_[3] = static_cast<std::byte>(packet.header.kind);
    detail::WriteLittleEndian(packet.header.route_id, std::span(raw_).subspan(4, 2));
    detail::WriteLittleEndian(packet.header.sequence, std::span(raw_).subspan(6, 4));
    detail::WriteLittleEndian(packet.header.source_timestamp_ns, std::span(raw_).subspan(10, 8));
    detail::WriteLittleEndian(packet.header.deadline_ns, std::span(raw_).subspan(18, 8));
    std::copy(packet.header.schema_hash.bytes.begin(), packet.header.schema_hash.bytes.end(),
              raw_.begin() + 26);
    detail::WriteLittleEndian(static_cast<std::uint16_t>(packet.payload.size()),
                              std::span(raw_).subspan(42, 2));
    std::copy(packet.payload.begin(), packet.payload.end(), raw_.begin() + kHeaderSize);
    const auto body_size = kHeaderSize + packet.payload.size();
    detail::WriteLittleEndian(Crc32c(std::span(raw_).first(body_size)),
                              std::span(raw_).subspan(body_size, 4));

    if (output.size() < 2) {
      return Status::kCapacityExceeded;
    }
    std::size_t encoded_size{};
    const auto status =
        CobsEncode(std::span(raw_).first(raw_size), output.first(output.size() - 1U), encoded_size);
    if (!IsOk(status)) {
      return status;
    }
    output[encoded_size] = std::byte{0};
    written = encoded_size + 1U;
    return Status::kOk;
  }

  Status Decode(std::span<const std::byte> encoded, PacketView& packet) noexcept {
    packet = {};
    if (encoded.size() < 2 || encoded.back() != std::byte{0}) {
      return Status::kProtocolError;
    }
    std::size_t raw_size{};
    auto status = CobsDecode(encoded.first(encoded.size() - 1U), raw_, raw_size);
    if (!IsOk(status)) {
      return status;
    }
    if (raw_size < kHeaderSize + kChecksumSize) {
      return Status::kProtocolError;
    }
    if (detail::ReadLittleEndian<std::uint16_t>(std::span<const std::byte>(raw_).subspan<0, 2>()) !=
        kMagic) {
      return Status::kProtocolError;
    }
    if (std::to_integer<std::uint8_t>(raw_[2]) != kProtocolVersion) {
      return Status::kVersionMismatch;
    }
    const auto payload_size =
        detail::ReadLittleEndian<std::uint16_t>(std::span<const std::byte>(raw_).subspan<42, 2>());
    if (payload_size > MaxPayload) {
      return Status::kCapacityExceeded;
    }
    if (raw_size != kHeaderSize + payload_size + kChecksumSize) {
      return Status::kProtocolError;
    }
    const auto expected_crc = detail::ReadLittleEndian<std::uint32_t>(
        std::span<const std::byte>(raw_).subspan(raw_size - 4U, 4));
    if (Crc32c(std::span<const std::byte>(raw_).first(raw_size - 4U)) != expected_crc) {
      return Status::kProtocolError;
    }
    const auto kind = static_cast<PacketKind>(std::to_integer<std::uint8_t>(raw_[3]));
    if (kind < PacketKind::kChannel || kind > PacketKind::kControl) {
      return Status::kProtocolError;
    }
    packet.header.route_id =
        detail::ReadLittleEndian<std::uint16_t>(std::span<const std::byte>(raw_).subspan<4, 2>());
    packet.header.kind = kind;
    packet.header.sequence =
        detail::ReadLittleEndian<std::uint32_t>(std::span<const std::byte>(raw_).subspan<6, 4>());
    packet.header.source_timestamp_ns =
        detail::ReadLittleEndian<std::uint64_t>(std::span<const std::byte>(raw_).subspan<10, 8>());
    packet.header.deadline_ns =
        detail::ReadLittleEndian<std::uint64_t>(std::span<const std::byte>(raw_).subspan<18, 8>());
    std::copy(raw_.begin() + 26, raw_.begin() + 42, packet.header.schema_hash.bytes.begin());
    packet.payload = std::span<const std::byte>(raw_).subspan(kHeaderSize, payload_size);
    return packet.header.route_id == 0 ? Status::kProtocolError : Status::kOk;
  }

 private:
  std::array<std::byte, kRawCapacity> raw_{};
};

}  // namespace aster::transport::usb
