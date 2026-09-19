#ifndef ASTER_MODULE_C_INTERFACE_RPC_RPC_BASE_H_
#define ASTER_MODULE_C_INTERFACE_RPC_RPC_BASE_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/type_support_base.h"

typedef struct aster_service_descriptor_t {
  aster_string_view_t name;
  aster_schema_hash_t schema_hash;
  aster_type_descriptor_t request_type;
  aster_type_descriptor_t response_type;
  aster_string_view_t instance_name;
} aster_service_descriptor_t;

typedef struct aster_rpc_call_info_t {
  uint32_t request_id;
  uint64_t deadline_ns;
} aster_rpc_call_info_t;

typedef aster_status_t (*aster_rpc_handler_t)(void* state, const uint8_t* request,
                                              size_t request_size, uint8_t* response,
                                              size_t response_capacity, size_t* response_size,
                                              const aster_rpc_call_info_t* info,
                                              const aster_execution_context_t* caller);
typedef void (*aster_rpc_completion_t)(void* state, aster_status_t status, const uint8_t* response,
                                       size_t response_size, const aster_rpc_call_info_t* info,
                                       const aster_execution_context_t* context);

typedef struct aster_rpc_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*register_client)(void* impl, const aster_service_descriptor_t* descriptor);
  aster_status_t (*register_server)(void* impl, const aster_service_descriptor_t* descriptor,
                                    aster_rpc_handler_t handler, void* handler_state);
  aster_status_t (*call_async)(void* impl, const aster_service_descriptor_t* descriptor,
                               const uint8_t* request, size_t request_size, uint64_t deadline_ns,
                               aster_rpc_completion_t completion, void* completion_state,
                               const aster_execution_context_t* caller);
} aster_rpc_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_RPC_RPC_BASE_H_ */
