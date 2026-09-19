#include <stddef.h>

#include "aster_core_plugin_interface/core_plugin_main.h"
#include "aster_module_c_interface/aster_module_c_interface.h"
#include "aster_pkg_c_interface/pkg_main.h"

static aster_module_info_t Info(void* instance) {
  (void)instance;
  return (aster_module_info_t){{"c-module", 8}, {"test.CModule", 12}, {"c-package", 9}, {1, 0, 0}};
}

static aster_status_t Initialize(void* instance, const aster_core_base_t* core) {
  (void)instance;
  (void)core;
  return ASTER_STATUS_OK;
}

static aster_status_t Start(void* instance) {
  (void)instance;
  return ASTER_STATUS_OK;
}

static void Shutdown(void* instance) { (void)instance; }

int main(void) {
  int state = 0;
  const aster_module_base_t module = {
      ASTER_ABI_VERSION, sizeof(aster_module_base_t), &state, Info, Initialize, Start, Shutdown};
  return offsetof(aster_module_base_t, abi_version) == 0 && module.info(module.impl).name.size == 8
             ? 0
             : 1;
}
