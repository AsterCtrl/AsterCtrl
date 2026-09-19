#pragma once

#include <cstddef>

#include "aster_module_cpp_interface/execution.hpp"
#include "aster_module_cpp_interface/status.hpp"
#include "aster_module_cpp_interface/util/string_view.hpp"

namespace aster::detail {
inline bool ValidView(aster_string_view_t value) noexcept {
  return value.data != nullptr && value.size != 0;
}
inline bool ValidBuffer(const void* data, std::size_t size) noexcept {
  return size == 0 || data != nullptr;
}
}  // namespace aster::detail
