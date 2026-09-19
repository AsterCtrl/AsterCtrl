#ifndef ASTER_MODULE_C_INTERFACE_UTIL_VERSION_H_
#define ASTER_MODULE_C_INTERFACE_UTIL_VERSION_H_

#include <stdint.h>

/*
 * This is the only ABI version visible to a Module. Leaf value types are not
 * independently versioned; extensible function tables carry struct_size.
 */
#define ASTER_ABI_VERSION 2u

#if defined(_WIN32)
#define ASTER_INTERFACE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define ASTER_INTERFACE_EXPORT __attribute__((visibility("default")))
#else
#define ASTER_INTERFACE_EXPORT
#endif

typedef struct aster_semantic_version_t {
  uint16_t major;
  uint16_t minor;
  uint16_t patch;
} aster_semantic_version_t;

#endif /* ASTER_MODULE_C_INTERFACE_UTIL_VERSION_H_ */
