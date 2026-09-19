#ifndef ASTER_MODULE_C_INTERFACE_ALLOCATOR_ALLOCATOR_BASE_H_
#define ASTER_MODULE_C_INTERFACE_ALLOCATOR_ALLOCATOR_BASE_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/util/status.h"

/* The caller owns returned memory and releases it through the same table. */
typedef struct aster_allocator_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*allocate)(void* impl, size_t size, size_t alignment, void** memory);
  aster_status_t (*deallocate)(void* impl, void* memory, size_t size, size_t alignment);
} aster_allocator_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_ALLOCATOR_ALLOCATOR_BASE_H_ */
