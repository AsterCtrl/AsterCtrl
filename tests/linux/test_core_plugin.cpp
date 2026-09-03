#include <cstddef>
#include <cstdint>
#include <cstring>

#include "aster/plugin.h"

namespace {

struct TestBackendV1 {
  std::uint32_t interface_version;
  std::uint32_t struct_size;
  void* context;
  std::int32_t (*read_value)(void* context);
};

std::int32_t ReadValue(void*) { return 42; }

const TestBackendV1 kBackend{1U, sizeof(TestBackendV1), nullptr, ReadValue};

AsterStatusV1 QueryInterface(void*, AsterStringViewV1 name, std::uint32_t version,
                             const void** interface_table, std::uint32_t* interface_struct_size) {
  if (interface_table == nullptr || interface_struct_size == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT_V1;
  }
  *interface_table = nullptr;
  *interface_struct_size = 0;
  constexpr char kName[] = "test.backend";
  if (name.data == nullptr || name.size != sizeof(kName) - 1U ||
      std::memcmp(name.data, kName, name.size) != 0) {
    return ASTER_STATUS_NOT_FOUND_V1;
  }
  if (version != 1U) {
    return ASTER_STATUS_VERSION_MISMATCH_V1;
  }
  *interface_table = &kBackend;
  *interface_struct_size = sizeof(kBackend);
  return ASTER_STATUS_OK_V1;
}

const AsterCorePluginV1 kPlugin{
    ASTER_CORE_ABI_VERSION_V1,
    sizeof(AsterCorePluginV1),
    {"test-core-plugin", 16U},
    {"0.2.0", 5U},
    nullptr,
    QueryInterface,
    nullptr,
};

}  // namespace

extern "C" const AsterCorePluginV1* aster_core_plugin_v1() { return &kPlugin; }
