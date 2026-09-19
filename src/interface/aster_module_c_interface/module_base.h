#ifndef ASTER_MODULE_C_INTERFACE_MODULE_BASE_H_
#define ASTER_MODULE_C_INTERFACE_MODULE_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/core_base.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/version.h"

typedef struct aster_module_info_t {
  aster_string_view_t name;
  aster_string_view_t type;
  aster_string_view_t package;
  aster_semantic_version_t version;
} aster_module_info_t;

/*
 * Canonical Module ABI. The C++ ModuleBase facade embeds one of these tables;
 * the Runtime invokes this table for both static and dynamic Modules.
 */
typedef struct aster_module_base_t {
  uint32_t abi_version;
  uint32_t struct_size;
  void* impl;
  aster_module_info_t (*info)(void* impl);
  aster_status_t (*initialize)(void* impl, const aster_core_base_t* core);
  aster_status_t (*start)(void* impl);
  void (*shutdown)(void* impl);
} aster_module_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_MODULE_BASE_H_ */
