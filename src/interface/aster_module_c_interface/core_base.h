#ifndef ASTER_MODULE_C_INTERFACE_CORE_BASE_H_
#define ASTER_MODULE_C_INTERFACE_CORE_BASE_H_

#include <stdint.h>

#include "aster_module_c_interface/allocator/allocator_base.h"
#include "aster_module_c_interface/channel/channel_base.h"
#include "aster_module_c_interface/clock/clock_base.h"
#include "aster_module_c_interface/configurator/configurator_base.h"
#include "aster_module_c_interface/executor/executor_base.h"
#include "aster_module_c_interface/hardware/hardware_manager_base.h"
#include "aster_module_c_interface/logger/logger_base.h"
#include "aster_module_c_interface/parameter/parameter_base.h"
#include "aster_module_c_interface/rpc/rpc_base.h"
#include "aster_module_c_interface/util/version.h"

/*
 * Stable Module-facing Core interface. New accessors may only be appended.
 * Callers must check abi_version and struct_size before using the table. A Core
 * must keep every returned service table alive until its Modules have stopped.
 * Service calls with a nullable caller argument use the Runtime execution
 * context when it is null. Module Packages do not maintain their own context.
 */
typedef struct aster_core_base_t {
  uint32_t abi_version;
  uint32_t struct_size;
  void* impl;
  const aster_configurator_base_t* (*configurator)(void* impl);
  const aster_logger_base_t* (*logger)(void* impl);
  const aster_executor_base_t* (*executor)(void* impl);
  const aster_channel_base_t* (*channel)(void* impl);
  const aster_rpc_base_t* (*rpc)(void* impl);
  const aster_parameter_base_t* (*parameter)(void* impl);
  const aster_clock_base_t* (*clock)(void* impl);
  const aster_allocator_base_t* (*allocator)(void* impl);
  const aster_hardware_manager_base_t* (*hardware)(void* impl);
  /* Borrowed instance identity, distinct from the exported Module type. */
  aster_string_view_t (*instance_name)(void* impl);
} aster_core_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_CORE_BASE_H_ */
