#ifndef ASTER_PLUGIN_H_
#define ASTER_PLUGIN_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASTER_CORE_ABI_VERSION_V1 1u
#define ASTER_MODULE_BUNDLE_ENTRYPOINT_V1 "aster_module_bundle_v1"
#define ASTER_CORE_PLUGIN_ENTRYPOINT_V1 "aster_core_plugin_v1"

typedef int32_t AsterStatusV1;

enum {
  ASTER_STATUS_OK_V1 = 0,
  ASTER_STATUS_INVALID_ARGUMENT_V1 = 0x01000001,
  ASTER_STATUS_NOT_FOUND_V1 = 0x01000002,
  ASTER_STATUS_CAPACITY_EXCEEDED_V1 = 0x02000001,
  ASTER_STATUS_UNAVAILABLE_V1 = 0x02000002,
  ASTER_STATUS_ALREADY_EXISTS_V1 = 0x02000003,
  ASTER_STATUS_TIMEOUT_V1 = 0x03000001,
  ASTER_STATUS_CANCELLED_V1 = 0x03000002,
  ASTER_STATUS_TYPE_MISMATCH_V1 = 0x04000001,
  ASTER_STATUS_VERSION_MISMATCH_V1 = 0x04000002,
  ASTER_STATUS_PROTOCOL_ERROR_V1 = 0x04000003,
  ASTER_STATUS_INVALID_STATE_V1 = 0x05000001,
  ASTER_STATUS_INTERNAL_V1 = 0x06000001,
};

typedef struct AsterStringViewV1 {
  const char* data;
  size_t size;
} AsterStringViewV1;

enum {
  ASTER_EXECUTION_KIND_THREAD_V1 = 0,
  ASTER_EXECUTION_KIND_INTERRUPT_V1 = 1,
};

enum {
  ASTER_LOG_LEVEL_TRACE_V1 = 0,
  ASTER_LOG_LEVEL_DEBUG_V1 = 1,
  ASTER_LOG_LEVEL_INFO_V1 = 2,
  ASTER_LOG_LEVEL_WARNING_V1 = 3,
  ASTER_LOG_LEVEL_ERROR_V1 = 4,
  ASTER_LOG_LEVEL_CRITICAL_V1 = 5,
};

enum {
  ASTER_CLOCK_DOMAIN_MONOTONIC_V1 = 0,
  ASTER_CLOCK_DOMAIN_SYNCHRONIZED_V1 = 1,
  ASTER_CLOCK_DOMAIN_SIMULATED_V1 = 2,
  ASTER_CLOCK_DOMAIN_REPLAY_V1 = 3,
};

typedef struct AsterExecutionContextV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 executor_name;
  uint32_t kind;
  uint64_t timestamp_ns;
} AsterExecutionContextV1;

typedef struct AsterSchemaHashV1 {
  uint8_t bytes[16];
} AsterSchemaHashV1;

typedef struct AsterTypeDescriptorV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 name;
  AsterSchemaHashV1 schema_hash;
  uint64_t max_serialized_size;
} AsterTypeDescriptorV1;

typedef struct AsterChannelDescriptorV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 name;
  AsterTypeDescriptorV1 message_type;
} AsterChannelDescriptorV1;

typedef struct AsterServiceDescriptorV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 name;
  AsterSchemaHashV1 schema_hash;
  AsterTypeDescriptorV1 request_type;
  AsterTypeDescriptorV1 response_type;
} AsterServiceDescriptorV1;

typedef struct AsterMessageInfoV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t sequence;
  uint64_t source_timestamp_ns;
} AsterMessageInfoV1;

typedef struct AsterRpcCallInfoV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  uint32_t request_id;
  uint64_t deadline_ns;
} AsterRpcCallInfoV1;

#define ASTER_CORE_SERVICE_CONFIGURATOR_NAME_V1 "aster.configurator"
#define ASTER_CORE_SERVICE_LOGGER_NAME_V1 "aster.logger"
#define ASTER_CORE_SERVICE_EXECUTOR_NAME_V1 "aster.executor"
#define ASTER_CORE_SERVICE_CHANNEL_NAME_V1 "aster.channel"
#define ASTER_CORE_SERVICE_RPC_NAME_V1 "aster.rpc"
#define ASTER_CORE_SERVICE_PARAMETER_NAME_V1 "aster.parameter"
#define ASTER_CORE_SERVICE_CLOCK_NAME_V1 "aster.clock"
#define ASTER_CORE_SERVICE_ALLOCATOR_NAME_V1 "aster.allocator"
#define ASTER_CORE_SERVICE_HARDWARE_MANAGER_NAME_V1 "aster.hardware_manager"

#define ASTER_CORE_SERVICE_CONFIGURATOR_VERSION_V1 1u
#define ASTER_CORE_SERVICE_LOGGER_VERSION_V1 1u
#define ASTER_CORE_SERVICE_EXECUTOR_VERSION_V1 1u
#define ASTER_CORE_SERVICE_CHANNEL_VERSION_V1 1u
#define ASTER_CORE_SERVICE_RPC_VERSION_V1 1u
#define ASTER_CORE_SERVICE_PARAMETER_VERSION_V1 1u
#define ASTER_CORE_SERVICE_CLOCK_VERSION_V1 1u
#define ASTER_CORE_SERVICE_ALLOCATOR_VERSION_V1 1u
#define ASTER_CORE_SERVICE_HARDWARE_MANAGER_VERSION_V1 1u

/*
 * Unless a service says otherwise, input pointers are borrowed for the call
 * only. Output buffers remain caller-owned, and every returned service table
 * is borrowed from the CoreRef that returned it.
 */
typedef AsterStatusV1 (*AsterConfiguratorGetV1)(void* context, AsterStringViewV1 key,
                                                uint8_t* output, size_t output_capacity,
                                                size_t* written);

typedef struct AsterConfiguratorServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterConfiguratorGetV1 get;
} AsterConfiguratorServiceV1;

typedef AsterStatusV1 (*AsterLoggerWriteV1)(void* context, uint32_t level,
                                            AsterStringViewV1 message,
                                            const AsterExecutionContextV1* caller);

typedef struct AsterLoggerServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterLoggerWriteV1 write;
} AsterLoggerServiceV1;

/* Callback state is borrowed until the callback runs or scheduling fails. */
typedef void (*AsterWorkCallbackV1)(void* state, const AsterExecutionContextV1* context);
typedef AsterStatusV1 (*AsterExecutorGetNameV1)(void* context, AsterStringViewV1* name);
typedef AsterStatusV1 (*AsterExecutorTryPostV1)(void* context, AsterWorkCallbackV1 callback,
                                                void* callback_state,
                                                const AsterExecutionContextV1* caller);
typedef AsterStatusV1 (*AsterExecutorTryPostAtV1)(void* context, uint64_t timestamp_ns,
                                                  AsterWorkCallbackV1 callback,
                                                  void* callback_state,
                                                  const AsterExecutionContextV1* caller);

typedef struct AsterExecutorServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterExecutorGetNameV1 get_name;
  AsterExecutorTryPostV1 try_post;
  AsterExecutorTryPostAtV1 try_post_at;
} AsterExecutorServiceV1;

/*
 * Registered callback state and descriptor string storage remain plugin-owned
 * and must stay valid until the module bundle is released. Message and metadata
 * pointers are borrowed only for the duration of a callback.
 */
typedef AsterStatusV1 (*AsterChannelCallbackV1)(void* state, const uint8_t* message,
                                                size_t message_size, const AsterMessageInfoV1* info,
                                                const AsterExecutionContextV1* caller);
typedef AsterStatusV1 (*AsterChannelRegisterPublisherV1)(
    void* context, const AsterChannelDescriptorV1* descriptor);
typedef AsterStatusV1 (*AsterChannelRegisterSubscriberV1)(
    void* context, const AsterChannelDescriptorV1* descriptor, AsterChannelCallbackV1 callback,
    void* callback_state);
typedef AsterStatusV1 (*AsterChannelPublishV1)(void* context,
                                               const AsterChannelDescriptorV1* descriptor,
                                               const uint8_t* message, size_t message_size,
                                               uint64_t source_timestamp_ns,
                                               const AsterExecutionContextV1* caller);

typedef struct AsterChannelServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterChannelRegisterPublisherV1 register_publisher;
  AsterChannelRegisterSubscriberV1 register_subscriber;
  AsterChannelPublishV1 publish;
} AsterChannelServiceV1;

/*
 * Server callback state and descriptor string storage remain plugin-owned for
 * the bundle lifetime.
 * Completion state remains plugin-owned until completion or call rejection.
 * Request/response buffers and callback metadata are borrowed for each call.
 */
typedef AsterStatusV1 (*AsterRpcHandlerV1)(void* state, const uint8_t* request, size_t request_size,
                                           uint8_t* response, size_t response_capacity,
                                           size_t* response_size, const AsterRpcCallInfoV1* info,
                                           const AsterExecutionContextV1* caller);
typedef void (*AsterRpcCompletionV1)(void* state, AsterStatusV1 status, const uint8_t* response,
                                     size_t response_size, const AsterRpcCallInfoV1* info,
                                     const AsterExecutionContextV1* context);
typedef AsterStatusV1 (*AsterRpcRegisterClientV1)(void* context,
                                                  const AsterServiceDescriptorV1* descriptor);
typedef AsterStatusV1 (*AsterRpcRegisterServerV1)(void* context,
                                                  const AsterServiceDescriptorV1* descriptor,
                                                  AsterRpcHandlerV1 handler, void* handler_state);
typedef AsterStatusV1 (*AsterRpcCallAsyncV1)(void* context,
                                             const AsterServiceDescriptorV1* descriptor,
                                             const uint8_t* request, size_t request_size,
                                             uint64_t deadline_ns, AsterRpcCompletionV1 completion,
                                             void* completion_state,
                                             const AsterExecutionContextV1* caller);

typedef struct AsterRpcServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterRpcRegisterClientV1 register_client;
  AsterRpcRegisterServerV1 register_server;
  AsterRpcCallAsyncV1 call_async;
} AsterRpcServiceV1;

typedef AsterStatusV1 (*AsterParameterGetV1)(void* context, AsterStringViewV1 name,
                                             AsterStringViewV1 type, uint8_t* output,
                                             size_t output_capacity, size_t* written);
typedef AsterStatusV1 (*AsterParameterSetV1)(void* context, AsterStringViewV1 name,
                                             AsterStringViewV1 type, const uint8_t* value,
                                             size_t value_size,
                                             const AsterExecutionContextV1* caller);

typedef struct AsterParameterServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterParameterGetV1 get;
  AsterParameterSetV1 set;
} AsterParameterServiceV1;

typedef AsterStatusV1 (*AsterClockGetDomainV1)(void* context, uint32_t* domain);
typedef AsterStatusV1 (*AsterClockNowNsV1)(void* context, uint64_t* now_ns);

typedef struct AsterClockServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterClockGetDomainV1 get_domain;
  AsterClockNowNsV1 now_ns;
} AsterClockServiceV1;

/* The caller owns returned memory and must release it through this table. */
typedef AsterStatusV1 (*AsterAllocatorAllocateV1)(void* context, size_t size, size_t alignment,
                                                  void** memory);
typedef AsterStatusV1 (*AsterAllocatorDeallocateV1)(void* context, void* memory, size_t size,
                                                    size_t alignment);

typedef struct AsterAllocatorServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterAllocatorAllocateV1 allocate;
  AsterAllocatorDeallocateV1 deallocate;
} AsterAllocatorServiceV1;

/* Resolved device pointers are borrowed from the core. */
typedef AsterStatusV1 (*AsterHardwareManagerResolveV1)(void* context, AsterStringViewV1 name,
                                                       AsterStringViewV1 type, void** device);

typedef struct AsterHardwareManagerServiceV1 {
  uint32_t service_version;
  uint32_t struct_size;
  void* context;
  AsterHardwareManagerResolveV1 resolve;
} AsterHardwareManagerServiceV1;

/*
 * Service ABIs are queried by a stable name and version. The returned table is
 * borrowed from the core and remains valid for the lifetime of this CoreRef.
 */
typedef const void* (*AsterQueryCoreServiceV1)(void* context, AsterStringViewV1 service_name,
                                               uint32_t service_version,
                                               uint32_t* service_struct_size);

typedef struct AsterCoreRefV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  void* context;
  AsterQueryCoreServiceV1 query_service;
} AsterCoreRefV1;

typedef struct AsterSemanticVersionV1 {
  uint16_t major;
  uint16_t minor;
  uint16_t patch;
} AsterSemanticVersionV1;

typedef struct AsterModuleInfoV1 {
  AsterStringViewV1 name;
  AsterStringViewV1 type;
  AsterStringViewV1 package;
  AsterSemanticVersionV1 version;
} AsterModuleInfoV1;

typedef AsterStatusV1 (*AsterModuleInitializeV1)(void* instance, const AsterCoreRefV1* core);
typedef AsterStatusV1 (*AsterModuleStartV1)(void* instance);
typedef void (*AsterModuleShutdownV1)(void* instance);

/* Module instances and these descriptors are owned by their ModuleBundle. */
typedef struct AsterModuleV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterModuleInfoV1 info;
  void* instance;
  AsterModuleInitializeV1 initialize;
  AsterModuleStartV1 start;
  AsterModuleShutdownV1 shutdown;
} AsterModuleV1;

typedef void (*AsterReleaseModuleBundleV1)(void* owner, const AsterModuleV1* modules,
                                           size_t module_count);

/*
 * The plugin owns modules and owner until release is called exactly once.
 * A non-empty bundle must provide release. The core never frees plugin memory.
 */
typedef struct AsterModuleBundleV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  const AsterModuleV1* modules;
  size_t module_count;
  void* owner;
  AsterReleaseModuleBundleV1 release;
} AsterModuleBundleV1;

typedef AsterStatusV1 (*AsterCreateModuleBundleV1)(void* plugin_state, AsterModuleBundleV1* bundle);
typedef void (*AsterReleasePluginStateV1)(void* plugin_state);

/*
 * A ModuleBundle shared object exports this descriptor directly. Keeping its
 * entrypoint distinct from CorePlugin lets the loader reject a backend plugin
 * when a business Module package was requested.
 */
typedef struct AsterModuleBundlePluginV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 name;
  AsterStringViewV1 version;
  void* plugin_state;
  AsterCreateModuleBundleV1 create_module_bundle;
  AsterReleasePluginStateV1 release;
} AsterModuleBundlePluginV1;

typedef const AsterModuleBundlePluginV1* (*AsterModuleBundleEntrypointV1)(void);

/*
 * Core Plugins expose versioned C function tables for Transport and other
 * Runtime backends. Returned tables are borrowed until the plugin is closed.
 * Their concrete layout is owned by the named backend Interface and must start
 * with its own version and structure-size fields.
 */
typedef struct AsterInterfaceHeaderV1 {
  uint32_t interface_version;
  uint32_t struct_size;
} AsterInterfaceHeaderV1;

typedef AsterStatusV1 (*AsterCorePluginQueryInterfaceV1)(void* plugin_state,
                                                         AsterStringViewV1 interface_name,
                                                         uint32_t interface_version,
                                                         const void** interface_table,
                                                         uint32_t* interface_struct_size);

typedef struct AsterCorePluginV1 {
  uint32_t abi_version;
  uint32_t struct_size;
  AsterStringViewV1 name;
  AsterStringViewV1 version;
  void* plugin_state;
  AsterCorePluginQueryInterfaceV1 query_interface;
  AsterReleasePluginStateV1 release;
} AsterCorePluginV1;

/*
 * The entrypoint returns a descriptor borrowed from the loaded shared object.
 * If release is non-null, the loader calls it before unloading the object.
 */
typedef const AsterCorePluginV1* (*AsterCorePluginEntrypointV1)(void);

#ifdef __cplusplus
}
#endif

#endif /* ASTER_PLUGIN_H_ */
