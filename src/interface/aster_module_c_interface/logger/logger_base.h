#ifndef ASTER_MODULE_C_INTERFACE_LOGGER_LOGGER_BASE_H_
#define ASTER_MODULE_C_INTERFACE_LOGGER_LOGGER_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"

enum {
  ASTER_LOG_LEVEL_TRACE = 0,
  ASTER_LOG_LEVEL_DEBUG = 1,
  ASTER_LOG_LEVEL_INFO = 2,
  ASTER_LOG_LEVEL_WARNING = 3,
  ASTER_LOG_LEVEL_ERROR = 4,
  ASTER_LOG_LEVEL_CRITICAL = 5,
};

typedef struct aster_logger_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*write)(void* impl, uint32_t level, aster_string_view_t message,
                          const aster_execution_context_t* caller);
} aster_logger_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_LOGGER_LOGGER_BASE_H_ */
