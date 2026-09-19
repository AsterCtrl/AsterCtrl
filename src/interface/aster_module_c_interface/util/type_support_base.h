#ifndef ASTER_MODULE_C_INTERFACE_UTIL_TYPE_SUPPORT_BASE_H_
#define ASTER_MODULE_C_INTERFACE_UTIL_TYPE_SUPPORT_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/util/string.h"

typedef struct aster_schema_hash_t {
  uint8_t bytes[16];
} aster_schema_hash_t;

typedef struct aster_type_descriptor_t {
  aster_string_view_t name;
  aster_schema_hash_t schema_hash;
  uint64_t max_serialized_size;
} aster_type_descriptor_t;

#endif /* ASTER_MODULE_C_INTERFACE_UTIL_TYPE_SUPPORT_BASE_H_ */
