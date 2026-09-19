#include "aster_runtime/core/rpc.hpp"

#include "aster_runtime/execution.hpp"
#include "validation.hpp"

namespace aster {

RpcBackend::RpcBackend() noexcept
    : native_{sizeof(aster_rpc_base_t), this, RpcRegisterClient, RpcRegisterServer, RpcCallAsync} {}

aster_status_t RpcBackend::RpcRegisterClient(
    void* context, const aster_service_descriptor_t* descriptor) noexcept {
  if (context == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ServiceDescriptor native{};
  const auto status = FromAbiServiceDescriptor(descriptor, native);
  return IsOk(status) ? ToAbiStatus(static_cast<RpcBackend*>(context)->RegisterClient(native))
                      : ToAbiStatus(status);
}

aster_status_t RpcBackend::RpcRegisterServer(void* context,
                                             const aster_service_descriptor_t* descriptor,
                                             aster_rpc_handler_t handler,
                                             void* handler_state) noexcept {
  if (context == nullptr || handler == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ServiceDescriptor native{};
  const auto status = FromAbiServiceDescriptor(descriptor, native);
  return IsOk(status) ? ToAbiStatus(static_cast<RpcBackend*>(context)->RegisterServer(
                            native, handler, handler_state))
                      : ToAbiStatus(status);
}

aster_status_t RpcBackend::RpcCallAsync(void* context, const aster_service_descriptor_t* descriptor,
                                        const std::uint8_t* request, std::size_t request_size,
                                        std::uint64_t deadline_ns,
                                        aster_rpc_completion_t completion, void* completion_state,
                                        const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || completion == nullptr || !detail::ValidBuffer(request, request_size)) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  const auto context_status = ResolveCallContext(caller, resolved);
  if (!IsOk(context_status)) {
    return ToAbiStatus(context_status);
  }
  ServiceDescriptor native{};
  const auto status = FromAbiServiceDescriptor(descriptor, native);
  if (!IsOk(status)) {
    return ToAbiStatus(status);
  }
  return ToAbiStatus(static_cast<RpcBackend*>(context)->CallAsync(
      native, {reinterpret_cast<const std::byte*>(request), request_size}, deadline_ns, completion,
      completion_state, resolved));
}

}  // namespace aster
