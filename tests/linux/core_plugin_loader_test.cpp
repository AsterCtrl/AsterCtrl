#include <cassert>
#include <cstdint>

#include "aster/platform/linux/plugin_loader.hpp"

namespace {

struct TestBackendV1 {
  std::uint32_t interface_version;
  std::uint32_t struct_size;
  void* context;
  std::int32_t (*read_value)(void* context);
};

}  // namespace

int main() {
  using aster::IsOk;
  using aster::Status;
  using aster::platform::linux::CorePluginLoader;
  using aster::platform::linux::PluginLoader;

  CorePluginLoader loader;
  assert(IsOk(loader.Open(ASTER_TEST_CORE_PLUGIN_PATH)));
  assert(loader.name() == "test-core-plugin");
  assert(loader.version() == "0.2.0");

  const void* table{};
  assert(IsOk(loader.QueryInterface("test.backend", 1U, sizeof(TestBackendV1), table)));
  const auto& backend = *static_cast<const TestBackendV1*>(table);
  assert(backend.interface_version == 1U);
  assert(backend.struct_size == sizeof(TestBackendV1));
  assert(backend.read_value(backend.context) == 42);

  assert(loader.QueryInterface("missing.backend", 1U, sizeof(TestBackendV1), table) ==
         Status::kNotFound);
  assert(loader.QueryInterface("test.backend", 2U, sizeof(TestBackendV1), table) ==
         Status::kVersionMismatch);
  assert(loader.QueryInterface("test.backend", 1U, sizeof(TestBackendV1) + 1U, table) ==
         Status::kVersionMismatch);
  loader.Close();
  assert(loader.QueryInterface("test.backend", 1U, sizeof(TestBackendV1), table) ==
         Status::kInvalidState);

  PluginLoader module_loader;
  assert(module_loader.Open(ASTER_TEST_CORE_PLUGIN_PATH, {}) == Status::kNotFound);
  assert(loader.Open(ASTER_TEST_MODULE_PLUGIN_PATH) == Status::kNotFound);
  return 0;
}
