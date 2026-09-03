#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>

#include "aster/transport/usb/framing.hpp"

namespace {

void RoundTripsBinaryPayloadAndMetadata() {
  aster::transport::usb::PacketCodec<32> encoder;
  aster::transport::usb::PacketCodec<32> decoder;
  std::array<std::byte, aster::transport::usb::PacketCodec<32>::kEncodedCapacity> encoded{};
  const std::array payload{std::byte{0}, std::byte{1}, std::byte{0xff}, std::byte{0}};
  aster::SchemaHash schema{};
  schema.bytes[3] = std::byte{0xaa};
  const aster::transport::PacketView source{
      {17, aster::transport::PacketKind::kRpcRequest, 42, 1000, 2500, schema}, payload};
  std::size_t written{};
  assert(encoder.Encode(source, encoded, written) == aster::Status::kOk);
  assert(written > payload.size());
  assert(encoded[written - 1] == std::byte{0});

  aster::transport::PacketView decoded;
  assert(decoder.Decode(std::span(encoded).first(written), decoded) == aster::Status::kOk);
  assert(decoded.header.route_id == source.header.route_id);
  assert(decoded.header.kind == source.header.kind);
  assert(decoded.header.sequence == source.header.sequence);
  assert(decoded.header.source_timestamp_ns == source.header.source_timestamp_ns);
  assert(decoded.header.deadline_ns == source.header.deadline_ns);
  assert(decoded.header.schema_hash == source.header.schema_hash);
  assert(
      std::equal(decoded.payload.begin(), decoded.payload.end(), payload.begin(), payload.end()));
}

void RejectsCorruptionAndTruncation() {
  aster::transport::usb::PacketCodec<8> encoder;
  aster::transport::usb::PacketCodec<8> decoder;
  std::array<std::byte, aster::transport::usb::PacketCodec<8>::kEncodedCapacity> encoded{};
  const std::array payload{std::byte{1}, std::byte{2}};
  aster::SchemaHash schema{};
  const aster::transport::PacketView source{
      {4, aster::transport::PacketKind::kChannel, 1, 2, 3, schema}, payload};
  std::size_t written{};
  assert(encoder.Encode(source, encoded, written) == aster::Status::kOk);

  aster::transport::PacketView decoded;
  assert(decoder.Decode(std::span(encoded).first(written - 1), decoded) ==
         aster::Status::kProtocolError);

  std::array<std::byte, aster::transport::usb::PacketCodec<8>::kRawCapacity> raw{};
  std::size_t raw_size{};
  assert(aster::transport::usb::CobsDecode(std::span(encoded).first(written - 1), raw, raw_size) ==
         aster::Status::kOk);
  raw[aster::transport::usb::kHeaderSize] ^= std::byte{1};
  std::array<std::byte, aster::transport::usb::PacketCodec<8>::kEncodedCapacity> corrupted{};
  std::size_t corrupted_size{};
  assert(aster::transport::usb::CobsEncode(std::span(raw).first(raw_size),
                                           std::span(corrupted).first(corrupted.size() - 1),
                                           corrupted_size) == aster::Status::kOk);
  corrupted[corrupted_size++] = std::byte{0};
  assert(decoder.Decode(std::span(corrupted).first(corrupted_size), decoded) ==
         aster::Status::kProtocolError);
}

void SeparatesMalformedCobsFromCapacityAndEncodeArguments() {
  constexpr std::array<std::byte, 0> empty{};
  constexpr std::array malformed_code{std::byte{2}};
  constexpr std::array embedded_zero{std::byte{2}, std::byte{0}};
  std::array<std::byte, 8> output{};
  std::size_t written{};
  assert(aster::transport::usb::CobsDecode(empty, output, written) ==
         aster::Status::kProtocolError);
  assert(aster::transport::usb::CobsDecode(malformed_code, output, written) ==
         aster::Status::kProtocolError);
  assert(aster::transport::usb::CobsDecode(embedded_zero, output, written) ==
         aster::Status::kProtocolError);

  constexpr std::array valid{std::byte{2}, std::byte{1}};
  std::array<std::byte, 0> no_capacity{};
  assert(aster::transport::usb::CobsDecode(valid, no_capacity, written) ==
         aster::Status::kCapacityExceeded);

  aster::transport::usb::PacketCodec<8> codec;
  aster::transport::PacketView decoded;
  constexpr std::array malformed_frame{std::byte{2}, std::byte{0}};
  assert(codec.Decode(malformed_frame, decoded) == aster::Status::kProtocolError);

  const std::array payload{std::byte{1}};
  aster::SchemaHash schema{};
  const aster::transport::PacketView invalid_source{
      {0, aster::transport::PacketKind::kChannel, 1, 2, 3, schema}, payload};
  std::array<std::byte, aster::transport::usb::PacketCodec<8>::kEncodedCapacity> encoded{};
  assert(codec.Encode(invalid_source, encoded, written) == aster::Status::kInvalidArgument);
}

}  // namespace

int main() {
  RoundTripsBinaryPayloadAndMetadata();
  RejectsCorruptionAndTruncation();
  SeparatesMalformedCobsFromCapacityAndEncodeArguments();
  return 0;
}
