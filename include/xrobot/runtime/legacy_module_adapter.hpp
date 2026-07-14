#pragma once

#include <string_view>

#include "xrobot/runtime/module.hpp"

namespace xrobot::runtime {

struct LegacyLifecycleHooks {
  Status (*initialize)(void*, ModuleContext&) noexcept{};
  Status (*start)(void*) noexcept{};
  void (*shutdown)(void*) noexcept{};
};

class LegacyModuleAdapter final : public Module {
 public:
  constexpr LegacyModuleAdapter(std::string_view name, void* instance,
                                LegacyLifecycleHooks hooks = {}) noexcept
      : name_(name), instance_(instance), hooks_(hooks) {}

  std::string_view Name() const noexcept override { return name_; }

  Status Initialize(ModuleContext& context) noexcept override {
    if (name_.empty() || instance_ == nullptr) {
      return Status::kInvalidArgument;
    }
    return hooks_.initialize == nullptr
               ? Status::kOk
               : hooks_.initialize(instance_, context);
  }

  Status Start() noexcept override {
    return hooks_.start == nullptr ? Status::kOk : hooks_.start(instance_);
  }

  void Shutdown() noexcept override {
    if (hooks_.shutdown != nullptr) {
      hooks_.shutdown(instance_);
    }
  }

  constexpr void* instance() const noexcept { return instance_; }

 private:
  std::string_view name_;
  void* instance_;
  LegacyLifecycleHooks hooks_;
};

}  // namespace xrobot::runtime
