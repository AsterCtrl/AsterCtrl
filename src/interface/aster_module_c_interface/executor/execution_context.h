#ifndef ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTION_CONTEXT_H_
#define ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTION_CONTEXT_H_

#include <stdint.h>

#include "aster_module_c_interface/util/string.h"

enum {
  ASTER_EXECUTION_KIND_THREAD = 0,
  ASTER_EXECUTION_KIND_INTERRUPT = 1,
};

typedef struct aster_execution_context_t {
  aster_string_view_t executor_name;
  uint32_t kind;
  uint64_t timestamp_ns;
} aster_execution_context_t;

#endif /* ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTION_CONTEXT_H_ */
