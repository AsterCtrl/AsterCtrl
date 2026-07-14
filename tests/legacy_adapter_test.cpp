#include <array>
#include <cassert>

#include "xrobot/runtime/legacy_module_adapter.hpp"
#include "xrobot/runtime/runtime.hpp"

namespace {

using xrobot::runtime::LegacyLifecycleHooks;
using xrobot::runtime::LegacyModuleAdapter;
using xrobot::runtime::ModuleContext;
using xrobot::runtime::ModuleSlot;
using xrobot::runtime::Runtime;
using xrobot::runtime::Status;

struct LegacyState {
  int initialize_count{};
  int start_count{};
  int shutdown_count{};
};

Status Initialize(void* instance, ModuleContext&) noexcept {
  ++static_cast<LegacyState*>(instance)->initialize_count;
  return Status::kOk;
}

Status Start(void* instance) noexcept {
  ++static_cast<LegacyState*>(instance)->start_count;
  return Status::kOk;
}

void Shutdown(void* instance) noexcept {
  ++static_cast<LegacyState*>(instance)->shutdown_count;
}

void AdapterParticipatesInTheNewRuntimeLifecycle() {
  LegacyState legacy;
  LegacyLifecycleHooks hooks{Initialize, Start, Shutdown};
  LegacyModuleAdapter adapter("legacy", &legacy, hooks);
  ModuleContext context("node", "legacy");
  std::array slots{ModuleSlot{&adapter, &context}};
  Runtime runtime(slots);

  assert(runtime.Initialize() == Status::kOk);
  assert(runtime.Start() == Status::kOk);
  runtime.Shutdown();
  assert(legacy.initialize_count == 1);
  assert(legacy.start_count == 1);
  assert(legacy.shutdown_count == 1);
  assert(adapter.instance() == &legacy);
}

}  // namespace

int main() {
  AdapterParticipatesInTheNewRuntimeLifecycle();
  return 0;
}
