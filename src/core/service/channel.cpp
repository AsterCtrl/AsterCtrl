#include "aster_runtime/core/channel.hpp"

#include "aster_runtime/execution.hpp"
#include "validation.hpp"

namespace aster {

ChannelBackend::ChannelBackend() noexcept
    : native_{sizeof(aster_channel_base_t), this, ChannelRegisterPublisher,
              ChannelRegisterSubscriber, ChannelPublish} {}

aster_status_t ChannelBackend::ChannelRegisterPublisher(
    void* context, const aster_channel_descriptor_t* descriptor) noexcept {
  if (context == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ChannelDescriptor native{};
  const auto status = FromAbiChannelDescriptor(descriptor, native);
  return IsOk(status)
             ? ToAbiStatus(static_cast<ChannelBackend*>(context)->RegisterPublisher(native))
             : ToAbiStatus(status);
}

aster_status_t ChannelBackend::ChannelRegisterSubscriber(
    void* context, const aster_channel_descriptor_t* descriptor, aster_channel_callback_t callback,
    void* callback_state) noexcept {
  if (context == nullptr || callback == nullptr) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ChannelDescriptor native{};
  const auto status = FromAbiChannelDescriptor(descriptor, native);
  return IsOk(status) ? ToAbiStatus(static_cast<ChannelBackend*>(context)->RegisterSubscriber(
                            native, callback, callback_state))
                      : ToAbiStatus(status);
}

aster_status_t ChannelBackend::ChannelPublish(void* context,
                                              const aster_channel_descriptor_t* descriptor,
                                              const std::uint8_t* message, std::size_t message_size,
                                              std::uint64_t source_timestamp_ns,
                                              const aster_execution_context_t* caller) noexcept {
  if (context == nullptr || !detail::ValidBuffer(message, message_size)) {
    return ASTER_STATUS_INVALID_ARGUMENT;
  }
  ExecutionContext resolved("unmanaged", ExecutionKind::kThread, 0);
  const auto context_status = ResolveCallContext(caller, resolved);
  if (!IsOk(context_status)) {
    return ToAbiStatus(context_status);
  }
  ChannelDescriptor native{};
  const auto status = FromAbiChannelDescriptor(descriptor, native);
  if (!IsOk(status)) {
    return ToAbiStatus(status);
  }
  return ToAbiStatus(static_cast<ChannelBackend*>(context)->Publish(
      native, {reinterpret_cast<const std::byte*>(message), message_size},
      caller == nullptr && source_timestamp_ns == 0 ? resolved.timestamp_ns() : source_timestamp_ns,
      resolved));
}

}  // namespace aster
