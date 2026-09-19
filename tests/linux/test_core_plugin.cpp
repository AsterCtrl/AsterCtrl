#include <cstddef>
#include <cstdint>
#include <cstring>

#include "aster_core_plugin_interface/core_plugin_main.h"

namespace {

struct TestBackend {
  std::uint32_t interface_version;
  std::uint32_t struct_size;
  void* context;
  std::int32_t (*read_value)(void* context);
};

std::int32_t ReadValue(void*) { return 42; }

const TestBackend kBackend{1U, sizeof(TestBackend), nullptr, ReadValue};

aster_status_t QueryInterface(void*, aster_string_view_t name, std::uint32_t version,
                              const void** interface_table, std::uint32_t* interface_struct_size) {
  if (interface_table == nullptr || interface_struct_size == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  *interface_table = nullptr;
  *interface_struct_size = 0;
  constexpr char kName[] = "test.backend";
  if (name.data == nullptr || name.size != sizeof(kName) - 1U ||
      std::memcmp(name.data, kName, name.size) != 0) {
    return ASTER_STATUS_NOT_FOUND;
  }
  if (version != 1U) {
    return ASTER_STATUS_VERSION_MISMATCH;
  }
  *interface_table = &kBackend;
  *interface_struct_size = sizeof(kBackend);
  return ASTER_STATUS_OK;
}

const aster_core_plugin_t kPlugin{
    ASTER_ABI_VERSION,
    sizeof(aster_core_plugin_t),
    {"test-core-plugin", 16U},
    {"0.2.0", 5U},
    nullptr,
    QueryInterface,
};

}  // namespace

extern "C" const aster_core_plugin_t* AsterDynlibCreateCorePlugin() { return &kPlugin; }

extern "C" void AsterDynlibDestroyCorePlugin(const aster_core_plugin_t*) {}
