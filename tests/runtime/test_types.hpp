#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "aster/rpc.hpp"
#include "aster/type_support.hpp"

namespace test {

struct Sample {
  std::uint32_t value{};
};

struct AddRequest {
  std::uint32_t left{};
  std::uint32_t right{};
};

struct AddResponse {
  std::uint32_t sum{};
};

struct AddService {};

inline constexpr aster::SchemaHash kSampleHash{{std::byte{0x01}}};
inline constexpr aster::SchemaHash kAddRequestHash{{std::byte{0x02}}};
inline constexpr aster::SchemaHash kAddResponseHash{{std::byte{0x03}}};
inline constexpr aster::SchemaHash kAddServiceHash{{std::byte{0x04}}};

inline aster::Status EncodeU32(std::uint32_t value, std::span<std::byte> output,
                               std::size_t& written) noexcept {
  written = 0;
  if (output.size() < 4) {
    return aster::Status::kCapacityExceeded;
  }
  for (std::size_t index = 0; index < 4; ++index) {
    output[index] = static_cast<std::byte>(value >> (index * 8U));
  }
  written = 4;
  return aster::Status::kOk;
}

inline aster::Status DecodeU32(std::span<const std::byte> input, std::uint32_t& value) noexcept {
  if (input.size() != 4) {
    return aster::Status::kInvalidArgument;
  }
  value = 0;
  for (std::size_t index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index]))
             << (index * 8U);
  }
  return aster::Status::kOk;
}

}  // namespace test

namespace aster {

template <>
struct TypeSupport<test::Sample> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.Sample", test::kSampleHash, 4};
  }
  static Status Encode(const test::Sample& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    return test::EncodeU32(value.value, output, written);
  }
  static Status Decode(std::span<const std::byte> input, test::Sample& value) noexcept {
    return test::DecodeU32(input, value.value);
  }
};

template <>
struct TypeSupport<test::AddRequest> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.AddRequest", test::kAddRequestHash, 8};
  }
  static Status Encode(const test::AddRequest& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    written = 0;
    if (output.size() < 8) {
      return Status::kCapacityExceeded;
    }
    std::size_t first{};
    std::size_t second{};
    const auto left = test::EncodeU32(value.left, output.first(4), first);
    const auto right = test::EncodeU32(value.right, output.subspan(4, 4), second);
    written = first + second;
    return !IsOk(left) ? left : right;
  }
  static Status Decode(std::span<const std::byte> input, test::AddRequest& value) noexcept {
    if (input.size() != 8) {
      return Status::kInvalidArgument;
    }
    const auto left = test::DecodeU32(input.first(4), value.left);
    const auto right = test::DecodeU32(input.subspan(4, 4), value.right);
    return !IsOk(left) ? left : right;
  }
};

template <>
struct TypeSupport<test::AddResponse> {
  static constexpr TypeDescriptor descriptor() noexcept {
    return {"test.AddResponse", test::kAddResponseHash, 4};
  }
  static Status Encode(const test::AddResponse& value, std::span<std::byte> output,
                       std::size_t& written) noexcept {
    return test::EncodeU32(value.sum, output, written);
  }
  static Status Decode(std::span<const std::byte> input, test::AddResponse& value) noexcept {
    return test::DecodeU32(input, value.sum);
  }
};

template <>
struct ServiceTypeSupport<test::AddService> {
  using Request = test::AddRequest;
  using Response = test::AddResponse;

  static constexpr ServiceDescriptor descriptor() noexcept {
    return {"test.Add", test::kAddServiceHash, TypeSupport<Request>::descriptor(),
            TypeSupport<Response>::descriptor()};
  }
};

}  // namespace aster
