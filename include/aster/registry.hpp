#pragma once

#include "aster/status.hpp"

namespace aster {

class Registry {
 public:
  virtual ~Registry() = default;

  virtual Status Seal() noexcept = 0;
  [[nodiscard]] virtual bool sealed() const noexcept = 0;
};

struct RegistrySlot {
  Registry* registry{};
};

}  // namespace aster
