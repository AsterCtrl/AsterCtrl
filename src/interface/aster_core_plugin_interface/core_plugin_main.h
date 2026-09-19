#ifndef ASTER_CORE_PLUGIN_INTERFACE_MAIN_H_
#define ASTER_CORE_PLUGIN_INTERFACE_MAIN_H_

#include <stdint.h>

#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ASTER_CORE_PLUGIN_CREATE_SYMBOL "AsterDynlibCreateCorePlugin"
#define ASTER_CORE_PLUGIN_DESTROY_SYMBOL "AsterDynlibDestroyCorePlugin"

typedef struct aster_interface_header_t {
  uint32_t interface_version;
  uint32_t struct_size;
} aster_interface_header_t;

typedef struct aster_core_plugin_t {
  uint32_t abi_version;
  uint32_t struct_size;
  aster_string_view_t name;
  aster_string_view_t version;
  void* impl;
  aster_status_t (*query_interface)(void* impl, aster_string_view_t interface_name,
                                    uint32_t interface_version, const void** interface_table,
                                    uint32_t* interface_struct_size);
} aster_core_plugin_t;

typedef const aster_core_plugin_t* (*aster_core_plugin_create_t)(void);
typedef void (*aster_core_plugin_destroy_t)(const aster_core_plugin_t* plugin);

ASTER_INTERFACE_EXPORT const aster_core_plugin_t* AsterDynlibCreateCorePlugin(void);
ASTER_INTERFACE_EXPORT void AsterDynlibDestroyCorePlugin(const aster_core_plugin_t* plugin);

#ifdef __cplusplus
}
#endif

#endif /* ASTER_CORE_PLUGIN_INTERFACE_MAIN_H_ */
