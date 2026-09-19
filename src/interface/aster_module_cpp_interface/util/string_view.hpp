#pragma once

#include <string_view>

#include "aster_module_c_interface/util/string.h"

namespace aster {

[[nodiscard]] constexpr aster_string_view_t ToAbiString(std::string_view value) noexcept {
  return {value.data(), value.size()};
}

[[nodiscard]] constexpr std::string_view FromAbiString(aster_string_view_t value) noexcept {
  return value.data == nullptr ? std::string_view{} : std::string_view(value.data, value.size);
}

}  // namespace aster
