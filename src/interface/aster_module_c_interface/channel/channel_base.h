#ifndef ASTER_MODULE_C_INTERFACE_CHANNEL_CHANNEL_BASE_H_
#define ASTER_MODULE_C_INTERFACE_CHANNEL_CHANNEL_BASE_H_

#include <stddef.h>
#include <stdint.h>

#include "aster_module_c_interface/executor/execution_context.h"
#include "aster_module_c_interface/util/status.h"
#include "aster_module_c_interface/util/string.h"
#include "aster_module_c_interface/util/type_support_base.h"

typedef struct aster_channel_descriptor_t {
  aster_string_view_t name;
  aster_type_descriptor_t message_type;
} aster_channel_descriptor_t;

typedef struct aster_message_info_t {
  uint32_t sequence;
  uint64_t source_timestamp_ns;
} aster_message_info_t;

/*
 * Registered callback state and descriptor string storage remain Module-owned
 * until Shutdown. Message pointers are borrowed only for the callback.
 */
typedef aster_status_t (*aster_channel_callback_t)(void* state, const uint8_t* message,
                                                   size_t message_size,
                                                   const aster_message_info_t* info,
                                                   const aster_execution_context_t* caller);

typedef struct aster_channel_base_t {
  uint32_t struct_size;
  void* impl;
  aster_status_t (*register_publisher)(void* impl, const aster_channel_descriptor_t* descriptor);
  aster_status_t (*register_subscriber)(void* impl, const aster_channel_descriptor_t* descriptor,
                                        aster_channel_callback_t callback, void* callback_state);
  aster_status_t (*publish)(void* impl, const aster_channel_descriptor_t* descriptor,
                            const uint8_t* message, size_t message_size,
                            uint64_t source_timestamp_ns, const aster_execution_context_t* caller);
} aster_channel_base_t;

#endif /* ASTER_MODULE_C_INTERFACE_CHANNEL_CHANNEL_BASE_H_ */
