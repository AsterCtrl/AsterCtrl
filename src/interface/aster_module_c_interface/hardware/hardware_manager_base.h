#ifndef ASTER_MODULE_C_INTERFACE_HARDWARE_HARDWARE_MANAGER_BASE_H_
#define ASTER_MODULE_C_INTERFACE_HARDWARE_HARDWARE_MANAGER_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"

/* Resolved device pointers are borrowed from the Core. */
typedef struct aster_hardware_manager_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*resolve)(void* impl, aster_string_view_t name, aster_string_view_t type,
                            void** device);
} aster_hardware_manager_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_HARDWARE_HARDWARE_MANAGER_BASE_H_ */
