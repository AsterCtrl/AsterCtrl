#ifndef ASTER_MODULE_C_INTERFACE_CLOCK_CLOCK_BASE_H_
#define ASTER_MODULE_C_INTERFACE_CLOCK_CLOCK_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/util/status.h"

enum {
  ASTER_CLOCK_DOMAIN_MONOTONIC = 0,
  ASTER_CLOCK_DOMAIN_SYNCHRONIZED = 1,
  ASTER_CLOCK_DOMAIN_SIMULATED = 2,
  ASTER_CLOCK_DOMAIN_REPLAY = 3,
};

typedef struct aster_clock_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*get_domain)(void* impl, uint32_t* domain);
  aster_status_t (*now_ns)(void* impl, uint64_t* now_ns);
} aster_clock_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_CLOCK_CLOCK_BASE_H_ */
