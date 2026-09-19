#ifndef ASTER_MODULE_C_INTERFACE_UTIL_VALUE_H_
#define ASTER_MODULE_C_INTERFACE_UTIL_VALUE_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/util/string.h"

#define ASTER_VALUE_NULL 0u
#define ASTER_VALUE_BOOL 1u
#define ASTER_VALUE_INT64 2u
#define ASTER_VALUE_UINT64 3u
#define ASTER_VALUE_FLOAT64 4u
#define ASTER_VALUE_STRING 5u
#define ASTER_VALUE_BYTES 6u

typedef struct aster_bytes_view_t {
  const uint8_t* data;
  size_t size;
} aster_bytes_view_t;

/* Tagged values, never the object representation of a C++ class.
 * Configurator payloads are immutable and borrowed until Module shutdown.
 * Parameter reads copy payloads into caller-owned storage. */
typedef struct aster_value_t {
  uint32_t kind;
  union {
    uint8_t boolean;
    int64_t integer;
    uint64_t unsigned_integer;
    double real;
    aster_string_view_t string;
    aster_bytes_view_t bytes;
  } data;
} aster_value_t;
#endif
