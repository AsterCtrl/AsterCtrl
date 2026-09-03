/*
 * Copyright (c) 2026 AsterCtrl contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstddef>
#include <cstdint>
#include <span>

#include "state.pb.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  aster::examples::local::v1::State value{};
  const auto bytes = std::as_bytes(std::span(data, size));
  static_cast<void>(aster::TypeSupport<aster::examples::local::v1::State>::Decode(bytes, value));
  return 0;
}
