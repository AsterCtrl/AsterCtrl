#include <array>
#include <span>

#include "aster/runtime.hpp"

int main() {
  std::array<aster::ModuleSlot, 0> modules{};
  aster::Runtime runtime{std::span<aster::ModuleSlot>(modules)};
  if (!aster::IsOk(runtime.Initialize()) || !aster::IsOk(runtime.Start())) {
    return 1;
  }
  runtime.Shutdown();
  return runtime.state() == aster::RuntimeState::kStopped ? 0 : 1;
}
