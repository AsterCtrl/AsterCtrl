#ifndef ASTER_MODULE_C_INTERFACE_PARAMETER_PARAMETER_BASE_H_
#define ASTER_MODULE_C_INTERFACE_PARAMETER_PARAMETER_BASE_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/value.h"

/* Get copies mutable payloads into caller storage. Set borrows input for the call. */
typedef struct aster_parameter_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*get)(void* impl, aster_string_view_t name, aster_value_t* output,
                        uint8_t* storage, size_t storage_capacity);
  aster_status_t (*set)(void* impl, aster_string_view_t name, const aster_value_t* value,
                        const aster_execution_context_t* caller);
} aster_parameter_base_t;
#endif
