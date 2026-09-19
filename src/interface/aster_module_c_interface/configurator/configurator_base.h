#ifndef ASTER_MODULE_C_INTERFACE_CONFIGURATOR_CONFIGURATOR_BASE_H_
#define ASTER_MODULE_C_INTERFACE_CONFIGURATOR_CONFIGURATOR_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/value.h"

/* Keys are borrowed for the call. Returned payloads remain immutable and valid
 * until the Module has shut down. The output value itself is caller-owned. */
typedef struct aster_configurator_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*get)(void* impl, aster_string_view_t key, aster_value_t* output);
} aster_configurator_base_t;
#endif
