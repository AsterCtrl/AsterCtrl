#pragma once

#include <cstdint>
#include <string_view>

#include "aster/core_ref.hpp"
#include "aster/status.hpp"

namespace aster {

struct SemanticVersion {
  std::uint16_t major{};
  std::uint16_t minor{};
  std::uint16_t patch{};

  constexpr bool operator==(const SemanticVersion&) const noexcept = default;
};

struct ModuleInfo {
  std::string_view name;
  std::string_view type;
  std::string_view package;
  SemanticVersion version;
};

class Module {
 public:
  virtual ~Module() = default;

  [[nodiscard]] virtual ModuleInfo Info() const noexcept = 0;
  virtual Status Initialize(CoreRef core) noexcept = 0;
  virtual Status Start() noexcept = 0;
  virtual void Shutdown() noexcept = 0;
};

struct ModuleSlot {
  Module* module{};
  CoreRef core;
  std::string_view instance_name;
};

}  // namespace aster
