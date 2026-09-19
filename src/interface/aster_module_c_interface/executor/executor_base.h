#ifndef ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTOR_BASE_H_
#define ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTOR_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"

/* Callback state is borrowed until the callback runs or scheduling fails. */
typedef void (*aster_work_fn_t)(void* state, const aster_execution_context_t* context);

typedef struct aster_executor_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*get_name)(void* impl, aster_string_view_t* name);
  aster_status_t (*try_post)(void* impl, aster_work_fn_t callback, void* callback_state,
                             const aster_execution_context_t* caller);
  aster_status_t (*try_post_at)(void* impl, uint64_t timestamp_ns, aster_work_fn_t callback,
                                void* callback_state, const aster_execution_context_t* caller);
} aster_executor_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_EXECUTOR_EXECUTOR_BASE_H_ */
