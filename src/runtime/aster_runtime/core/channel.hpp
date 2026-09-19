#pragma once

#include "aster_module_cpp_interface/channel.hpp"
#include "aster_runtime/registry.hpp"

namespace aster {

class ChannelBackend : public Registry {
 public:
  ChannelBackend() noexcept;
  ChannelBackend(const ChannelBackend&) = delete;
  ChannelBackend& operator=(const ChannelBackend&) = delete;
  [[nodiscard]] const aster_channel_base_t* NativeHandle() const noexcept { return &native_; }
  virtual Status RegisterPublisher(const ChannelDescriptor& descriptor) noexcept = 0;
  virtual Status RegisterSubscriber(const ChannelDescriptor& descriptor,
                                    aster_channel_callback_t callback,
                                    void* callback_state) noexcept = 0;
  virtual Status Publish(const ChannelDescriptor& descriptor, std::span<const std::byte> message,
                         std::uint64_t source_timestamp_ns,
                         const ExecutionContext& caller) noexcept = 0;

 private:
  static aster_status_t ChannelRegisterPublisher(
      void* context, const aster_channel_descriptor_t* descriptor) noexcept;
  static aster_status_t ChannelRegisterSubscriber(void* context,
                                                  const aster_channel_descriptor_t* descriptor,
                                                  aster_channel_callback_t callback,
                                                  void* callback_state) noexcept;
  static aster_status_t ChannelPublish(void* context, const aster_channel_descriptor_t* descriptor,
                                       const std::uint8_t* message, std::size_t message_size,
                                       std::uint64_t source_timestamp_ns,
                                       const aster_execution_context_t* caller) noexcept;
  aster_channel_base_t native_;
};

}  // namespace aster
