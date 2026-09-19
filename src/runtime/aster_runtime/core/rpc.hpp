#pragma once

#include "aster_module_cpp_interface/rpc.hpp"
#include "aster_runtime/registry.hpp"

namespace aster {

class RpcBackend : public Registry {
 public:
  RpcBackend() noexcept;
  RpcBackend(const RpcBackend&) = delete;
  RpcBackend& operator=(const RpcBackend&) = delete;
  [[nodiscard]] const aster_rpc_base_t* NativeHandle() const noexcept { return &native_; }
  virtual Status RegisterClient(const ServiceDescriptor& descriptor) noexcept = 0;
  virtual Status RegisterServer(const ServiceDescriptor& descriptor, RawRpcHandler handler,
                                void* handler_state) noexcept = 0;
  virtual Status CallAsync(const ServiceDescriptor& descriptor, std::span<const std::byte> request,
                           std::uint64_t deadline_ns, RawRpcCompletion completion,
                           void* completion_state, const ExecutionContext& caller) noexcept = 0;

 private:
  static aster_status_t RpcRegisterClient(void* context,
                                          const aster_service_descriptor_t* descriptor) noexcept;
  static aster_status_t RpcRegisterServer(void* context,
                                          const aster_service_descriptor_t* descriptor,
                                          aster_rpc_handler_t handler,
                                          void* handler_state) noexcept;
  static aster_status_t RpcCallAsync(void* context, const aster_service_descriptor_t* descriptor,
                                     const std::uint8_t* request, std::size_t request_size,
                                     std::uint64_t deadline_ns, aster_rpc_completion_t completion,
                                     void* completion_state,
                                     const aster_execution_context_t* caller) noexcept;
  aster_rpc_base_t native_;
};

}  // namespace aster
