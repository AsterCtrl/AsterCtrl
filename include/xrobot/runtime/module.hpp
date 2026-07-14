#pragma once

#include <string_view>

#include "xrobot/runtime/module_context.hpp"
#include "xrobot/runtime/status.hpp"

namespace xrobot::runtime {

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

}  // namespace xrobot::runtime
