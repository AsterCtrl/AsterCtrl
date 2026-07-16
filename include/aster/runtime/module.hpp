#pragma once

#include <string_view>

#include "aster/runtime/module_context.hpp"
#include "aster/runtime/status.hpp"

namespace aster::runtime {

class Module {
 public:
  virtual ~Module() = default;

  virtual std::string_view Name() const noexcept = 0;
  virtual Status Initialize(ModuleContext& context) noexcept = 0;
  virtual Status Start() noexcept = 0;
  virtual void Shutdown() noexcept = 0;
};

struct ModuleSlot {
  Module* module{};
  ModuleContext* context{};
};

}  // namespace aster::runtime
