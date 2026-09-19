#ifndef ASTER_MODULE_C_INTERFACE_UTIL_STRING_H_
#define ASTER_MODULE_C_INTERFACE_UTIL_STRING_H_

#include <stddef.h>

typedef struct aster_string_view_t {
  const char* data;
  size_t size;
} aster_string_view_t;

#endif /* ASTER_MODULE_C_INTERFACE_UTIL_STRING_H_ */
